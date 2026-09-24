# Datos de la landing

- `faq.es.json` / `faq.en.json`: preguntas frecuentes. El build genera a la vez el HTML visible y el JSON-LD `FAQPage`, así que siempre coinciden.
- `ab-examples.json`: ejemplos reales de ANTES/DESPUÉS. Mientras esté vacío (`[]`), la sección no se publica.

## Añadir un antes/después

1. Copia dos archivos (misma canción, mismo fragmento, misma duración, ~30-45 s, MP3 192 kbps o AAC) en `src/assets/audio/`:
   `nombre-cancion-mezcla.mp3` y `nombre-cancion-master.mp3`.
2. Añade una entrada:

```json
[
  {
    "a": "/assets/audio/nombre-cancion-mezcla.mp3",
    "b": "/assets/audio/nombre-cancion-master.mp3",
    "gainA": 0,
    "gainB": -6,
    "title": { "es": "«Canción» · Artista", "en": "“Song” · Artist" },
    "credit": { "es": "Mezcla sin master → master final", "en": "Unmastered mix → final master" },
    "levelMatched": true
  }
]
```

`gainB` (dB) baja el master para compararlo al mismo volumen percibido que la mezcla; si `levelMatched` es `true`, la página lo indica. Después ejecuta `npm run build`.
