import { spawnSync } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

if (process.env.VOZLARGA_NO_UPDATE !== '1') {
  const update = spawnSync(process.execPath, [path.join(root, 'scripts', 'update.mjs'), '--automatic'], {
    cwd: root,
    stdio: 'inherit',
    windowsHide: true,
  });
  if (update.error || update.status !== 0) {
    console.warn('No se pudo completar el auto-update; iniciando la versión local existente.');
  }
}

// Start in a fresh Node process so an updated start.mjs is used immediately.
const start = spawnSync(process.execPath, [path.join(root, 'scripts', 'start.mjs')], {
  cwd: root,
  stdio: 'inherit',
  windowsHide: false,
});
if (start.error) {
  console.error(`No se pudo iniciar VozLarga: ${start.error.message}`);
  process.exitCode = 1;
} else {
  process.exitCode = start.status ?? 0;
}
