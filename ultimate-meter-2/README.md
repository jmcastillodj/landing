# ULTIMATE METER 2 · Jm Castillo

Plugin de medición (VST3, AU y Standalone) para mezcla y mastering, con el tema **V2**:
los colores del primer ULTIMATE METER puestos al día (planos, más oscuros y neutros), el logo de
[jmcastillo.es](https://jmcastillo.es) e Inter Tight.

Es un plugin distinto del primer ULTIMATE METER (otro código de plugin y otra carpeta de presets), así que los dos
pueden estar instalados a la vez y las sesiones del primero no cambian.

## Módulos

Spectrum (curva, barras o **relleno**, en L/R o **Mid/Side**), Balance tonal con targets, Spectrogram, Waveform
(libre, barrido o sujeto a la rejilla del DAW), Loudness (ITU-R BS.1770-4 / EBU R 128), Loudness Round (radar),
Reference (hasta 4 pistas, A/B, modo espejo), Correlometer por bandas, VU Meter (VU, RMS y PPM), BPM Detective,
History y Goniometer. En la columna lateral: niveles, sección de monitor (LR, RL, L, R, M, S, mute y polaridad),
loudness, true peak, balance, correlación y desviación mono.

**Multi** muestra varios módulos a la vez en filas y columnas que se arrastran. **Theme** cambia el esquema de
color; "V2" es el de por defecto y los del primer plugin siguen disponibles. **Presets** guarda y carga
ajustes, con importación y exportación en archivos `.umpreset`.

## Cambios respecto al primer ULTIMATE METER

- **Spectrum** como un analizador en tiempo real: promedio en potencia de todos los bloques (solape del 75 %), tiempo
  de promedio ajustable (1000 ms por defecto), rango ajustable (de −18 a −78 dB por defecto), slope de 4,5 dB/oct,
  suavizado de 1/6 de octava y Mid/Side relleno.
- **Waveform** en 3 bandas superpuestas: graves en azul, medios en naranja y agudos en blanco, con filtros de cruce
  de 24 dB/oct en 200 Hz y 2 kHz.

- El VU está calibrado con un tono: un seno a −18 dBFS marca 0 VU con la calibración en −18.
- La sección de monitor y la escucha de una referencia no entran nunca en un render offline, y el monitor no se
  guarda en los presets.
- Las referencias se remuestrean con un sinc con ventana, se vuelven a leer si cambia la frecuencia de la sesión y
  se guardan y se restauran con la sesión aunque la ventana esté cerrada.
- El radar empieza de nuevo cuando el DAW empieza a reproducir.
- Los anchos de columna de Multi se guardan, y con muchos módulos se encogen en vez de salirse de la ventana.
- El Correlometer y el VU solo calculan mientras se ven.
- El layout sigue al DAW cuando este restaura el estado con la ventana abierta.

## Compilar

Hace falta CMake 3.22 o posterior y un compilador de C++20. JUCE va fijado a la versión 9.0.6.

```bash
git clone --depth 1 --branch 9.0.6 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Los plugins quedan en `build/UltimateMeter2_artefacts/Release/` (`VST3`, `AU` en macOS y `Standalone`). Con
`-DULTIMATEMETER2_COPY_AFTER_BUILD=ON` se copian a las carpetas de plugins del usuario. En Linux hacen falta antes
`libasound2-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxi-dev libfreetype-dev
libfontconfig1-dev libgl1-mesa-dev`.

En GitHub Actions (`.github/workflows/ultimate-meter-2.yml`) cada push compila macOS, y Windows se compila a mano
desde la pestaña Actions.

## Licencia y créditos

GNU General Public License, versión 3 (ver `LICENSE.md`). Copyright © 2026 Jm Castillo.

- Es una versión modificada de [Grisey](https://github.com/ZhiyuAlexZhang/Grisey), © Yulania, bajo GPLv3. Los
  cambios se hicieron en 2026 y cada archivo lo indica en su cabecera.
- El detector de tempo viene de BPM Detective, del mismo autor.
- Usa [JUCE](https://juce.com) 9.0.6 bajo AGPLv3.
- La tipografía es Inter Tight, bajo SIL Open Font License (`Assets/InterTight-OFL.txt`).
- El logo y el isotipo de Jm Castillo son marca de su autor y no están cubiertos por la GPL.
