import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const automatic = process.argv.includes('--automatic');

function run(command, args, options = {}) {
  return spawnSync(command, args, {
    cwd: root,
    encoding: 'utf8',
    windowsHide: true,
    env: { ...process.env, GIT_TERMINAL_PROMPT: '0' },
    ...options,
  });
}

function output(result) {
  return `${result.stdout || ''}${result.stderr || ''}`.trim();
}

function fail(message) {
  const prefix = automatic ? 'Actualización omitida' : 'No se pudo actualizar';
  console.warn(`${prefix}: ${message}`);
  process.exitCode = 1;
}

async function main() {
  if (process.env.VOZLARGA_NO_UPDATE === '1') {
    console.log('Actualización automática desactivada por VOZLARGA_NO_UPDATE=1.');
    return;
  }
  if (!existsSync(path.join(root, '.git'))) {
    console.log('No es un clon Git; se usará el portable actual sin buscar actualizaciones.');
    return;
  }

  const gitVersion = run('git', ['--version']);
  if (gitVersion.error || gitVersion.status !== 0) {
    fail('Git no está disponible. Se conservará la versión instalada.');
    return;
  }

  const branchResult = run('git', ['branch', '--show-current']);
  const branch = branchResult.stdout?.trim();
  if (!branch) {
    fail('el repositorio no está en una rama local.');
    return;
  }

  const remoteResult = run('git', ['remote', 'get-url', 'origin']);
  if (remoteResult.status !== 0) {
    console.log('No existe el remoto origin; se conservará la versión instalada.');
    return;
  }

  // Never overwrite source changes made by the user. Ignored portable data,
  // downloaded models and node_modules do not make the checkout dirty.
  const statusResult = run('git', ['status', '--porcelain', '--untracked-files=no']);
  if (statusResult.status !== 0) {
    fail(output(statusResult) || 'no se pudo comprobar el estado de Git.');
    return;
  }
  if (statusResult.stdout.trim()) {
    console.warn('Hay cambios locales en archivos del repositorio; no se aplicará el auto-update.');
    console.warn('Los modelos y la carpeta VozLarga-PC-1.0 no cuentan como cambios locales.');
    return;
  }

  console.log(`Buscando actualizaciones para ${branch}…`);
  const fetch = run('git', ['fetch', '--quiet', '--prune', 'origin', branch], { timeout: 120000 });
  if (fetch.error || fetch.status !== 0) {
    fail(output(fetch) || 'sin conexión con el repositorio. Se conservará la versión instalada.');
    return;
  }

  const remoteRef = `origin/${branch}`;
  const remoteExists = run('git', ['rev-parse', '--verify', '--quiet', remoteRef]);
  if (remoteExists.status !== 0) {
    fail(`no existe ${remoteRef}.`);
    return;
  }

  const behindResult = run('git', ['rev-list', '--count', `HEAD..${remoteRef}`]);
  const behind = Number(behindResult.stdout?.trim() || 0);
  if (behindResult.status !== 0 || !Number.isFinite(behind)) {
    fail(output(behindResult) || 'no se pudo comparar la versión local.');
    return;
  }
  if (behind === 0) {
    console.log('✓ VozLarga ya está actualizado.');
    return;
  }

  const ancestor = run('git', ['merge-base', '--is-ancestor', 'HEAD', remoteRef]);
  if (ancestor.status !== 0) {
    console.warn('La rama local y la remota han divergido; no se modificará el repositorio automáticamente.');
    return;
  }

  console.log(`Aplicando ${behind} actualización${behind === 1 ? '' : 'es'} sin borrar modelos ni trabajos…`);
  const merge = run('git', ['merge', '--ff-only', '--quiet', remoteRef], { timeout: 120000 });
  if (merge.error || merge.status !== 0) {
    fail(output(merge) || 'Git no pudo aplicar el avance rápido.');
    return;
  }

  // Synchronize npm metadata without recursively invoking postinstall. Then
  // run the newly updated setup script, which copies only runtime files and
  // reuses every model whose size and SHA-256 remain valid.
  const npmCommand = process.platform === 'win32' ? 'npm.cmd' : 'npm';
  const install = run(npmCommand, ['install', '--ignore-scripts', '--no-audit', '--no-fund'], { stdio: 'inherit', encoding: undefined, timeout: 300000 });
  if (install.error || install.status !== 0) {
    fail('npm no pudo sincronizar las dependencias. El repositorio sí quedó actualizado.');
    return;
  }

  const setup = run(process.execPath, [path.join(root, 'scripts', 'setup.mjs')], { stdio: 'inherit', encoding: undefined, timeout: 1800000 });
  if (setup.error || setup.status !== 0) {
    fail('no se pudo terminar de preparar el runtime actualizado. Los modelos existentes no fueron eliminados.');
    return;
  }

  console.log('✓ Auto-update completado; modelos, checkpoints y transcripciones conservados.');
}

main().catch(error => fail(error.message));
