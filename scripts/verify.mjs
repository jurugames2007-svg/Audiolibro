import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { readFile, stat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const portable = path.join(root, 'VozLarga-PC-1.0');
const checksumFile = path.join(portable, 'CHECKSUMS-SHA256.txt');

function hashFile(filename) {
  return new Promise((resolve, reject) => {
    const hash = createHash('sha256');
    const input = createReadStream(filename);
    input.on('data', chunk => hash.update(chunk));
    input.on('error', reject);
    input.on('end', () => resolve(hash.digest('hex')));
  });
}

async function main() {
  let manifest;
  try {
    manifest = await readFile(checksumFile, 'utf8');
  } catch {
    throw new Error('No existe CHECKSUMS-SHA256.txt. Ejecute npm run setup.');
  }

  const rows = manifest.split(/\r?\n/).filter(Boolean);
  if (!rows.length) throw new Error('El manifiesto de integridad está vacío.');

  let verified = 0;
  for (const row of rows) {
    const match = /^([0-9a-f]{64})  (.+)$/.exec(row);
    if (!match) throw new Error(`Línea inválida en el manifiesto: ${row}`);
    const relative = match[2];
    const absolute = path.resolve(portable, ...relative.split('/'));
    if (!absolute.startsWith(`${path.resolve(portable)}${path.sep}`)) throw new Error(`Ruta no segura: ${relative}`);
    const info = await stat(absolute);
    if (!info.isFile()) throw new Error(`No es un archivo: ${relative}`);
    const actual = await hashFile(absolute);
    if (actual !== match[1]) throw new Error(`SHA-256 incorrecto: ${relative}`);
    verified++;
  }

  console.log(`✓ ${verified} archivos verificados en VozLarga-PC-1.0`);
}

main().catch(error => {
  console.error(`Error: ${error.message}`);
  process.exitCode = 1;
});
