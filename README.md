# VozLarga PC

Aplicación de escritorio portable para Windows 10 x64 que transcribe grabaciones extensas en español completamente en local.

- whisper.cpp 1.9.2 con backend Vulkan para GPU AMD y motor CPU independiente.
- Whisper Small multilingüe Q5_1.
- Silero VAD opcional.
- Conversión local con FFmpeg.
- Bloques de 15 minutos con 2 segundos de solapamiento.
- Pausa y reanudación mediante checkpoints.
- Un único TXT UTF-8, verificado y publicado de forma atómica.

El fuente de la interfaz Win32 C++17 está en [`src/`](src/). Los textos incorporados a la distribución están en [`packaging/`](packaging/).

El rendimiento objetivo (2 horas en menos de 20 minutos) requiere medición en la RX 5600 XT real y no se garantiza sin ese benchmark.
