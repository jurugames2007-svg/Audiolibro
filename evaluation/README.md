# Evaluación 10/10 de VozLarga

`EVALUACION-VOZLARGA-10-10.xlsx` separa evidencia medida de objetivos todavía
no evaluables. No asigna WER inventados ni atribuye resultados de Tiny/CPU al
producto Small/Vulkan.

## Calcular WER con referencias reales

```powershell
npm run evaluate:wer -- referencia.txt transcripcion.txt resultados-wer.csv
```

El cálculo normaliza mayúsculas, puntuación y espacios, conserva letras y
tildes españolas, y reporta sustituciones, eliminaciones e inserciones.

## Repetir la regresión de formatos largos

```powershell
npm run test:formats
```

Genera MP3 y M4A sintéticos de dos horas, aplica el preprocesamiento, valida los
18 bloques PCM y elimina los temporales. No mide precisión lingüística ni GPU.

## Completar la certificación

Para completar las filas marcadas `NO EVALUADO` se necesitan los audios y
referencias humanas descritos en la hoja **Casos requeridos**, además de ejecutar
Small Q5_1 + Vulkan en la RX 5600 XT física. El umbral de 100% de diarización en
voces solapadas del mismo canal no está soportado sin otro modelo específico.
