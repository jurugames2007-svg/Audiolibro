# VozLarga PC

Aplicación de escritorio para **Windows 10/11 x64** que transcribe grabaciones extensas en español completamente en local.

- whisper.cpp 1.9.2 con backend Vulkan para GPU AMD y motor CPU independiente.
- Whisper Small multilingüe Q5_1.
- Silero VAD opcional.
- Conversión local con FFmpeg.
- Bloques de 15 minutos con 2 segundos de solapamiento.
- Pausa y reanudación mediante checkpoints.
- Un único TXT UTF-8, verificado y publicado de forma atómica.

## Inicio sencillo con npm

Requiere [Node.js 18 o posterior](https://nodejs.org/) y Windows x64:

```powershell
npm install
npm start
```

`npm install` prepara `VozLarga-PC-1.0/`, instala FFmpeg y descarga una sola vez los modelos oficiales, verificando tamaño y SHA-256. Después, la transcripción y el uso normal mediante `npm start` son totalmente locales y no necesitan conexión.

Si una descarga se interrumpe, repita:

```powershell
npm run setup
```

Comprobación integral y ZIP opcional:

```powershell
npm run verify
npm run portable:zip
```

El ZIP resultante se llama `VozLarga-PC-1.0-Windows-x64.zip`.

## Desarrollo

El fuente de la interfaz Win32 C++17 está en [`src/`](src/). Los binarios Windows x64 necesarios para `npm install` están en [`runtime/`](runtime/) y los textos incorporados a la distribución están en [`packaging/`](packaging/).

El rendimiento objetivo (2 horas en menos de 20 minutos) requiere medición en la RX 5600 XT real y no se garantiza sin ese benchmark.
