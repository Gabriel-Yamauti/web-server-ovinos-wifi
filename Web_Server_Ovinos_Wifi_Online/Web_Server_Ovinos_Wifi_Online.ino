/*********
  Rui Santos & Sara Santos - Random Nerd Tutorials
  Projeto original em: https://RandomNerdTutorials.com/esp32-mpu-6050-web-server/
  Código modificado e expandido com novas funcionalidades.
 *********/
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Arduino_JSON.h>
#include "LittleFS.h"
#include <math.h>

// --- CREDENCIAIS DA REDE ---
const char* ssid = "XXXXXX";
const char* password = "XXXXXX";

// --- OBJETOS DO SERVIDOR ---
AsyncWebServer server(80);
AsyncEventSource events("/events");

// --- VARIÁVEIS GLOBAIS ---
// Sensor e Leituras
Adafruit_MPU6050 mpu;
sensors_event_t a, g, temp;
JSONVar readings;
float gyroX, gyroY, gyroZ;
float accX, accY, accZ;
float temperature;

// Calibração/Erro do Giroscópio
float gyroXerror = 0.07;
float gyroYerror = 0.03;
float gyroZerror = 0.01;

// Timers para envio de dados
unsigned long lastTime = 0;
unsigned long lastTimeTemperature = 0;
unsigned long lastTimeAcc = 0;
unsigned long gyroDelay = 10;
unsigned long temperatureDelay = 1000;
unsigned long accelerometerDelay = 200;

// Timers para Status (Memória e Cronômetro)
unsigned long startTime = 0;
unsigned long lastStatusTime = 0;
unsigned long statusDelay = 1000; // Envia o status a cada 1 segundos

// VARIÁVEL PARA O AUTO-RESET
unsigned long stationaryStartTime = 0;

// --- FUNÇÕES DE INICIALIZAÇÃO ---

void initLittleFS() {
  if (!LittleFS.begin()) {
    Serial.println("An error has occurred while mounting LittleFS");
  }
  Serial.println("LittleFS mounted successfully");
}

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("");
  Serial.println(WiFi.localIP());
}

void initMPU() {
  if (!mpu.begin()) {
    Serial.println("Failed to find MPU6050 chip");
    while (1) {
      delay(10);
    }
  }
  Serial.println("MPU6050 Found!");
}

// --- FUNÇÕES DE LEITURA E FORMATAÇÃO DE DADOS ---

String getGyroReadings() {
  mpu.getEvent(&a, &g, &temp);
  float gyroX_temp = g.gyro.x;
  if (abs(gyroX_temp) > gyroXerror) {
    gyroX += gyroX_temp / 50.00;
  }
  float gyroY_temp = g.gyro.y;
  if (abs(gyroY_temp) > gyroYerror) {
    gyroY += gyroY_temp / 70.00;
  }
  float gyroZ_temp = g.gyro.z;
  if (abs(gyroZ_temp) > gyroZerror) {
    gyroZ += gyroZ_temp / 90.00;
  }
  // --- CONVERSÃO DE RADIANOS PARA GRAUS ---
  float gyroX_deg = gyroX * 180.0 / M_PI;
  float gyroY_deg = gyroY * 180.0 / M_PI;
  float gyroZ_deg = gyroZ * 180.0 / M_PI;

  readings["gyroX"] = String(gyroX_deg);
  readings["gyroY"] = String(gyroY_deg);
  readings["gyroZ"] = String(gyroZ_deg);
  // --- FIM DA CONVERSÃO ---

  String jsonString = JSON.stringify(readings);
  return jsonString;
}

String getAccReadings() {
  mpu.getEvent(&a, &g, &temp);
  accX = a.acceleration.x;
  accY = a.acceleration.y;
  accZ = a.acceleration.z;
  readings["accX"] = String(accX);
  readings["accY"] = String(accY);
  readings["accZ"] = String(accZ);
  String accString = JSON.stringify(readings);
  return accString;
}

String getTemperature() {
  mpu.getEvent(&a, &g, &temp);
  temperature = temp.temperature;
  return String(temperature);
}

// --- FUNÇÃO DE LOG EM CSV ---

void logDataToCSV() {
  File dataFile = LittleFS.open("/sensor_data.csv", "a");
  if (!dataFile) {
    Serial.println("Failed to open data file for appending");
    return;
  }

  // --- CONVERTE O ÂNGULO GLOBAL ACUMULADO PARA GRAUS ---
  float gyroX_deg_log = gyroX * 180.0 / M_PI;
  float gyroY_deg_log = gyroY * 180.0 / M_PI;
  float gyroZ_deg_log = gyroZ * 180.0 / M_PI;

  // Usa as variáveis globais que já contêm as últimas leituras
  // e os ângulos em graus.
  String dataString = String(millis()) + "," +
                      String(accX) + "," +
                      String(accY) + "," +
                      String(accZ) + "," +
                      String(gyroX_deg_log) + "," +
                      String(gyroY_deg_log) + "," +
                      String(gyroZ_deg_log) + ",";

  if (!dataFile.println(dataString)) {
    Serial.println("CSV write failed");
  }
  dataFile.close();
}


