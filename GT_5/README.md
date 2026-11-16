\### Esquema de Datos e Ingesta (Telegraf + InfluxDB)



\* \*\*Ruta de ingesta:\*\* Los datos viajan desde el `ESP32` mediante publicación MQTT hacia el broker `Mosquitto`. Luego, `Telegraf` (suscrito al tópico global) captura el payload, parsea los niveles del tópico como tags y lo persiste en la base de datos temporal `InfluxDB`. Grafana lee desde allí mediante consultas Flux.

\* \*\*Organización:\*\* `curso-iot`

\* \*\*Bucket común:\*\* `mediciones`

\* \*\*Measurement:\*\* `mediciones`

\* \*\*Tags estructurales propios:\*\* `equipo: E06`, `proyecto: P26`, `nodo: Baston\_E06`.

\* \*\*Fields del proyecto (datos reales):\*\*

&#x20; \* `distancia\_cm` (Float): Distancia calculada por el sensor HC-SR04.

&#x20; \* `estado\_num` (Int): Estado de la FSM (0=Libre, 1=Lejos, 2=Cerca, 3=Error).

&#x20; \* `sensor\_ok` (Int): Bandera de salud del sensor (1=Operativo, 0=Falla).

&#x20; \* `gps\_lat` / `gps\_lng` (String): Coordenadas geográficas.

&#x20; \* `gps\_speed` / `gps\_hdop` / `gps\_sats` (Float/Int): Telemetría satelital.

