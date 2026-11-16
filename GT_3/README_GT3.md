# GT2 - Bastón P26 con HC-SR04

En esta etapa reemplazamos temporalmente el sensor ToF GY-53 por un HC-SR04,
porque las primeras lecturas del ToF no fueron reproducibles. Trabajamos con un
ESP32, un LED verde y un zumbador pasivo. El motor de vibración, su etapa de
potencia y el GPS forman parte de las próximas sesiones del proyecto.

Nuestro montaje actual utiliza los siguientes pines:

- TRIG -> GPIO26
- ECHO -> GPIO25, mediante divisor resistivo de 10 kΩ / 20 kΩ
- LED -> GPIO27
- ZUMBADOR PASIVO -> GPIO32

Alimentamos el HC-SR04 con 5 V y medimos 3,3 V en el punto medio del divisor de
ECHO. Trabajamos a una temperatura ambiente de 24 °C, usamos una velocidad del
sonido de 343 m/s, un periodo de medición de 60 ms y un timeout de 12 000 µs.
Ubicamos el sensor a 0 cm del suelo y orientado hacia el frente.

Antes de verificar fijamos una tolerancia de ±2,29 cm respecto de la regla y un
máximo aceptado de 4,6 % de mediciones expiradas.

| Distancia real | Media HC-SR04 | Dispersión | Desviación | Desviación relativa | Expiradas |
|---:|---:|---:|---:|---:|---:|
| 50,0 cm | 47,71 cm | 0,03 cm | -2,29 cm | -4,6 % | 0,0 % |
| 3,0 cm | 4,25 cm | 0,02 cm | +1,25 cm | +41,6 % | 0,0 % |

El segundo contraste está próximo a la zona muerta del HC-SR04. Aunque el error
absoluto fue de 1,25 cm, su porcentaje resulta alto porque la referencia es de
solo 3 cm.

También medimos la respuesta frente a distintos materiales y ángulos:

| Condición | Muestras | Media | Dispersión | Sin detección |
|---|---:|---:|---:|---:|
| Cartón, 0° | 1217 | 47,97 cm | 0,14 cm | 0,0 % |
| Toalla, 0° | 1189 | 49,75 cm | 0,51 cm | 0,0 % |
| Cartón, 30° | 1216 | 87,96 cm | 9,10 cm | 0,0 % |

La inclinación de 30° produjo la mayor variación. Esto muestra que el ángulo del
obstáculo influye más que el material en las condiciones ensayadas.

Programamos cuatro estados:

- LIBRE
- OBSTACULO_LEJOS
- OBSTACULO_CERCA
- ERROR_SENSOR

Aplicamos los siguientes umbrales con histéresis:

- Entramos a OBSTACULO_LEJOS con una distancia menor o igual a 50 cm y
  regresamos a LIBRE desde 60 cm.
- Entramos a OBSTACULO_CERCA con una distancia menor o igual a 20 cm y
  regresamos a OBSTACULO_LEJOS desde 25 cm.

Desde LIBRE pasamos directamente a OBSTACULO_CERCA cuando la primera lectura
ya es menor o igual a 20 cm. Así evitamos esperar un segundo periodo de
medición antes de activar el aviso cercano.

La banda de 10 cm del límite lejano y la banda de 5 cm del límite cercano son
mayores que la dispersión frontal máxima observada, de 0,51 cm. Así evitamos
cambios repetidos de estado por pequeñas oscilaciones de la lectura.

Durante esta etapa usamos el LED y el zumbador de esta forma:

- LIBRE: LED apagado y zumbador apagado.
- OBSTACULO_LEJOS: LED con parpadeo lento y zumbador apagado.
- OBSTACULO_CERCA: LED fijo y tono continuo de 1 kHz.
- ERROR_SENSOR: doble pulso rápido de LED y tono breve de 2 kHz.

Una medición aislada sin eco se interpreta como camino libre o como un objeto
fuera del alcance. Ingresamos a ERROR_SENSOR únicamente cuando ECHO permanece
en nivel alto durante cinco ciclos consecutivos. Para recuperarnos exigimos tres
ecos válidos consecutivos y la confirmación manual r desde el monitor serie.

El firmware no utiliza delay() ni pulseIn(). Generamos el pulso TRIG y
capturamos ECHO mediante micros(), mientras las decisiones y los patrones de
aviso se actualizan con millis(). Con un periodo de 60 ms, un timeout de 12 ms
y hasta 1 ms para decidir y actualizar los actuadores, nuestro presupuesto
conservador de latencia es menor o igual a 73 ms. La medición física repetida de
esa latencia se incorporará en una próxima sesión.

Este montaje es un prototipo educativo. No sustituye un bastón blanco ni un
perro guía. El cono de detección es cercano a 15°, un escalón hacia abajo puede
no devolver eco y un obstáculo situado fuera del eje o a la altura de la cabeza
puede quedar fuera del campo observado.

En las próximas sesiones integraremos el motor de vibración con su etapa de
potencia, el diodo de protección y, más adelante, el módulo GPS. Antes de
energizar esa etapa revisaremos el transistor, la alimentación y la masa común.
