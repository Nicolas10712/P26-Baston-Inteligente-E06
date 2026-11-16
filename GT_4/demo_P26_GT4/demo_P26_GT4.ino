/* =========================================================================
   P26 - Bastón Inteligente para Discapacidad Visual (Fase Integración GT3 + MQTT)
   Hardware: ESP32, HC-SR04, Motor Vibración, Zumbador, LED, GPS NEO-6M
   ========================================================================= */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include <TinyGPSPlus.h>

// 0. configuracion gps

// Instancia del decodificador
TinyGPSPlus gps;

// Variables de telemetría GPS
bool gpsFixValido = false;
double gpsLatitud = 0.0;
double gpsLongitud = 0.0;
float gpsVelocidadKmh = 0.0;
float gpsHdop = 99.0;
uint32_t gpsSatelites = 0;

 

// 1. Asignación de Pines 
const int PIN_TRIG   = 26;
const int PIN_ECHO   = 25;
const int PIN_MOTOR  = 33;
const int PIN_BUZZER = 32;
const int PIN_LED    = 27;
const int RXD2       = 16;
const int TXD2       = 17;

// 2. Máquinas de Estado 
enum class Estado {
  LIBRE,
  OBSTACULO_LEJOS,
  OBSTACULO_CERCA,
  ERROR_SENSOR
};

enum class FaseSensor {
  ESPERA_PERIODO,
  PULSO_TRIG_BAJO,
  PULSO_TRIG_ALTO,
  ESPERA_ECHO_ALTO,
  ESPERA_ECHO_BAJO
};

Estado estadoActual = Estado::LIBRE;
FaseSensor faseSensor = FaseSensor::ESPERA_PERIODO;

// 3. Parámetros y Constantes
// Sensor
const uint32_t PERIODO_SENSOR_US = 60000; // 60 ms entre disparos
const uint32_t TIMEOUT_ECO_US    = 12000; // 12 ms timeout
const float VELOCIDAD_SONIDO     = 0.0343;

const float D_CERCA_ON  = 20.0;
const float D_CERCA_OFF = 25.0;
const float D_LEJOS_ON  = 50.0;
const float D_LEJOS_OFF = 60.0;

// MQTT & WiFi
const uint32_t PERIODO_PUB_MS    = 10000;  // Publicar cada 10 s
const uint32_t REINTENTO_WIFI_MS = 15000;
const uint32_t ESPERA_INICIAL    = 2000;
const uint32_t ESPERA_MAXIMA     = 30000;

// 4. Variables Globales
// Variables Sensor
uint32_t marcaSensorUs = 0;
uint32_t subidaEchoUs = 0;
uint8_t fallasConsecutivas = 0;
uint8_t ecosValidosConsecutivos = 0;
float distanciaCm = 0.0;
bool nuevaLectura = false;
bool sinEcoAislado = false;

// Variables FSM y Seguridad
bool fallaSensor = false;
bool recuperacionValidada = false;
bool confirmacionManual = false;

// Variables Actuadores
uint32_t marcaPatronMs = 0;
bool estadoFase = false;       
uint8_t pasoError = 0;         

// Variables GPS
String tramaGPS = ""; 
String ultimaTramaGPS = "Sin señal GPS"; // Para enviar por MQTT

// Variables Red
WiFiClient   red;
PubSubClient mqtt(red);
String clientId, topicDatos, topicEstado, topicCmd;
uint32_t tPub = 0, tWiFi = 0, tReconexion = 0;
uint32_t esperaReconexion = ESPERA_INICIAL;

// 5. Funciones Auxiliares FSM
Estado estadoSegunDistancia(float d) {
  if (d <= D_CERCA_ON) return Estado::OBSTACULO_CERCA;
  if (d <= D_LEJOS_ON) return Estado::OBSTACULO_LEJOS;
  return Estado::LIBRE;
}

String estadoToString(Estado e) {
  switch(e) {
    case Estado::LIBRE: return "LIBRE";
    case Estado::OBSTACULO_LEJOS: return "OBSTACULO_LEJOS";
    case Estado::OBSTACULO_CERCA: return "OBSTACULO_CERCA";
    case Estado::ERROR_SENSOR: return "ERROR_SENSOR";
  }
  return "DESCONOCIDO";
}

void cambiarEstado(Estado nuevo) {
  estadoActual = nuevo;
  
  // Limpieza de actuadores al transicionar por seguridad
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_MOTOR, LOW);
  noTone(PIN_BUZZER);
  pasoError = 0; 
  
  Serial.print("--- CAMBIO DE ESTADO: ");
  Serial.println(estadoToString(estadoActual));
  
  if (estadoActual == Estado::ERROR_SENSOR) {
    Serial.println("[!] Escriba 'r' por Serial o envíe {\"comando\":\"reset\"} por MQTT para confirmar");
  }
}

