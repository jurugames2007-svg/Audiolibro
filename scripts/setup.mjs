import { createHash } from 'node:crypto';
import { createReadStream, createWriteStream, existsSync } from 'node:fs';
import { copyFile, mkdir, readdir, rm, stat, writeFile } from 'node:fs/promises';
import https from 'node:https';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const portable = path.join(root, 'VozLarga-PC-1.0');
const forceWindows = process.env.VOZLARGA_FORCE_WINDOWS_SETUP === '1';

const assets = [
  {
    name: 'Whisper Small multilingüe Q5_1',
    destination: path.join(portable, 'models', 'ggml-small-q5_1.bin'),
    url: process.env.VOZLARGA_WHISPER_MODEL_URL || 'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin?download=true',
    size: 190085487,
    sha256: 'ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb',
  },
  {
    name: 'Silero VAD 6.2.0',
    destination: path.join(portable, 'models', 'ggml-silero-v6.2.0.bin'),
    url: process.env.VOZLARGA_VAD_MODEL_URL || 'https://huggingface.co/ggml-org/whisper-vad/resolve/main/ggml-silero-v6.2.0.bin?download=true',
    size: 885098,
    sha256: '2aa269b785eeb53a82983a20501ddf7c1d9c48e33ab63a41391ac6c9f7fb6987',
  },
];

const runtimeFiles = [
  ['VozLarga.exe', 'VozLarga.exe', 893440, '03b302b7a1b7850aba1fbeab5982fdbdbd8d1ec4e71c300403fd171c5424b8b2'],
  ['engine/whisper-vulkan.exe', 'engine/whisper-vulkan.exe', 54074880, 'a71b5794f0ce6a7646294600bc55319574a5591eab36dba80c9bccae8429bf82'],
  ['engine/whisper-cpu.exe', 'engine/whisper-cpu.exe', 3145728, 'd3c3fa58dffbfde190731f140cff959de5dc9fc779423e0ff4d5b8164adc640e'],
];

function hashFile(filename) {
  return new Promise((resolve, reject) => {
    const hash = createHash('sha256');
    const input = createReadStream(filename);
    input.on('data', chunk => hash.update(chunk));
    input.on('error', reject);
    input.on('end', () => resolve(hash.digest('hex')));
  });
}

async function validFile(filename, size, sha256) {
  try {
    const info = await stat(filename);
    return info.isFile() && info.size === size && await hashFile(filename) === sha256;
  } catch {
    return false;
  }
}

async function copyTree(source, destination) {
  await mkdir(destination, { recursive: true });
  for (const entry of await readdir(source, { withFileTypes: true })) {
    const from = path.join(source, entry.name);
    const to = path.join(destination, entry.name);
    if (entry.isDirectory()) await copyTree(from, to);
    else if (entry.isFile()) await copyFile(from, to);
  }
}

function requestDownload(url, destination, redirects = 0) {
  if (redirects > 10) return Promise.reject(new Error('demasiadas redirecciones'));
  return new Promise((resolve, reject) => {
    const request = https.get(url, {
      headers: {
        'User-Agent': 'VozLarga-PC/1.0 npm-installer',
        Accept: 'application/octet-stream,*/*',
      },
      timeout: 60000,
    }, response => {
      if ([301, 302, 303, 307, 308].includes(response.statusCode)) {
        const location = response.headers.location;
        response.resume();
        if (!location) return reject(new Error(`redirección ${response.statusCode} sin destino`));
        return resolve(requestDownload(new URL(location, url).href, destination, redirects + 1));
      }
      if (response.statusCode !== 200) {
        response.resume();
        return reject(new Error(`HTTP ${response.statusCode}`));
      }

      const total = Number(response.headers['content-length'] || 0);
      let received = 0;
      let lastPercent = -1;
      const output = createWriteStream(destination, { flags: 'w' });
      response.on('data', chunk => {
        received += chunk.length;
        const percent = total ? Math.floor(received * 100 / total) : -1;
        if (percent >= 0 && percent >= lastPercent + 5) {
          process.stdout.write(`  ${percent}% (${(received / 1048576).toFixed(1)} MB)\n`);
          lastPercent = percent;
        }
      });
      response.pipe(output);
      output.on('finish', () => output.close(resolve));
      output.on('error', reject);
      response.on('error', reject);
    });
    request.on('timeout', () => request.destroy(new Error('tiempo de espera agotado')));
    request.on('error', reject);
  });
}

