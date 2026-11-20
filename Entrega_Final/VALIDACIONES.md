# Validaciones — P26 E06, implementación final

**Versión actual probada por el equipo:** `demo_P26_GT6_ERROR_BOTON_3S_GPIO22.ino`.

| ID | Prueba | Resultado registrado / comunicado |
| --- | --- | --- |
| V01 | LIBRE, OBSTACULO_LEJOS y OBSTACULO_CERCA | Validación funcional comunicada por el equipo |
| V02 | `SIN_ECO` sin retorno válido | Validado funcionalmente, sensor desconectado en ensayo previo |
| V03 | `ERROR_SENSOR` por pulsación larga GPIO14 (~3 s) | Simulación diagnóstica funcional; no prueba avería real |
| V04 | Permanencia en `ERROR_SENSOR` sin pulsación de rearme | Publicaciones repetidas `estado_num:3` antes del rearme |
| V05 | Rearme hacia LIBRE | Monitor Serie registra confirmación y transición |
| V06 | Rearme hacia OBSTACULO_LEJOS | Equipo informa resultado satisfactorio |
| V07 | Rearme hacia OBSTACULO_CERCA | Monitor Serie con `estado_num:2` y lectura válida |
| V08 | LED amarillo remoto GPIO22 ON/OFF | Confirmado por el equipo desde Node-RED |
| V09 | Telemetría MQTT | Mensajes con estado, salud del sensor y distancia cuando válida |
| V10 | Node-RED Dashboard 2.0 | JSON válido (22 nodos) y captura recibida |
| V11 | Grafana | JSON válido (4 paneles) y captura recibida |
| V12 | GPS | **Validado** |
| V13 | Avería eléctrica real detectada automáticamente | **No validado** |

La entrada `ERROR_SENSOR` por comando de diagnóstico `f` se utilizó en una variante de prueba **anterior**; la **versión final** activa la misma simulación mediante pulsación larga del botón. No presentar los registros de la variante anterior como si fueran el registro del firmware actual.

El archivo `evidencias/Captura_Grafana.png` muestra datos en una ventana histórica. Las fotografías acreditan el montaje visible, no todas las conexiones internas. La captura de Grafana con LED remoto en `0` no constituye evidencia visual del LED encendido; el ON/OFF fue comunicado por el equipo.

**Ubicación de los archivos:** `grafana/dashboard_P26_final.json`, `nodered/flujo_P26_final.json`, `evidencias/` y `Informe_Validaciones_P26_E06.pdf` dentro de esta misma carpeta.
