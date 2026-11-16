# GT1 — Sensado, Calibración y Alimentación

**Equipo:** E06 · **Proyecto:** — Bastón de asistencia para discapacidad visual
**NRC / Docente:** 12254 - Yainet García
**Fecha de la Sesión B:** 

## 1. Proyecto Wokwi

- Enlace (copia propia, renombrada `E32-GT1`): `<pegar URL de wokwi.com/projects/... aquí>`
- Placa: ESP32 DevKit C v4 (38 pines)
- Sensor analógico del taller (núcleo GT1): potenciómetro en `esp:34` (ADC1), alimentado a 3.3 V — usado como sustituto genérico del sensor del proyecto, siguiendo la guía ("el núcleo puede resolverse con termistor o fotorresistor en todos los casos").
- Sensor real del proyecto: ToF GY-53 (VL53L0X, I2C). No se simula de forma nativa en Wokwi; no aplica sustituto por HC-SR04 en este ítem porque el núcleo del taller trabaja sobre ADC, no sobre el sensor final.

## 2. Calibración de dos puntos

Referencia entregada por la docente: `<describir el patrón/instrumento entregado>`
Rango de trabajo calibrado: `<rango del proyecto, ej. 0-40 cm / 0-40 °C>`

| Punto | Uso | Referencia R | Lectura sin corregir M | Valor calibrado |
|---|---|---|---|---|
| 1 | Cálculo | 8.0 | 10.44 | 8.00 |
| 2 | Cálculo | 31.0 | 34.73 | 31.00 |
| 3 | Verificación | 18.5 | 21.70 | 18.66 |

**Cálculo:**
```
m = (R2 - R1) / (M2 - M1) = (31.0 - 8.0) / (34.73 - 10.44) = 0.9469
b = R1 - m * M1            = 8.0 - 0.9469 * 10.44           = -1.8855
```

**Valores a usar en el firmware:**
```cpp
const float M_CAL = 0.9469f;
const float B_CAL = -1.8855f;
```

**Tolerancia declarada (antes de verificar):** ± 1.0 (unidades del ejercicio) — dispositivo de seguridad para detección de obstáculos; un error mayor a 1 unidad podría comprometer la alerta al usuario.
**Error obtenido en el punto de verificación:** 18.66 − 18.5 = **0.16** → **dentro de tolerancia** 
**Fecha de la calibración:** `<completar: fecha de la Sesión B>`

## 3. Ruido y filtrado

- Filtro implementado: **media móvil** de **N = 5**
- **Criterio de elección de filtro y N:** se mantiene media móvil (no mediana) porque el motor de vibración recibe una señal PWM **proporcional** a la distancia (ver arquitectura, sección D2 del ED): la mediana puede introducir saltos discretos entre valores del buffer, poco adecuados para una respuesta háptica continua. El rechazo de ecos falsos aislados por el movimiento pendular del bastón (riesgo documentado en la sección 5 del ED) no se resuelve en este filtro numérico, sino en una capa distinta: la FSM exige varias lecturas consecutivas coherentes antes de confirmar un obstáculo, evitando que un eco aislado dispare una alerta. N=5 se eligió como balance entre suavizar el ruido del ADC y no introducir una latencia excesiva para una alerta de seguridad.
- **Diferencia observada entre señal cruda y filtrada:** en `t=20000 ms`, `calibrado_C` (crudo) cayó a 2.39 pero `filtrado_C` se mantuvo en 3.20 — la media móvil todavía arrastraba las lecturas más altas de las muestras anteriores.
- **Retardo introducido por el filtro:** con `N_FILTRO=5` y muestreo cada 1 s (taller), la media móvil tarda hasta **5 muestras (~5 s)** en reflejar completamente un cambio brusco de la señal; el retraso típico frente a un escalón es de aproximadamente `(N-1)/2 ≈ 2` muestras.

## 4. Ficha de alimentación

Ver `S3_Ficha_alimentacion_nodo_E32.xlsx` en `/docs`. Resumen:
- Batería 2×18650 en serie (7.4 V), corriente media ≈ 157 mA, autonomía estimada ≈ 16.6 h.

## 5. Commits

- [ ] Commit de la Sesión B con este README actualizado.

