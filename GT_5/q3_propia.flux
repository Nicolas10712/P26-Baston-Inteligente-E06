// Pregunta: ¿Cual fue la distancia minima detectada (el obstaculo mas cercano) en las ultimas 24 horas?
from(bucket: "mediciones")
  |> range(start: -24h)
  |> filter(fn: (r) => r._measurement == "mediciones")
  |> filter(fn: (r) => r.equipo == "E06" and r.proyecto == "P26" and r.nodo == "Baston_E06")
  |> filter(fn: (r) => r._field == "distancia_cm")
  |> min()