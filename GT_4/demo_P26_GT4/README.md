\### Estructura de Tópicos MQTT (Convención de 4 niveles)



| Tópico | QoS | Retained | Descripción del Payload |

| :--- | :--- | :--- | :--- |

| `curso/E06/P26/Baston\_E06` | 1 | Sí | JSON plano con estado FSM, confirmación de hardware, distancia física (cm) e información del GPS. |

| `curso/E06/P26/Baston\_E06/estado` | 1 | Sí | Estado de conexión del nodo (online/offline). Utiliza LWT (Last Will and Testament) automático del broker. |

| `curso/E06/P26/Baston\_E06/cmd` | 1 | No | Canal reservado para comandos entrantes. Ejemplo: `{"comando":"reset"}` para recuperación manual. |

