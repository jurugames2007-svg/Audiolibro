import { spawnSync } from 'node:child_process';
import { existsSync, rmSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const portable = path.join(root, 'VozLarga-PC-1.0');
const destination = path.join(root, 'VozLarga-PC-1.0-Windows-x64.zip');

if (process.platform !== 'win32') {
  console.error('La creación del ZIP portable se ejecuta en Windows.');
  process.exit(1);
}
if (!existsSync(path.join(portable, 'CHECKSUMS-SHA256.txt'))) {
  console.error('Ejecute npm run setup antes de crear el ZIP.');
  process.exit(1);
}

const verification = spawnSync(process.execPath, [path.join(root, 'scripts', 'verify.mjs')], { stdio: 'inherit' });
if (verification.status !== 0) process.exit(verification.status ?? 1);
rmSync(destination, { force: true });

const escapePowerShell = value => value.replaceAll("'", "''");
const command = `Compress-Archive -LiteralPath '${escapePowerShell(portable)}' -DestinationPath '${escapePowerShell(destination)}' -CompressionLevel Optimal -Force`;
const result = spawnSync('powershell.exe', ['-NoLogo', '-NoProfile', '-NonInteractive', '-Command', command], { stdio: 'inherit' });
if (result.error) {
  console.error(`No se pudo ejecutar PowerShell: ${result.error.message}`);
  process.exit(1);
}
if (result.status !== 0) process.exit(result.status ?? 1);
console.log(`✓ ZIP creado: ${destination}`);
