import { spawnSync } from 'node:child_process';
import { readFile, rm, mkdtemp, stat } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const ffmpeg = process.env.VOZLARGA_FFMPEG || path.join(root, 'VozLarga-PC-1.0', 'tools', 'ffmpeg.exe');
const durationSeconds = 2 * 60 * 60;
const blockSeconds = 900;
const stepSeconds = 898;

function run(args, allowFailure = false) {
  const result = spawnSync(ffmpeg, args, {
    encoding: 'utf8',
    windowsHide: true,
    timeout: 300000,
    maxBuffer: 10 * 1024 * 1024,
  });
  const text = `${result.stdout || ''}${result.stderr || ''}`;
  if (!allowFailure && (result.error || result.status !== 0))
    throw new Error(`FFmpeg falló (${result.status}): ${text.slice(-2000)}`);
  return { ...result, text };
}

function parseDuration(text) {
  const match = /Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)/.exec(text);
  if (!match) return 0;
  return Number(match[1]) * 3600 + Number(match[2]) * 60 + Number(match[3]);
}

function parseWave(buffer) {
  if (buffer.toString('ascii', 0, 4) !== 'RIFF' || buffer.toString('ascii', 8, 12) !== 'WAVE')
    throw new Error('el bloque generado no es RIFF/WAVE');
  let offset = 12;
  let format;
  let dataBytes;
  while (offset + 8 <= buffer.length) {
    const id = buffer.toString('ascii', offset, offset + 4);
    const size = buffer.readUInt32LE(offset + 4);
    const body = offset + 8;
    if (id === 'fmt ' && size >= 16) {
      format = {
        codec: buffer.readUInt16LE(body),
        channels: buffer.readUInt16LE(body + 2),
        sampleRate: buffer.readUInt32LE(body + 4),
        bits: buffer.readUInt16LE(body + 14),
      };
    }
    if (id === 'data') { dataBytes = size; break; }
    offset = body + size + (size & 1);
  }
  if (!format || dataBytes === undefined) throw new Error('cabecera WAV incompleta');
  return { ...format, dataBytes, seconds: dataBytes / (format.sampleRate * format.channels * (format.bits / 8)) };
}

async function testFile(filename, extension) {
  const probe = run(['-hide_banner', '-i', filename], true);
  const duration = parseDuration(probe.text);
  if (Math.abs(duration - durationSeconds) > 0.2)
    throw new Error(`${extension}: duración inesperada ${duration}`);

  const total = Math.max(1, Math.ceil(Math.max(0, duration - 2) / stepSeconds));
  if (total !== 9) throw new Error(`${extension}: se esperaban 9 bloques y se calcularon ${total}`);

  const wav = path.join(path.dirname(filename), `chunk-${extension}.wav`);
  for (let index = 0; index < total; index++) {
    const start = index * stepSeconds;
    const length = Math.min(blockSeconds, duration - start);
    run(['-hide_banner', '-loglevel', 'error', '-y', '-ss', start.toFixed(3), '-i', filename,
      '-t', length.toFixed(3), '-vn', '-sn', '-dn', '-af',
      'highpass=f=70,lowpass=f=7600,afftdn=nf=-25,dynaudnorm=f=150:g=15',
      '-ac', '1', '-ar', '16000', '-c:a', 'pcm_s16le', wav]);
    const wave = parseWave(await readFile(wav));
    if (wave.codec !== 1 || wave.channels !== 1 || wave.sampleRate !== 16000 || wave.bits !== 16)
      throw new Error(`${extension}: formato WAV incorrecto en bloque ${index + 1}`);
    if (Math.abs(wave.seconds - length) > 0.15)
      throw new Error(`${extension}: bloque ${index + 1} mide ${wave.seconds}s, esperado ${length}s`);
  }
  await rm(wav, { force: true });
  const info = await stat(filename);
  console.log(`✓ ${extension.toUpperCase()}: ${duration.toFixed(2)} s, ${total} bloques, ${(info.size / 1048576).toFixed(1)} MB`);
}

async function main() {
  const version = run(['-hide_banner', '-version']);
  if (!version.text.includes('ffmpeg version')) throw new Error('FFmpeg no respondió a -version');

  const temporary = await mkdtemp(path.join(os.tmpdir(), 'vozlarga-2h-'));
  try {
    const mp3 = path.join(temporary, 'prueba-2-horas.mp3');
    const m4a = path.join(temporary, 'prueba-2-horas.m4a');
    console.log('Generando archivos sintéticos de dos horas…');
    run(['-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'anullsrc=r=16000:cl=mono',
      '-t', String(durationSeconds), '-c:a', 'libmp3lame', '-b:a', '24k', '-y', mp3]);
    run(['-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'anullsrc=r=16000:cl=mono',
      '-t', String(durationSeconds), '-c:a', 'aac', '-b:a', '24k', '-movflags', '+faststart', '-y', m4a]);
    await testFile(mp3, 'mp3');
    await testFile(m4a, 'm4a');
    console.log('✓ Regresión de formatos largos superada.');
  } finally {
    if (process.env.VOZLARGA_KEEP_TEST !== '1') await rm(temporary, { recursive: true, force: true });
    else console.log(`Archivos conservados en: ${temporary}`);
  }
}

main().catch(error => {
  console.error(`Error de prueba: ${error.message}`);
  process.exitCode = 1;
});
