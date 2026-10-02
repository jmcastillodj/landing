# ULTIMATE METER by Jm Castillo

Plugin de medición (VST3, AU y Standalone) para mezcla y mastering: niveles, loudness (ITU-R BS.1770-4 / EBU R 128),
true peak, espectro, espectrograma, goniómetro, historial y **waveform en tiempo real**.

Es un derivado de [Grisey](https://github.com/ZhiyuAlexZhang/Grisey) de Yulania (GPLv3), con apariencia estilo Studio One,
vista Waveform coloreada por frecuencias y modo **Multi** (varias vistas a la vez, cada una redimensionable con divisores).

## Novedades respecto a Grisey

- **Waveform en tiempo real** con muestras reales (máximo y mínimo con signo, escala lineal): canales L / R / M / S o dos carriles,
  colores Multi-band / Static / Color map, historial de picos, barrido estático, time code del DAW y lupa de zoom vertical (marca el clipping en rojo).
- **Multi-vista**: varias vistas a la vez en filas configurables (menú Layout), con divisores arrastrables; se conservan los tamaños.
- **Spectrum** en curva o en barras (16 a 96), con velocidad, peak hold y curvas de referencia (ruido rosa, blanco, marrón, mezcla típica...).
- **Balance** (balance tonal): targets predefinidos con banda de tolerancia y targets propios medidos desde un archivo de audio (completo o una parte).
- **Spectrogram** con esquemas de color (clic derecho): Studio, Magma, Viridis, Ice, Rainbow, Grayscale.
- **Resumen** conmutable entre Loudness y RMS (clic en el título); clic en el True Peak para reiniciarlo.

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
