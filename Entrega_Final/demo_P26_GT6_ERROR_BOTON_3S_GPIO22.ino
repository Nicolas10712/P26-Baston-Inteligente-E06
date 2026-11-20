 /* =========================================================================
   P26 - Bastón Inteligente para Discapacidad Visual
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
const int PIN_LED    = 27;  // LED principal de avisos del baston
const int PIN_BOTON  = 14;  // Rearme fisico de ERROR_SENSOR: SE CONSERVA
const int PIN_LED_REMOTO = 22; // LED amarillo remoto conectado fisicamente a GPIO22
const int RXD2       = 16;
const int TXD2       = 17;

// 2. Máquinas de Estado 
enum class Estado {
  LIBRE,
  OBSTACULO_LEJOS,
  OBSTACULO_CERCA,
  ERROR_SENSOR,
  ACTUANDO_REMOTO,  // Indice 4: GT6 (se conserva)
  SIN_ECO           // Indice 5: medicion desconocida, distinto de fallo electrico
};

// El callback MQTT registra el comando. Solo ejecutarFSM() decide y actua.
enum class CmdRemoto : uint8_t { NINGUNO, ON, OFF };

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
const uint32_t TIMEOUT_ECO_US    = 25000; // 25 ms: ~4.29 m teoricos para retorno de eco
const uint8_t MAX_FALLAS_ECO      = 5;     // Tras 5 intentos sin eco: SIN_ECO (no equivale a fallo)
const uint8_t ECOS_RECUPERACION   = 3;     // Recuperacion: 3 ecos consecutivos + rearme manual
const uint32_t MAX_EDAD_LECTURA_MS = 250; // No transmitir como vigente una lectura antigua
const uint32_t DEBOUNCE_BOTON_MS   = 50; // 50 ms para filtrar rebote mecanico del boton
// PRUEBA DIAGNOSTICA: mantener GPIO14 presionado durante 3 s para simular ERROR_SENSOR.
// Se puede deshabilitar sin tocar el circuito dejando esta constante en false.
const uint32_t PULSACION_LARGA_ERROR_MS = 3000;
const bool DIAGNOSTICO_ERROR_POR_BOTON = true;
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
const uint32_t T_MAX_REMOTO_MS   = 60000; // Sin renovacion de ON: apagado maximo 60 s

// 4. Variables Globales
// Variables Sensor
uint32_t marcaSensorUs = 0;
uint32_t subidaEchoUs = 0;
uint8_t fallasConsecutivas = 0;
uint8_t fallasEchoAltoConsecutivas = 0; // ECHO permanece alto entre disparos: posible fallo electrico
uint8_t ecosValidosConsecutivos = 0;
float distanciaCm = 0.0;
bool nuevaLectura = false;
bool hayLecturaValida = false;       // No existe distancia valida hasta el primer eco completo
uint32_t marcaLecturaValidaMs = 0;  // Edad de la ultima lectura util

// Variables FSM y Seguridad
bool fallaSensor = false;
bool recuperacionValidada = false;
bool confirmacionManual = false;
CmdRemoto cmdPendiente = CmdRemoto::NINGUNO;
uint32_t marcaActuacionRemotaMs = 0;

// Variables Antirrebote del Botón Físico
bool estadoBotonEstable = HIGH;      // En INPUT_PULLUP, reposo es HIGH
bool ultimaLecturaBoton = HIGH;
uint32_t ultimoCambioBotonMs = 0;
// Diferenciar pulsacion larga (diagnostico) de nueva pulsacion corta (rearme).
uint32_t inicioPulsacionBotonMs = 0;
bool pulsacionComenzoEnError = false;
bool rearmeListoAlPulsar = false; // Exigir 3 ecos antes de la nueva pulsacion.
bool pulsacionLargaResuelta = false;

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
    case Estado::ACTUANDO_REMOTO: return "ACTUANDO_REMOTO";
    case Estado::SIN_ECO: return "SIN_ECO";
  }
  return "DESCONOCIDO";
}

void cambiarEstado(Estado nuevo) {
  estadoActual = nuevo;
  
  // Limpieza de actuadores al transicionar por seguridad
  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_LED_REMOTO, LOW);  // Cada transicion cancela salidas remotas previas
  digitalWrite(PIN_MOTOR, LOW);
  noTone(PIN_BUZZER);
  if (nuevo != Estado::ACTUANDO_REMOTO) marcaActuacionRemotaMs = 0;
  pasoError = 0; 
  
  Serial.print("--- CAMBIO DE ESTADO: ");
  Serial.println(estadoToString(estadoActual));
  
  if (estadoActual == Estado::SIN_ECO) {
    ecosValidosConsecutivos = 0;
    Serial.println("[sensor] Sin eco sostenido: alcance/reflectancia desconocidos. No se asume camino libre.");
  }

  if (estadoActual == Estado::ERROR_SENSOR) {
    // El rearme nunca puede heredarse de un comando anterior a la falla.
    confirmacionManual = false;
    ecosValidosConsecutivos = 0;
    recuperacionValidada = false;
    Serial.println("[!] ERROR_SENSOR: 3 ecos validos consecutivos y luego confirmacion manual (Boton, 'r' o MQTT reset).");
  }
}

// 6. Lógica de Red (WiFi / MQTT)
void recibirComando(char* topic, byte* payload, unsigned int largo) {
  // Ignorar cualquier mensaje que no provenga del subtopico de comando propio.
  if (strcmp(topic, topicCmd.c_str()) != 0) return;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload, largo);
  if (error) {
    Serial.printf("[cmd] JSON invalido: %s\n", error.c_str());
    return;
  }

  const char* comando = doc["cmd"] | "";
  if (strcmp(comando, "ON") == 0) {
    cmdPendiente = CmdRemoto::ON;
    Serial.println("[cmd recibido] ON");
  } else if (strcmp(comando, "OFF") == 0) {
    cmdPendiente = CmdRemoto::OFF;
    Serial.println("[cmd recibido] OFF");
  } else {
    // Se conserva el reset existente de NP2 para recuperar ERROR_SENSOR.
    const char* rearme = doc["comando"] | "";
    if (strcmp(rearme, "reset") == 0) {
      if (estadoActual == Estado::ERROR_SENSOR) {
        confirmacionManual = true;
        Serial.println("[cmd recibido] reset: confirmar rearme tras 3 ecos validos");
      } else {
        Serial.println("[cmd] reset ignorado: no estamos en ERROR_SENSOR");
      }
    } else {
      Serial.println("[cmd] comando no reconocido (esperado ON, OFF o reset)");
    }
  }
  // El callback NO enciende GPIO ni cambia de Estado: decide la FSM en loop().
}

void mantenerWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  
  uint32_t ahora = millis();
  // Reintenta cada 30 segundos para no saturar el procesador
  if (ahora - tWiFi < 30000) return; 
  tWiFi = ahora;
  
  Serial.println("[wifi] Buscando red en segundo plano (sin bloquear baston)...");
  
  // Es mas seguro desconectar la radio y volver a iniciarla
  WiFi.disconnect(); 
  WiFi.begin(WIFI_SSID, WIFI_PASS);
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

  // Estado general numérico (Evita el conflicto de tipos en InfluxDB)
  // 0: LIBRE | 1: OBSTACULO_LEJOS | 2: OBSTACULO_CERCA | 3: ERROR_SENSOR
  // 4: ACTUANDO_REMOTO | 5: SIN_ECO. No se renumeran estados previos.
  doc["estado_num"] = static_cast<int>(estadoActual);
  doc["remoto_activo"] = (estadoActual == Estado::ACTUANDO_REMOTO) ? 1 : 0;
  doc["sin_eco"] = (estadoActual == Estado::SIN_ECO || fallasConsecutivas > 0) ? 1 : 0;
  
  // Un dato se informa como valido solo si es reciente y no hubo eco fallido
  // desde la ultima medicion. No publicar el 0 inicial ni distancias obsoletas.
  bool distanciaConfiable = hayLecturaValida && !fallaSensor &&
                            fallasConsecutivas == 0 &&
                            (millis() - marcaLecturaValidaMs <= MAX_EDAD_LECTURA_MS);
  doc["sensor_ok"] = distanciaConfiable ? 1 : 0;
  if (distanciaConfiable) {
    doc["distancia_cm"] = roundf(distanciaCm * 10.0f) / 10.0f;
  }
  doc["rssi_dbm"] = WiFi.RSSI();

  // Telemetría GPS con campos PLANOS (sin anidar)
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

  // PubSubClient::publish publica con QoS 0 y retained=true.
  // La suscripcion a /cmd si usa QoS 1 (mqtt.subscribe).
  if (mqtt.publish(topicDatos.c_str(), (const uint8_t*)payload, n, true)) {
    Serial.printf("[pub] %s -> %s\n", topicDatos.c_str(), payload);
  } else {
    Serial.println("[pub] ERROR publish() (buffer o sesion)");
  }
}

// 7. Lógica de Entrada (Datos Seriales)
void leerEntrada() {
  // 1.A Confirmación manual vía Monitor Serie
  if (Serial.available() > 0) {
    char c = Serial.read();
    if (c == 'r' || c == 'R') {
      if (estadoActual == Estado::ERROR_SENSOR) {
        confirmacionManual = true;
        Serial.println("[!] Confirmacion de rearme recibida (Serial).");
      } else {
        Serial.println("[serial] r ignorada: el sistema no esta en ERROR_SENSOR.");
      }
    }
  }

  // 1.B Boton fisico GPIO14: antirrebote, pulsacion larga y rearme separado.
  // Una pulsacion mantenida durante 3 s, iniciada fuera del error, puede
  // simular ERROR_SENSOR. Esa MISMA pulsacion nunca confirma el rearme.
  bool lecturaActualBoton = digitalRead(PIN_BOTON);
  uint32_t ahoraMs = millis();

  if (lecturaActualBoton != ultimaLecturaBoton) {
    ultimoCambioBotonMs = ahoraMs; // Reiniciar temporizador ante rebote.
  }
  ultimaLecturaBoton = lecturaActualBoton;

  if ((uint32_t)(ahoraMs - ultimoCambioBotonMs) >= DEBOUNCE_BOTON_MS &&
      lecturaActualBoton != estadoBotonEstable) {
    estadoBotonEstable = lecturaActualBoton;

    if (estadoBotonEstable == LOW) {
      // Registrar el estado al COMENZAR la pulsacion.
      inicioPulsacionBotonMs = ahoraMs;
      pulsacionComenzoEnError = (estadoActual == Estado::ERROR_SENSOR);
      rearmeListoAlPulsar = pulsacionComenzoEnError && recuperacionValidada;
      pulsacionLargaResuelta = false;
    } else {
      // Procesar la pulsacion corta al SOLTAR, no al comenzar.
      // Solo rearma si se PRESIONO cuando ya existia ERROR_SENSOR.
      if (pulsacionComenzoEnError && estadoActual == Estado::ERROR_SENSOR) {
        if ((uint32_t)(ahoraMs - inicioPulsacionBotonMs) >= PULSACION_LARGA_ERROR_MS) {
          Serial.println("[boton] Rearme ignorado: usar una pulsacion corta en ERROR_SENSOR.");
        } else if (rearmeListoAlPulsar && recuperacionValidada) {
          confirmacionManual = true;
          Serial.println("[!] Confirmacion de rearme recibida (Boton fisico).");
        } else {
          Serial.println("[boton] Rearme no autorizado: esperar 3 ecos validos ANTES de pulsar.");
        }
      } else if (!pulsacionLargaResuelta) {
        Serial.println("[boton] Pulsacion corta ignorada: no se inicio en ERROR_SENSOR.");
      }
      pulsacionComenzoEnError = false;
      rearmeListoAlPulsar = false;
      pulsacionLargaResuelta = false;
    }
  }

  // Pulsacion larga sin delay(): ejecutarla UNA sola vez por pulsacion.
  if (estadoBotonEstable == LOW && !pulsacionComenzoEnError &&
      !pulsacionLargaResuelta && DIAGNOSTICO_ERROR_POR_BOTON &&
      (uint32_t)(ahoraMs - inicioPulsacionBotonMs) >= PULSACION_LARGA_ERROR_MS) {
    pulsacionLargaResuelta = true;
    // Igual que la prueba anterior con 'f': exigir eco reciente y confiable.
    bool lecturaParaPrueba = hayLecturaValida && !fallaSensor &&
                             fallasConsecutivas == 0 &&
                             (uint32_t)(ahoraMs - marcaLecturaValidaMs) <= MAX_EDAD_LECTURA_MS;
    bool estadoAdmitido = (estadoActual == Estado::LIBRE ||
                           estadoActual == Estado::OBSTACULO_LEJOS ||
                           estadoActual == Estado::OBSTACULO_CERCA ||
                           estadoActual == Estado::ACTUANDO_REMOTO);
    if (estadoAdmitido && lecturaParaPrueba) {
      fallaSensor = true; // ejecutarFSM() hace la transicion a ERROR_SENSOR.
      Serial.println("[DIAG BOTON] Pulsacion larga >= 3 s: falla SIMULADA solicitada.");
      Serial.println("[DIAG BOTON] Soltar; esperar 3 ecos validos y pulsar de nuevo para rearmar.");
    } else {
      Serial.println("[DIAG BOTON] Ignorada: sensor sin lectura reciente o estado no permitido.");
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
// Ningun eco: no se puede saber si hay distancia grande, mala reflexion o
// desconexion. Se informa SIN_ECO, nunca "distancia infinita" ni LIBRE segura.
void registrarEcoInvalido() {
  nuevaLectura = false;
  ecosValidosConsecutivos = 0;
  recuperacionValidada = false;
  if (fallasConsecutivas < MAX_FALLAS_ECO) ++fallasConsecutivas;
}

// Falla identificable: ECHO permanece alto AL COMENZAR OTRO DISPARO (>60 ms).
// Es diferente del pulso largo de ~38 ms que algunos HC-SR04 entregan sin retorno.
// Requiere 5 ciclos para no considerar un transitorio como averia.
void registrarEchoBloqueado() {
  registrarEcoInvalido();
  if (fallasEchoAltoConsecutivas < MAX_FALLAS_ECO) ++fallasEchoAltoConsecutivas;
  if (fallasEchoAltoConsecutivas >= MAX_FALLAS_ECO && !fallaSensor) {
    fallaSensor = true;
    Serial.println("[sensor] ECHO alto en reposo durante 5 ciclos: ERROR_SENSOR.");
  }
}

void registrarEcoValido(uint32_t tiempoVueloUs) {
  distanciaCm = (tiempoVueloUs * VELOCIDAD_SONIDO) / 2.0f;
  hayLecturaValida = true;
  marcaLecturaValidaMs = millis();
  nuevaLectura = true;
  fallasConsecutivas = 0;
  fallasEchoAltoConsecutivas = 0;

  // La bandera de error se mantiene enclavada mientras no haya rearme.
  if (fallaSensor) {
    if (ecosValidosConsecutivos < ECOS_RECUPERACION) ++ecosValidosConsecutivos;
    recuperacionValidada = (ecosValidosConsecutivos >= ECOS_RECUPERACION);
  } else if (estadoActual == Estado::SIN_ECO) {
    if (ecosValidosConsecutivos < ECOS_RECUPERACION) ++ecosValidosConsecutivos;
    recuperacionValidada = (ecosValidosConsecutivos >= ECOS_RECUPERACION);
  } else {
    ecosValidosConsecutivos = 0;
    recuperacionValidada = false;
  }
}

void actualizarSensor() {
  uint32_t ahoraUs = micros();

  switch (faseSensor) {
    case FaseSensor::ESPERA_PERIODO:
      if (ahoraUs - marcaSensorUs >= PERIODO_SENSOR_US) {
        marcaSensorUs = ahoraUs;
        if (digitalRead(PIN_ECHO) == HIGH) {
          // No disparar nuevamente con ECHO atascado en alto.
          registrarEchoBloqueado();
        } else {
          fallasEchoAltoConsecutivas = 0;
          digitalWrite(PIN_TRIG, LOW);
          faseSensor = FaseSensor::PULSO_TRIG_BAJO;
        }
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
        registrarEcoInvalido(); // ECHO nunca subio: contar tambien esta falla.
        faseSensor = FaseSensor::ESPERA_PERIODO;
      }
      break;

    case FaseSensor::ESPERA_ECHO_BAJO:
      if (digitalRead(PIN_ECHO) == LOW) {
        uint32_t bajadaEchoUs = micros();
        uint32_t tiempoVuelo = bajadaEchoUs - subidaEchoUs;
        registrarEcoValido(tiempoVuelo);
        faseSensor = FaseSensor::ESPERA_PERIODO;
      } 
      else if (ahoraUs - subidaEchoUs >= TIMEOUT_ECO_US) {
        registrarEcoInvalido(); // ECHO quedo alto: mismo contador de fallas.
        faseSensor = FaseSensor::ESPERA_PERIODO;
      }
      break;
  }
}

// 9. Máquina de Estados Principal
void ejecutarFSM() {
  // Cada comando se consume como un evento, jamas como accion directa desde MQTT.
  CmdRemoto cmd = cmdPendiente;
  cmdPendiente = CmdRemoto::NINGUNO;

  // ERROR_SENSOR tiene prioridad absoluta sobre un comando remoto.
  if (fallaSensor && estadoActual != Estado::ERROR_SENSOR) {
    cambiarEstado(Estado::ERROR_SENSOR);
    nuevaLectura = false;
    return;
  }

  // Cinco mediciones sin retorno se clasifican SIN_ECO, sin suponer distancia.
  // La bandera fallaSensor (ECHO persistentemente alto) sigue teniendo prioridad.
  if (fallasConsecutivas >= MAX_FALLAS_ECO &&
      estadoActual != Estado::SIN_ECO && estadoActual != Estado::ERROR_SENSOR) {
    cambiarEstado(Estado::SIN_ECO);
    nuevaLectura = false;
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
      // Permitir ON solamente desde LIBRE y con distancia estable >= 60 cm.
      if (estadoActual == Estado::LIBRE && cmd == CmdRemoto::ON) {
        bool lecturaSegura = hayLecturaValida && !fallaSensor &&
                             fallasConsecutivas == 0 &&
                             (millis() - marcaLecturaValidaMs <= MAX_EDAD_LECTURA_MS) &&
                             distanciaCm >= D_LEJOS_OFF;
        if (lecturaSegura) {
          cambiarEstado(Estado::ACTUANDO_REMOTO);
          marcaActuacionRemotaMs = millis();
          Serial.println("[fsm] ON aceptado: indicador remoto activo");
        } else {
          Serial.println("[fsm] ON ignorado: distancia/sensor no seguros");
        }
      }
      break;

    case Estado::OBSTACULO_LEJOS:
      if (nuevaLectura && distanciaCm >= D_LEJOS_OFF) {
        cambiarEstado(Estado::LIBRE);
      } else if (nuevaLectura && distanciaCm <= D_CERCA_ON) {
        cambiarEstado(Estado::OBSTACULO_CERCA);
      }
      break;

    case Estado::OBSTACULO_CERCA:
      if (nuevaLectura && distanciaCm >= D_CERCA_OFF) {
        cambiarEstado(Estado::OBSTACULO_LEJOS);
      }
      break;

    case Estado::SIN_ECO:
      // No activar ON mientras la distancia no sea confiable.
      if (cmd == CmdRemoto::ON) {
        Serial.println("[fsm] ON ignorado: no hay medicion confiable");
      }
      // Tres ecos consecutivos restauran la FSM segun la distancia medida.
      if (nuevaLectura && recuperacionValidada) {
        recuperacionValidada = false;
        ecosValidosConsecutivos = 0;
        cambiarEstado(estadoSegunDistancia(distanciaCm));
      }
      break;

    case Estado::ERROR_SENSOR:
      if (recuperacionValidada && confirmacionManual) {
        confirmacionManual = false;
        fallaSensor = false;
        fallasConsecutivas = 0;
        ecosValidosConsecutivos = 0;
        recuperacionValidada = false;
        cambiarEstado(estadoSegunDistancia(distanciaCm));
      }
      break;

    case Estado::ACTUANDO_REMOTO:
      // La deteccion local cancela el remoto y activa el estado correspondiente.
      if (nuevaLectura && distanciaCm <= D_LEJOS_ON) {
        cambiarEstado(estadoSegunDistancia(distanciaCm));
        Serial.println("[fsm] ON cancelado: obstaculo local prioritario");
      } else if (!hayLecturaValida || fallasConsecutivas > 0 ||
                 millis() - marcaLecturaValidaMs > MAX_EDAD_LECTURA_MS) {
        cambiarEstado(Estado::LIBRE);
        Serial.println("[fsm] ON cancelado: sensor sin lectura confiable");
      } else if (cmd == CmdRemoto::OFF) {
        cambiarEstado(Estado::LIBRE);
        Serial.println("[fsm] OFF recibido: indicador remoto apagado");
      } else if (millis() - marcaActuacionRemotaMs >= T_MAX_REMOTO_MS) {
        cambiarEstado(Estado::LIBRE);
        Serial.println("[fsm] 60 s sin renovacion: indicador remoto apagado");
      } else if (cmd == CmdRemoto::ON) {
        marcaActuacionRemotaMs = millis();
        Serial.println("[fsm] ON renovado: temporizador actualizado");
      }
      break;
  }

  nuevaLectura = false;
}

// 10. Lógica de Actuadores
void actualizarActuadores() {
  uint32_t ahoraMs = millis();

  switch (estadoActual) {
    case Estado::LIBRE:
      break;

    case Estado::ACTUANDO_REMOTO:
      // Indicador no critico: nunca sustituye vibrador, LED ni zumbador locales.
      digitalWrite(PIN_LED_REMOTO, HIGH);
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

    case Estado::SIN_ECO:
      // Advertencia local no bloqueante: sensor sin distancia fiable.
      // No informar LIBRE ni permitir LED remoto mientras dure esta condicion.
      digitalWrite(PIN_MOTOR, LOW);
      if (ahoraMs - marcaPatronMs >= 1200) {
        marcaPatronMs = ahoraMs;
        digitalWrite(PIN_LED, HIGH);
        tone(PIN_BUZZER, 700);
      } else if (ahoraMs - marcaPatronMs >= 150) {
        digitalWrite(PIN_LED, LOW);
        noTone(PIN_BUZZER);
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
  pinMode(PIN_BOTON, INPUT_PULLUP); // Mantener el boton de rearme ya instalado
  pinMode(PIN_LED_REMOTO, OUTPUT);

  digitalWrite(PIN_LED, LOW);
  digitalWrite(PIN_LED_REMOTO, LOW);
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
  WiFi.setAutoReconnect(true); // El ESP32 intentará reconectar automáticamente
  WiFi.setSleep(false);
  
  Serial.println("[wifi] Iniciando busqueda de red...");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  tWiFi = millis();

  // Configuramos el cliente MQTT
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(recibirComando);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(15);
  mqtt.setSocketTimeout(3); // Si el broker falla, solo espera 3 segundos máximo

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