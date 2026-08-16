import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const executable = path.join(root, 'VozLarga-PC-1.0', 'VozLarga.exe');

if (process.platform !== 'win32' || process.arch !== 'x64') {
  console.error('VozLarga PC requiere Windows 10/11 x64.');
  process.exit(1);
}
if (!existsSync(executable)) {
  console.error('El portable todavía no está preparado. Ejecute: npm run setup');
  process.exit(1);
}

const child = spawn(executable, [], {
  cwd: path.dirname(executable),
  stdio: 'inherit',
  windowsHide: false,
});

child.on('error', error => {
  console.error(`No se pudo iniciar VozLarga.exe: ${error.message}`);
  process.exitCode = 1;
});
child.on('exit', (code, signal) => {
  if (signal) process.exitCode = 1;
  else process.exitCode = code ?? 0;
});