// 6. Lógica de Red (WiFi / MQTT)
void recibirComando(char* topic, byte* payload, unsigned int largo) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, largo);
  if (error) {
    Serial.printf("[cmd] JSON invalido en %s: %s\n", topic, error.c_str());
    return;
  }
  
  Serial.printf("[cmd] recibido en %s\n", topic);
  
  // Si recibimos {"comando":"reset"} activamos confirmación manual remotamente
  if (doc.containsKey("comando") && doc["comando"] == "reset") {
    confirmacionManual = true;
    Serial.println("[!] Confirmacion manual recibida VIA MQTT.");
  }
}

void mantenerWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  uint32_t ahora = millis();
  if (ahora - tWiFi < REINTENTO_WIFI_MS) return;
  tWiFi = ahora;
  Serial.println("[wifi] sin red, reintentando...");
  WiFi.reconnect();
}

void mantenerMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  uint32_t ahora = millis();
  if (ahora - tReconexion < esperaReconexion) return;
  tReconexion = ahora;

  Serial.printf("[mqtt] conectando como %s ... ", clientId.c_str());
  
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS,
                   topicEstado.c_str(), 1, true, "offline")) {
    Serial.println("OK");
    mqtt.publish(topicEstado.c_str(), "online", true);   
    mqtt.subscribe(topicCmd.c_str(), 1);                 
    esperaReconexion = ESPERA_INICIAL;
  } else {
    Serial.printf("FALLO rc=%d, reintento en %u s\n",
                  mqtt.state(), (unsigned)(esperaReconexion / 1000));
    esperaReconexion = (esperaReconexion * 2 > ESPERA_MAXIMA) ? ESPERA_MAXIMA : esperaReconexion * 2;
  }
}

void publicarDatos() {
  if (!mqtt.connected()) return;

  JsonDocument doc;

  // Estado general y sensor
  doc["estado"]    = estadoToString(estadoActual);
  doc["sensor_ok"] = !fallaSensor ? 1 : 0;
  if (!fallaSensor) {
    doc["distancia_cm"] = roundf(distanciaCm * 10.0f) / 10.0f;
  }
  doc["rssi_dbm"] = WiFi.RSSI();

  // CORRECCIÓN 1: Telemetría GPS con campos PLANOS (sin anidar)
  doc["gps_fix"] = gpsFixValido ? 1 : 0;

  if (gpsFixValido) {
    doc["gps_lat"]   = serialized(String(gpsLatitud, 6));
    doc["gps_lng"]   = serialized(String(gpsLongitud, 6));
    doc["gps_speed"] = roundf(gpsVelocidadKmh * 10.0f) / 10.0f;
    doc["gps_hdop"]  = roundf(gpsHdop * 10.0f) / 10.0f;
    doc["gps_sats"]  = gpsSatelites;
  } else {
    // Si no hay enlace satelital aún, reportamos cuántos satélites ve en tránsito
    doc["gps_sats"] = gps.satellites.isValid() ? gps.satellites.value() : 0;
  }

  char payload[384]; 
  size_t n = serializeJson(doc, payload, sizeof(payload));

  // CORRECCIÓN 2: Publicar con QoS 1 y retained = true (cambiamos el 'false' final por 'true')
  if (mqtt.publish(topicDatos.c_str(), (const uint8_t*)payload, n, true)) {
    Serial.printf("[pub] %s -> %s\n", topicDatos.c_str(), payload);
  } else {
    Serial.println("[pub] ERROR publish() (buffer o sesion)");
  }
}

