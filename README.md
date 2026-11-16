P26 — Bastón Inteligente | Equipo E06
Proyecto de Fundamentos de IoT de la Universidad Autónoma de Chile. Prototipo educativo de bastón de asistencia que utiliza un ESP32 y un sensor ultrasónico HC-SR04 para detectar obstáculos, generar avisos locales y transmitir telemetría mediante MQTT. El proyecto incorpora flujos de Node-RED, persistencia en InfluxDB y visualización en Grafana.
Alcance: prototipo académico en evaluación. No está certificado ni sustituye un bastón blanco, un perro guía u otra ayuda de movilidad. El HC-SR04 tiene limitaciones de cobertura, superficie y ángulo de reflexión.

Implementación actual
El archivo de referencia para la versión de seis estados es [`Entrega_Final/demo_P26_GT6_SIN_ECO_EXPERIMENTAL.ino`](Entrega_Final/demo_P26_GT6_SIN_ECO_EXPERIMENTAL.ino). Se conserva su nombre para identificar la versión efectivamente archivada. El firmware de cinco estados disponible en GT_6/firmware/ corresponde a una etapa previa, no a la versión de seis estados.
Código	Estado	Descripción
0	LIBRE	Distancia válida compatible con camino libre según los umbrales
1	OBSTACULO_LEJOS	Obstáculo intermedio, aviso local
2	OBSTACULO_CERCA	Obstáculo cercano, aviso prioritario
3	ERROR_SENSOR	Condición de falla identificada, con recuperación controlada
4	ACTUANDO_REMOTO	Indicador remoto activado por comando MQTT, sujeto a condiciones seguras
5	SIN_ECO	Sin medición ultrasónica confiable; no implica necesariamente sensor averiado


Los umbrales del firmware de seis estados son 20/25 cm (entrada/salida de CERCA) y 50/60 cm (entrada/salida de LEJOS). La lógica remota admite ON únicamente desde LIBRE con una lectura válida y segura; OFF, el temporizador de 60 segundos o una condición local prioritaria cancelan la actuación remota.
Hardware y pines declarados en el firmware
Dispositivo / señal	GPIO ESP32
HC-SR04 TRIG	26
HC-SR04 ECHO	25
LED local	27
Zumbador	32
Salida de control del motor de vibración	33
Botón de rearme	14
LED remoto	23
GPS UART RX / TX	16 / 17


Precauciones: ECHO del HC-SR04 debe estar acondicionado a los niveles de 3,3 V del ESP32. El motor requiere una etapa de potencia y protección adecuadas; no debe conectarse directamente a un GPIO. La asignación de pines refleja el código, no equivale a certificar el montaje físico.
Software y comunicaciones
- Firmware: Arduino IDE o herramienta compatible con ESP32; bibliotecas PubSubClient, ArduinoJson y TinyGPSPlus, además de las bibliotecas del núcleo ESP32.
- MQTT: telemetría en curso/E06/P26/Baston_E06 y comandos en curso/E06/P26/Baston_E06/cmd.
- Control remoto: payload JSON {"cmd":"ON"} o {"cmd":"OFF"}. Para solicitar recuperación de ERROR_SENSOR, el firmware también reconoce {"comando":"reset"} bajo las condiciones programadas.
- Node-RED: el flujo básico archivado está en [`GT_6/nodered/flujo.json`](GT_6/nodered/flujo.json). Incluye recepción de telemetría y comandos ON/OFF; no incluye la exportación posterior del Dashboard 2.0.
- Grafana: el dashboard archivado está en [`GT_6/grafana/dashboard.json`](GT_6/grafana/dashboard.json). Esta exportación representa la etapa previa y sus etiquetas de estados contemplan 0–4; queda por incorporar la etiqueta del estado 5.
- InfluxDB: consultas Flux del trabajo GT5 en [`GT_5/`](GT_5/).
Configuración local del firmware
1. Copiar [`Entrega_Final/config.example.h`](Entrega_Final/config.example.h) como config.h en la misma carpeta que el .ino que se compilará.
2. Completar localmente Wi-Fi, host y credenciales MQTT.
3. Abrir el .ino con Arduino IDE, instalar las bibliotecas necesarias y seleccionar la placa ESP32 adecuada.
4. Compilar y cargar en el ESP32. Las pruebas físicas deben realizarse con el circuito conectado de manera segura.
El archivo config.h contiene credenciales personales y no debe subirse a GitHub. El repositorio solo debe conservar config.example.h, con valores de ejemplo.
Organización del repositorio
Carpeta	Contenido
[`GT_1/`](GT_1/)	Ejercicio de sensado y calibración, documentación y material Wokwi
[`GT_2/`](GT_2/)	Resultados y grafo de la etapa inicial
[`GT_3/`](GT_3/)	Informe técnico del HC-SR04 y máquina de estados
[`GT_4/`](GT_4/)	Integración MQTT y firmware de esa etapa
[`GT_5/`](GT_5/)	Esquema de ingesta y consultas Flux
[`GT_6/`](GT_6/)	Firmware histórico, exportaciones, datos y capturas
[`Entrega_Final/`](Entrega_Final/)	Firmware experimental de seis estados y plantilla de configuración sin claves


Verificaciones y límites de la evidencia
En pruebas del prototipo se han observado los estados locales LIBRE, OBSTACULO_LEJOS y OBSTACULO_CERCA, la recepción de órdenes MQTT para el LED remoto y su cancelación por obstáculo o temporizador. Se dispone de una versión experimental que diferencia SIN_ECO de ERROR_SENSOR.

Aspectos aún no acreditados completamente por este repositorio: recuperación física controlada de ERROR_SENSOR, posicionamiento GPS con FIX válido y cierre del ensayo experimental de sigma de GT1. El dashboard interactivo más reciente de Node-RED requiere una nueva exportación para poder reproducirse desde GitHub. No se presentan esas pruebas como resultados concluidos.
Uso académico
Material desarrollado para el proyecto P26, equipo E06. Los resultados se refieren al prototipo y a las condiciones de ensayo documentadas, no a un dispositivo de seguridad certificado.