async function downloadVerified(asset) {
  if (await validFile(asset.destination, asset.size, asset.sha256)) {
    console.log(`✓ ${asset.name} ya está verificado`);
    return;
  }

  await mkdir(path.dirname(asset.destination), { recursive: true });
  const temporary = `${asset.destination}.download`;
  await rm(temporary, { force: true });
  console.log(`Descargando ${asset.name}…`);
  let lastError;
  for (let attempt = 1; attempt <= 3; attempt++) {
    try {
      await rm(temporary, { force: true });
      await requestDownload(asset.url, temporary);
      if (!await validFile(temporary, asset.size, asset.sha256)) {
        const actualSize = existsSync(temporary) ? (await stat(temporary)).size : 0;
        throw new Error(`integridad incorrecta (${actualSize} bytes)`);
      }
      await rm(asset.destination, { force: true });
      await copyFile(temporary, asset.destination);
      await rm(temporary, { force: true });
      console.log(`✓ ${asset.name} verificado`);
      return;
    } catch (error) {
      lastError = error;
      await rm(temporary, { force: true });
      if (attempt < 3) console.warn(`  intento ${attempt} falló; reintentando…`);
    }
  }
  throw new Error(`No se pudo obtener ${asset.name}: ${lastError?.message || 'error desconocido'}`);
}

async function collectFiles(directory, base = directory) {
  const result = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const absolute = path.join(directory, entry.name);
    if (entry.isDirectory()) result.push(...await collectFiles(absolute, base));
    else if (entry.isFile() && entry.name !== 'CHECKSUMS-SHA256.txt') {
      result.push([absolute, path.relative(base, absolute).split(path.sep).join('/')]);
    }
  }
  return result;
}

async function writeChecksums() {
  const rows = [];
  for (const [absolute, relative] of await collectFiles(portable)) {
    rows.push(`${await hashFile(absolute)}  ${relative}`);
  }
  rows.sort((a, b) => a.localeCompare(b));
  await writeFile(path.join(portable, 'CHECKSUMS-SHA256.txt'), `${rows.join('\r\n')}\r\n`, 'utf8');
}

async function main() {
  if ((!forceWindows && process.platform !== 'win32') || process.arch !== 'x64') {
    console.log('VozLarga PC se prepara en Windows x64. npm install terminó sin descargar binarios para esta plataforma.');
    return;
  }

  console.log('Preparando VozLarga PC 1.0 portable…');
  await mkdir(portable, { recursive: true });

  for (const [sourceRelative, destinationRelative, size, sha256] of runtimeFiles) {
    const source = path.join(root, 'runtime', 'win32-x64', ...sourceRelative.split('/'));
    if (!await validFile(source, size, sha256)) throw new Error(`Runtime dañado en el repositorio: ${sourceRelative}`);
    const destination = path.join(portable, ...destinationRelative.split('/'));
    await mkdir(path.dirname(destination), { recursive: true });
    await copyFile(source, destination);
  }

  await copyTree(path.join(root, 'packaging', 'licenses'), path.join(portable, 'licenses'));
  await copyTree(path.join(root, 'src'), path.join(portable, 'source-gui'));
  for (const name of ['LEEME.txt', 'BENCHMARK.txt', 'BUILD-INFO.txt']) {
    await copyFile(path.join(root, 'packaging', name), path.join(portable, name));
  }

  const ffmpegSource = path.join(root, 'node_modules', '@ffmpeg-installer', 'win32-x64', 'ffmpeg.exe');
  const ffmpegSize = 64458752;
  const ffmpegSha = 'c8abc49e7be62dde8e12972af373959e0076a7b8dc8040eb45978e0608f8781e';
  if (!await validFile(ffmpegSource, ffmpegSize, ffmpegSha)) {
    throw new Error('FFmpeg no está disponible. Ejecute npm install en Windows 10/11 x64 sin --omit=optional.');
  }
  await mkdir(path.join(portable, 'tools'), { recursive: true });
  await copyFile(ffmpegSource, path.join(portable, 'tools', 'ffmpeg.exe'));

  for (const asset of assets) await downloadVerified(asset);
  await writeChecksums();

  console.log('\n✓ Portable preparado y verificado.');
  console.log(`  ${path.join(portable, 'VozLarga.exe')}`);
  console.log('Ejecute: npm start');
}

main().catch(error => {
  console.error(`\nError: ${error.message}`);
  process.exitCode = 1;
});