// 7. Lógica de Entrada (Datos Seriales)
void leerEntrada() {
  // 1. Confirmación manual vía Monitor Serie
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      confirmacionManual = true;
      Serial.println("[!] Confirmacion manual recibida (Serial).");
    }
  }

  // 2. Ingesta continua del GPS byte a byte
  while (Serial2.available() > 0) {
    char c = Serial2.read();
    gps.encode(c); // TinyGPSPlus parsea en tiempo real
  }

  // 3. Evaluación de calidad del Fix
  // Se exige: ubicación válida, dato fresco (< 2000 ms) y HDOP razonable (< 5.0)
  if (gps.location.isValid() && gps.location.age() < 2000) {
    gpsFixValido = true;
    gpsLatitud = gps.location.lat();
    gpsLongitud = gps.location.lng();
    gpsVelocidadKmh = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
    gpsHdop = gps.hdop.isValid() ? gps.hdop.hdop() : 99.0;
    gpsSatelites = gps.satellites.isValid() ? gps.satellites.value() : 0;
  } else {
    gpsFixValido = false;
  }
}
// 8. Lógica del Sensor Ultrasónico
void actualizarSensor() {
  uint32_t ahoraUs = micros();

  switch (faseSensor) {
    case FaseSensor::ESPERA_PERIODO:
      if (ahoraUs - marcaSensorUs >= PERIODO_SENSOR_US) {
        digitalWrite(PIN_TRIG, LOW);
        marcaSensorUs = ahoraUs;
        faseSensor = FaseSensor::PULSO_TRIG_BAJO;
      }
      break;

    case FaseSensor::PULSO_TRIG_BAJO:
      if (ahoraUs - marcaSensorUs >= 2) { 
        digitalWrite(PIN_TRIG, HIGH);
        marcaSensorUs = ahoraUs;
        faseSensor = FaseSensor::PULSO_TRIG_ALTO;
      }
      break;

    case FaseSensor::PULSO_TRIG_ALTO:
      if (ahoraUs - marcaSensorUs >= 10) { 
        digitalWrite(PIN_TRIG, LOW);
        marcaSensorUs = ahoraUs;
        faseSensor = FaseSensor::ESPERA_ECHO_ALTO;
      }
      break;

    case FaseSensor::ESPERA_ECHO_ALTO:
      if (digitalRead(PIN_ECHO) == HIGH) {
        subidaEchoUs = micros();
        faseSensor = FaseSensor::ESPERA_ECHO_BAJO;
      } 
      else if (ahoraUs - marcaSensorUs >= TIMEOUT_ECO_US) {
        sinEcoAislado = true;
        faseSensor = FaseSensor::ESPERA_PERIODO;
      }
      break;

    case FaseSensor::ESPERA_ECHO_BAJO:
      if (digitalRead(PIN_ECHO) == LOW) {
        uint32_t bajadaEchoUs = micros();
        uint32_t tiempoVuelo = bajadaEchoUs - subidaEchoUs;
        distanciaCm = (tiempoVuelo * VELOCIDAD_SONIDO) / 2.0;
        
        fallasConsecutivas = 0;
        fallaSensor = false;
        nuevaLectura = true;
        
        if (ecosValidosConsecutivos < 3) ecosValidosConsecutivos++;
        if (ecosValidosConsecutivos >= 3) recuperacionValidada = true;

        faseSensor = FaseSensor::ESPERA_PERIODO;
      } 
      else if (ahoraUs - subidaEchoUs >= TIMEOUT_ECO_US) {
        sinEcoAislado = true;
        ecosValidosConsecutivos = 0;
        recuperacionValidada = false;
        
        fallasConsecutivas++;
        if (fallasConsecutivas >= 5) {
          fallaSensor = true;
        }
        faseSensor = FaseSensor::ESPERA_PERIODO;
      }
      break;
  }
}

// 9. Máquina de Estados Principal
void ejecutarFSM() {
  if (fallaSensor && estadoActual != Estado::ERROR_SENSOR) {
    cambiarEstado(Estado::ERROR_SENSOR);
    return;
  }

  switch (estadoActual) {
    case Estado::LIBRE:
      if (nuevaLectura) {
        if (distanciaCm <= D_CERCA_ON) {
          cambiarEstado(Estado::OBSTACULO_CERCA);
        } else if (distanciaCm <= D_LEJOS_ON) {
          cambiarEstado(Estado::OBSTACULO_LEJOS);
        }
      }
      break;

    case Estado::OBSTACULO_LEJOS:
      if (sinEcoAislado || (nuevaLectura && distanciaCm >= D_LEJOS_OFF)) {
        cambiarEstado(Estado::LIBRE);
      } else if (nuevaLectura && distanciaCm <= D_CERCA_ON) {
        cambiarEstado(Estado::OBSTACULO_CERCA);
      }
      break;

    case Estado::OBSTACULO_CERCA:
      if (sinEcoAislado) {
        cambiarEstado(Estado::LIBRE);
      } else if (nuevaLectura && distanciaCm >= D_CERCA_OFF) {
        cambiarEstado(Estado::OBSTACULO_LEJOS);
      }
      break;

    case Estado::ERROR_SENSOR:
      if (recuperacionValidada && confirmacionManual) {
        confirmacionManual = false;
        fallaSensor = false;
        fallasConsecutivas = 0;
        cambiarEstado(estadoSegunDistancia(distanciaCm));
      }
      break;
  }
  
  nuevaLectura = false;
  sinEcoAislado = false;
}

