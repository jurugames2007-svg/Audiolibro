# VozLarga PC

Aplicación de escritorio para **Windows 10/11 x64** que transcribe grabaciones extensas en español completamente en local.

- whisper.cpp 1.9.2 con backend Vulkan para GPU AMD y motor CPU independiente.
- Whisper Large v3 Turbo multilingüe Q5_0 para mayor precisión y menor tendencia a inventar texto.
- Silero VAD opcional.
- Conversión local con FFmpeg para MP3, M4A/AAC, WAV, FLAC y vídeo.
- Mejora determinista de voz: filtro de graves/agudos, reducción de ruido y normalización, sin un segundo modelo IA.
- Diagnóstico de volumen/clipping y omisión de bloques completamente silenciosos.
- Contexto configurable para nombres propios y términos técnicos, más continuidad entre bloques.
- Diarización opcional por canales cuando la reunión esté grabada en estéreo.
- Bloques de 15 minutos con 2 segundos de solapamiento.
- Pausa y reanudación mediante checkpoints.
- Párrafos legibles, acceso directo al audio/TXT y publicación atómica UTF-8.

Whisper Large v3 Turbo Q5_0 es el único modelo necesario para reconocer voz. Sustituye a Small Q5_1 para mejorar nombres, acentos y coherencia. La limpieza de audio, validación, párrafos y herramientas de revisión no descargan otros modelos. Transcribir voz sin ningún modelo de reconocimiento no es técnicamente posible.

## Inicio sencillo con npm

Requiere [Node.js 18 o posterior](https://nodejs.org/) y Windows x64:

```powershell
npm install
npm start
```

`npm install` prepara `VozLarga-PC-1.0/`, instala FFmpeg y descarga una sola vez los modelos oficiales, verificando tamaño y SHA-256. Después, la transcripción es completamente local. Al ejecutar `npm start`, el lanzador consulta el repositorio y **no elimina ni vuelve a descargar** modelos válidos, checkpoints o transcripciones.

- En un clon Git aplica únicamente cambios nuevos mediante avance rápido.
- En una copia descargada como ZIP consulta la revisión de GitHub y sincroniza los archivos administrados del proyecto; nunca toca `VozLarga-PC-1.0/` ni `node_modules/`.

Inicio sin consultar actualizaciones:

```powershell
npm run start:offline
```

Actualización manual o reparación de la instalación:

```powershell
npm run update
npm run setup
```

En clones Git, el auto-update se omite si hay cambios locales o ramas divergentes. Sin conexión siempre se abre la versión instalada. También puede desactivarse definiendo `VOZLARGA_NO_UPDATE=1`.

Comprobación integral, regresión MP3/M4A de dos horas y ZIP opcional:

```powershell
npm run verify
npm run test:formats
npm run portable:zip
```

`npm run test:formats` genera temporalmente MP3 y M4A sintéticos de dos horas,
valida sus 18 bloques PCM y los elimina al terminar. Para calcular WER con una
referencia humana:

```powershell
npm run evaluate:wer -- referencia.txt transcripcion.txt resultados-wer.csv
```

La evaluación auditable está en `evaluation/EVALUACION-VOZLARGA-10-10.xlsx`.

El ZIP resultante se llama `VozLarga-PC-1.0-Windows-x64.zip`.

## Desarrollo

El fuente de la interfaz Win32 C++17 está en [`src/`](src/). Los binarios Windows x64 necesarios para `npm install` están en [`runtime/`](runtime/) y los textos incorporados a la distribución están en [`packaging/`](packaging/).

El rendimiento objetivo (2 horas en menos de 20 minutos) requiere medición en la RX 5600 XT real y no se garantiza sin ese benchmark.
