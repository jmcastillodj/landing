# ULTIMATE METER by Jm Castillo

Plugin de medición (VST3, AU y Standalone) para mezcla y mastering: niveles, loudness (ITU-R BS.1770-4 / EBU R 128),
true peak, espectro, espectrograma, goniómetro, historial y **waveform en tiempo real**.

Es un derivado de [Grisey](https://github.com/ZhiyuAlexZhang/Grisey) de Yulania (GPLv3), con apariencia estilo Studio One,
vista Waveform coloreada por frecuencias y modo **Multi** (varias vistas a la vez, cada una redimensionable con divisores).

## Compilar

```bash
git clone --depth 1 https://github.com/juce-framework/JUCE.git JUCE
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

El VST3 queda en `build/UltimateMeter_artefacts/Release/VST3`. Con GitHub Actions
(`.github/workflows/ultimate-meter.yml`) se generan los binarios de Windows y macOS como artefactos.

## Licencia y créditos

GPLv3 (ver `LICENSE.md`). Basado en Grisey, © Yulania. Usa [JUCE](https://juce.com) bajo AGPLv3.
El aspecto está inspirado en Studio One, sin usar ningún recurso gráfico ni código de PreSonus.