// --- SETUP ---
void setup() {
  Serial.begin(115200);
  startTime = millis(); // Inicia o timer de gravação
  
  initWiFi();
  initLittleFS();
  initMPU();

  // Cria o arquivo CSV com cabeçalho se ele não existir
  File dataFile = LittleFS.open("/sensor_data.csv", "r");
  if (!dataFile) {
    Serial.println("Creating CSV file with header...");
    dataFile = LittleFS.open("/sensor_data.csv", "w");
    if (dataFile) {
      dataFile.println("timestamp,accX,accY,accZ,gyroX,gyroY,gyroZ");
      dataFile.close();
    }
  } else {
    dataFile.close();
  }

  // --- ROTAS DO SERVIDOR WEB ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/index.html", "text/html");
  });

  server.serveStatic("/", LittleFS, "/");

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    gyroX = 0; gyroY = 0; gyroZ = 0;
    request->send(200, "text/plain", "OK");
  });
  server.on("/resetX", HTTP_GET, [](AsyncWebServerRequest *request) {
    gyroX = 0;
    request->send(200, "text/plain", "OK");
  });
  server.on("/resetY", HTTP_GET, [](AsyncWebServerRequest *request) {
    gyroY = 0;
    request->send(200, "text/plain", "OK");
  });
  server.on("/resetZ", HTTP_GET, [](AsyncWebServerRequest *request) {
    gyroZ = 0;
    request->send(200, "text/plain", "OK");
  });

  // Rota para baixar o arquivo CSV
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/sensor_data.csv", "text/csv", true);
  });

  // Rota para limpar os dados do CSV
  server.on("/clear", HTTP_GET, [](AsyncWebServerRequest *request) {
    LittleFS.remove("/sensor_data.csv");
    File dataFile = LittleFS.open("/sensor_data.csv", "w");
    if (dataFile) {
      dataFile.println("timestamp,accX,accY,accZ,gyroX,gyroY,gyroZ");
      dataFile.close();
      request->send(200, "text/plain", "Dados do CSV apagados com sucesso!");
    } else {
      request->send(500, "text/plain", "Erro ao limpar os dados.");
    }
  });

  // --- EVENTOS DO SERVIDOR (SSE) ---
  events.onConnect([](AsyncEventSourceClient *client) {
    if (client->lastId()) {
      Serial.printf("Client reconnected! Last message ID: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 10000);
  });
  server.addHandler(&events);

  server.begin();
}


// --- LOOP PRINCIPAL ---
void loop() {
  if ((millis() - lastTime) > gyroDelay) {
    events.send(getGyroReadings().c_str(), "gyro_readings", millis());
    lastTime = millis();
  }
  if ((millis() - lastTimeAcc) > accelerometerDelay) {
    // Envia os dados do acelerômetro para a página web e salva no CSV
    events.send(getAccReadings().c_str(), "accelerometer_readings", millis());
    logDataToCSV();
    lastTimeAcc = millis();

    // --- INÍCIO DA LÓGICA DE AUTO-RESET ---
    
    // 1. Calcula a magnitude do vetor de aceleração
    float totalAcc = sqrt(accX * accX + accY * accY + accZ * accZ);

    // 2. Verifica se o sensor está parado (magnitude próxima à da gravidade)
    if (totalAcc >= 9.0 && totalAcc <= 11.0) {
      // Se o sensor está parado, iniciamos um cronômetro
      if (stationaryStartTime == 0) {
        stationaryStartTime = millis();
      }
    }
    else {
      // Se o sensor se moveu, reiniciamos o cronômetro
      stationaryStartTime = 0;
    }

    // 3. Se o cronômetro foi iniciado e já se passaram 10 segundos
    if (stationaryStartTime > 0 && millis() - stationaryStartTime > 10000) {
      // Reseta o giroscópio para 0
      gyroX = 0;
      gyroY = 0;
      gyroZ = 0;
      
      Serial.println("Giroscopio resetado automaticamente por inatividade.");
      
      // Reinicia o cronômetro para não ficar resetando a cada ciclo
      stationaryStartTime = 0; 
    }
    // --- FIM DA LÓGICA DE AUTO-RESET ---
  }
  if ((millis() - lastTimeTemperature) > temperatureDelay) {
    events.send(getTemperature().c_str(), "temperature_reading", millis());
    lastTimeTemperature = millis();
  }
  
  if ((millis() - lastStatusTime) > statusDelay) {
    // Calcula o tempo decorrido em milissegundos
    unsigned long elapsedTime = millis() - startTime;

    // --- CÓDIGO CORRIGIDO PARA OBTER INFORMAÇÕES DE ARMAZENAMENTO ---
    FSInfo fs_info; // Cria uma estrutura para guardar as informações
    LittleFS.info(fs_info); // Pede ao LittleFS para preencher a estrutura
    
    long totalBytes = fs_info.totalBytes; // Acessa os dados de dentro da estrutura
    long usedBytes = fs_info.usedBytes;   // Acessa os dados de dentro da estrutura
    
    int storagePercent = (totalBytes > 0) ? (int)((usedBytes * 100.0) / totalBytes) : 0;
    // --- FIM DA CORREÇÃO ---

    // Cria um objeto JSON para enviar os dados
    JSONVar statusReadings;
    statusReadings["elapsed"] = String(elapsedTime);
    statusReadings["used"] = String(usedBytes);
    statusReadings["total"] = String(totalBytes);
    statusReadings["percent"] = String(storagePercent);

    String jsonString = JSON.stringify(statusReadings);
    
    // Envia o evento "storage_info" para a página web
    events.send(jsonString.c_str(), "storage_info", millis());
    
    lastStatusTime = millis();
  }
}
