#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

// Configuración de red y Supabase

const char* ssid = "MOVISTAR_4C20";
const char* password = "fBojctXpeDGPms9KyQPG";

String supabase_url = "https://ogwgfiaqfcvxptrbnacw.supabase.co"; 
String supabase_key = "sb_publishable_kTtgApojl0XdaKwQ02CUCw_MnB8WF4H";

// Variables del sensor

MAX30105 particleSensor;

uint32_t irBuffer[100];
uint32_t redBuffer[100];

int32_t bufferLength;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

// Media de los últimos 10 valores

const byte NUM_MUESTRAS = 10;

int hrHist[NUM_MUESTRAS];
int spo2Hist[NUM_MUESTRAS];

byte hrIndex = 0;
byte spo2Index = 0;
byte hrCount = 0;
byte spo2Count = 0;

// Contador de medias

int contadorMedias = 0;

// Variables de Estado Vigía

bool esperandoPaciente = true;
String id_evaluacion_pendiente = ""; 
unsigned long ultimoChequeoNube = 0;
const long intervaloChequeo = 5000;

// FUNCIONES MATEMÁTICAS 

int calcularMediaHR() {
  long suma = 0;
  for (byte i = 0; i < hrCount; i++) {
    suma += hrHist[i];
  }
  if (hrCount == 0) return 0;
  return (suma / hrCount) / 2; 
}

int calcularMediaSPO2() {
  long suma = 0;
  for (byte i = 0; i < spo2Count; i++) {
    suma += spo2Hist[i];
  }
  if (spo2Count == 0) return 0;
  return suma / spo2Count;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Wifi

  Serial.println(F("================================"));
  Serial.print(F("Conectando a WiFi: "));
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  Serial.println(F("\n WiFi Conectado!"));

  // Iniciar sensor

  Serial.println(F("Iniciando sistema del sensor..."));

  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(F("MAX30105 no encontrado. Revisa las conexiones."));
    while (1) { yield(); }
  }

  Serial.println(F("Sensor conectado. Configurando..."));

  byte ledBrightness = 15;
  byte sampleAverage = 6;
  byte ledMode = 2;
  byte sampleRate = 100;
  int pulseWidth = 411;
  int adcRange = 4096;

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  // Apagar la luz mientras no haya nadie esperando 

  particleSensor.setPulseAmplitudeRed(0); 
  particleSensor.setPulseAmplitudeIR(0);

  Serial.println(F("Sistema Listo. Vigía activado esperando pacientes..."));
  Serial.println(F("================================"));
}