// 10. Lógica de Actuadores
void actualizarActuadores() {
  uint32_t ahoraMs = millis();

  switch (estadoActual) {
    case Estado::LIBRE:
      break;

    case Estado::OBSTACULO_LEJOS:
      if (ahoraMs - marcaPatronMs >= 250) {
        marcaPatronMs = ahoraMs;
        estadoFase = !estadoFase;
        digitalWrite(PIN_LED, estadoFase ? HIGH : LOW);
        digitalWrite(PIN_MOTOR, estadoFase ? HIGH : LOW);
      }
      break;

    case Estado::OBSTACULO_CERCA:
      digitalWrite(PIN_LED, HIGH);
      digitalWrite(PIN_MOTOR, HIGH);
      
      if (ahoraMs - marcaPatronMs >= 100) { 
        marcaPatronMs = ahoraMs;
        tone(PIN_BUZZER, 1000); 
      }
      break;

    case Estado::ERROR_SENSOR:
      digitalWrite(PIN_MOTOR, LOW);
      
      if (pasoError == 0 && ahoraMs - marcaPatronMs >= 700) {
        digitalWrite(PIN_LED, HIGH); tone(PIN_BUZZER, 2000);
        marcaPatronMs = ahoraMs; pasoError = 1;
      } 
      else if (pasoError == 1 && ahoraMs - marcaPatronMs >= 100) {
        digitalWrite(PIN_LED, LOW); noTone(PIN_BUZZER);
        marcaPatronMs = ahoraMs; pasoError = 2;
      }
      else if (pasoError == 2 && ahoraMs - marcaPatronMs >= 100) {
        digitalWrite(PIN_LED, HIGH); tone(PIN_BUZZER, 2000);
        marcaPatronMs = ahoraMs; pasoError = 3;
      }
      else if (pasoError == 3 && ahoraMs - marcaPatronMs >= 100) {
        digitalWrite(PIN_LED, LOW); noTone(PIN_BUZZER);
        marcaPatronMs = ahoraMs; pasoError = 0;
      }
      break;
  }
}

// 11. Configuración Inicial y Bucle Principal
void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_MOTOR, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);

  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_MOTOR, LOW);
  noTone(PIN_BUZZER);

  delay(500);
  Serial.println("\n=======================================================");
  Serial.println("P26 - Baston Inteligente + MQTT Iniciado (Integracion)");
  Serial.println("=======================================================");

  // CONFIGURACIÓN RED
  clientId    = String(MQTT_USER) + "-" + NODO;
  topicDatos  = String("curso/") + MQTT_USER + "/" + PROYECTO + "/" + NODO;
  topicEstado = topicDatos + "/estado";
  topicCmd    = topicDatos + "/cmd";
  Serial.printf("[id] Datos : %s\n", topicDatos.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  
  uint32_t inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[wifi] IP = ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[wifi] sin red todavia; el nodo seguira funcionando y reintentara");
  }
  tWiFi = millis();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(recibirComando);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(3);

  cambiarEstado(Estado::LIBRE);
}

void loop() {
  // 1. Tareas de red y conectividad (No bloqueantes)
  mantenerWiFi();
  mantenerMQTT();
  mqtt.loop(); 

  // 2. Submáquinas de control del bastón
  leerEntrada();          
  actualizarSensor();     
  ejecutarFSM();          
  actualizarActuadores(); 

  // 3. Temporizador de publicación MQTT
  uint32_t ahora = millis();
  if (ahora - tPub >= PERIODO_PUB_MS) { 
    tPub = ahora;     
    publicarDatos(); 
  }


// 4. Diagnóstico de conexión física del GPS (cada 5 segundos)
  static uint32_t timerDebugGPS = 0;
  if (millis() - timerDebugGPS > 5000) {
    timerDebugGPS = millis();
    if (gps.charsProcessed() < 10) {
      Serial.println("[!] ERROR GPS: No llega comunicación. Revisa cables RX/TX y asegúrate que sea 9600 baudios.");
    }
  }

  // 5. Monitor Serie: Debug local del GPS (cada 2 segundos)
  static uint32_t timerImpresionGPS = 0;
  if (millis() - timerImpresionGPS > 2000) {
    timerImpresionGPS = millis();
    
    Serial.print("[GPS DEBUG] Satelites visibles: ");
    Serial.print(gps.satellites.isValid() ? gps.satellites.value() : 0);
    
    if (gpsFixValido) {
      Serial.print(" | FIX OK | Lat: ");
      Serial.print(gpsLatitud, 6);
      Serial.print(" Lon: ");
      Serial.print(gpsLongitud, 6);
      Serial.print(" | Vel: ");
      Serial.print(gpsVelocidadKmh);
      Serial.println(" km/h");
    } else {
      Serial.println(" | Buscando senal satelital (FIX = 0)...");
    }
  }
}