void loop() {
  
  // Esperar paciente
  
  if (esperandoPaciente) {
    
    if (millis() - ultimoChequeoNube >= intervaloChequeo) {
      ultimoChequeoNube = millis();
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F("Buscando pacientes pendientes en Supabase..."));
        HTTPClient http;
        
        // Buscar fila con bpm y spo2 vacío

        String urlBusqueda = supabase_url + "/rest/v1/evaluaciones?select=id&bpm=is.null&order=fecha.desc&limit=1";
        
        http.begin(urlBusqueda);
        http.addHeader("apikey", supabase_key);
        http.addHeader("Authorization", "Bearer " + supabase_key);
        
        int httpCode = http.GET();
        
        if (httpCode == 200) {
          String payload = http.getString();
          StaticJsonDocument<512> doc;
          deserializeJson(doc, payload);
          
          if (doc.size() > 0) {
            
            id_evaluacion_pendiente = doc[0]["id"].as<String>();
            Serial.print(F("¡Paciente Encontrado! ID: "));
            Serial.println(id_evaluacion_pendiente);
            
            // Resetear las variables 

            hrIndex = 0;
            spo2Index = 0;
            hrCount = 0;
            spo2Count = 0;
            contadorMedias = 0;
            esperandoPaciente = false;
            
            // Encender la luz 

            particleSensor.setPulseAmplitudeRed(15); 
            particleSensor.setPulseAmplitudeIR(15);
            
            Serial.println(F("LUZ ENCENDIDA - EMPEZANDO LECTURAS"));
          } else {
             Serial.println(F("Nadie esperando."));
          }
        }
        http.end();
      }
    }
  } 
  
  // Medición
  
  else { 
    bufferLength = 100;

    for (byte i = 0; i < bufferLength; i++) {
      while (!particleSensor.available()) {
        particleSensor.check();
        yield();
      }
      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

    while (!esperandoPaciente) {
      yield();

      // Desplazar muestras

      for (byte i = 25; i < 100; i++) {
        redBuffer[i - 25] = redBuffer[i];
        irBuffer[i - 25] = irBuffer[i];
      }

      // Leer 25 muestras nuevas

      for (byte i = 75; i < 100; i++) {
        while (!particleSensor.available()) {
          particleSensor.check();
          yield();
        }
        redBuffer[i] = particleSensor.getRed();
        irBuffer[i] = particleSensor.getIR();
        particleSensor.nextSample();
      }

      // Calcular BPM y SpO2

      maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

      // Guardar BPM válidos

      if (validHeartRate && heartRate > 40 && heartRate < 220) {
        hrHist[hrIndex] = heartRate;
        hrIndex++;
        if (hrIndex >= NUM_MUESTRAS) hrIndex = 0;
        if (hrCount < NUM_MUESTRAS) hrCount++;
      }

      // Guardar SpO2 válidos

      if (validSPO2 && spo2 >= 80 && spo2 <= 100) {
        spo2Hist[spo2Index] = spo2;
        spo2Index++;
        if (spo2Index >= NUM_MUESTRAS) spo2Index = 0;
        if (spo2Count < NUM_MUESTRAS) spo2Count++;
      }

      // Calcular medias

      int mediaHR = calcularMediaHR();
      int mediaSPO2 = calcularMediaSPO2();

      // Contar medias 

      if (hrCount >= NUM_MUESTRAS && spo2Count >= NUM_MUESTRAS) {
        contadorMedias++;
      }

      // Mostrar resultados 

      Serial.print(F("IR=")); Serial.print(irBuffer[99]);
      Serial.print(F(", RED=")); Serial.print(redBuffer[99]);
      Serial.print(F(", HR=")); Serial.print(heartRate);
      Serial.print(F(", HR_MEDIO=")); Serial.print(mediaHR);
      Serial.print(F(", HRvalid=")); Serial.print(validHeartRate);
      Serial.print(F(", SPO2=")); Serial.print(spo2);
      Serial.print(F(", SPO2_MEDIO=")); Serial.print(mediaSPO2);
      Serial.print(F(", SPO2Valid=")); Serial.print(validSPO2);
      Serial.print(F(", MEDIA_NUM=")); Serial.println(contadorMedias);

      // Parar medición
      
      if (contadorMedias >= 5) {
        Serial.println();
        Serial.println(F("================================"));
        Serial.println(F("FIN DE MEDICION"));
        Serial.print(F("HR MEDIO FINAL = ")); Serial.println(mediaHR);
        Serial.print(F("SPO2 MEDIO FINAL = ")); Serial.println(mediaSPO2);
        Serial.println(F("================================"));

        // Mandar los datos a Supabase

        if (WiFi.status() == WL_CONNECTED) {
          Serial.println(F("Subiendo datos a Supabase..."));
          HTTPClient http;
          
          String urlUpdate = supabase_url + "/rest/v1/evaluaciones?id=eq." + id_evaluacion_pendiente;
          
          http.begin(urlUpdate);
          http.addHeader("apikey", supabase_key);
          http.addHeader("Authorization", "Bearer " + supabase_key);
          http.addHeader("Content-Type", "application/json");
          http.addHeader("Prefer", "return=minimal");
          
          String jsonPayload = "{\"bpm\": " + String(mediaHR) + ", \"spo2\": " + String(mediaSPO2) + "}";
          int httpCode = http.PATCH(jsonPayload);
          
          if (httpCode == 204 || httpCode == 200) {
            Serial.println(F("¡Datos guardados con éxito!"));
          } else {
            Serial.print(F("Error subiendo a la nube: "));
            Serial.println(httpCode);
          }
          http.end();
        }

        // Resetear el sistema para el siguiente alumno

        particleSensor.setPulseAmplitudeRed(0); 
        particleSensor.setPulseAmplitudeIR(0);
        id_evaluacion_pendiente = "";
        esperandoPaciente = true; 
      }
    }
  }
}