import { spawnSync } from 'node:child_process';
import { createWriteStream, existsSync } from 'node:fs';
import { copyFile, mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import https from 'node:https';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const automatic = process.argv.includes('--automatic');
const markerPath = path.join(root, '.vozlarga-update.json');

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

async function syncRuntime() {
  // Avoid recursively invoking postinstall. The newly updated setup script
  // copies runtime files and reuses every model whose SHA-256 is still valid.
  const npmCommand = process.platform === 'win32' ? 'npm.cmd' : 'npm';
  const install = run(npmCommand, ['install', '--ignore-scripts', '--no-audit', '--no-fund'], {
    stdio: 'inherit', encoding: undefined, timeout: 300000,
  });
  if (install.error || install.status !== 0)
    throw new Error('npm no pudo sincronizar las dependencias.');

  const setup = run(process.execPath, [path.join(root, 'scripts', 'setup.mjs')], {
    stdio: 'inherit', encoding: undefined, timeout: 1800000,
  });
  if (setup.error || setup.status !== 0)
    throw new Error('no se pudo preparar el runtime actualizado. Los modelos existentes no fueron eliminados.');
}

function getJson(url, redirects = 0) {
  if (redirects > 10) return Promise.reject(new Error('demasiadas redirecciones HTTP'));
  return new Promise((resolve, reject) => {
    const request = https.get(url, {
      headers: { 'User-Agent': 'VozLarga-PC/1.0 updater', Accept: 'application/vnd.github+json' },
      timeout: 30000,
    }, response => {
      if ([301, 302, 303, 307, 308].includes(response.statusCode)) {
        const location = response.headers.location;
        response.resume();
        if (!location) return reject(new Error('redirección sin destino'));
        return resolve(getJson(new URL(location, url).href, redirects + 1));
      }
      let body = '';
      response.setEncoding('utf8');
      response.on('data', chunk => { body += chunk; });
      response.on('end', () => {
        if (response.statusCode !== 200) return reject(new Error(`GitHub respondió HTTP ${response.statusCode}`));
        try { resolve(JSON.parse(body)); }
        catch { reject(new Error('GitHub devolvió una respuesta inválida')); }
      });
      response.on('error', reject);
    });
    request.on('timeout', () => request.destroy(new Error('tiempo de espera agotado')));
    request.on('error', reject);
  });
}

function downloadFile(url, destination, redirects = 0) {
  if (redirects > 10) return Promise.reject(new Error('demasiadas redirecciones HTTP'));
  return new Promise((resolve, reject) => {
    const request = https.get(url, {
      headers: { 'User-Agent': 'VozLarga-PC/1.0 updater', Accept: 'application/zip,*/*' },
      timeout: 60000,
    }, response => {
      if ([301, 302, 303, 307, 308].includes(response.statusCode)) {
        const location = response.headers.location;
        response.resume();
        if (!location) return reject(new Error('redirección sin destino'));
        return resolve(downloadFile(new URL(location, url).href, destination, redirects + 1));
      }
      if (response.statusCode !== 200) {
        response.resume();
        return reject(new Error(`descarga HTTP ${response.statusCode}`));
      }
      const outputFile = createWriteStream(destination, { flags: 'w' });
      response.pipe(outputFile);
      outputFile.on('finish', () => outputFile.close(resolve));
      outputFile.on('error', reject);
      response.on('error', reject);
    });
    request.on('timeout', () => request.destroy(new Error('tiempo de espera agotado')));
    request.on('error', reject);
  });
}

async function copyManagedTree(source, destination, topLevel = true) {
  const protectedNames = new Set(['.git', 'node_modules', 'VozLarga-PC-1.0', 'build']);
  await mkdir(destination, { recursive: true });
  for (const entry of await readdir(source, { withFileTypes: true })) {
    if (topLevel && protectedNames.has(entry.name)) continue;
    const from = path.join(source, entry.name);
    const to = path.join(destination, entry.name);
    if (entry.isDirectory()) await copyManagedTree(from, to, false);
    else if (entry.isFile()) {
      await mkdir(path.dirname(to), { recursive: true });
      await copyFile(from, to);
    }
  }
}

async function updateExportedZip() {
  const packageInfo = JSON.parse(await readFile(path.join(root, 'package.json'), 'utf8'));
  const config = packageInfo.vozlarga || {};
  const owner = config.updateOwner || 'jurugames2007-svg';
  const repository = config.updateRepo || 'Audiolibro';
  const branch = process.env.VOZLARGA_UPDATE_BRANCH || config.updateBranch || 'main';
  const encodedBranch = encodeURIComponent(branch);

  console.log(`Buscando actualizaciones de ${owner}/${repository}:${branch}…`);
  const commit = await getJson(`https://api.github.com/repos/${owner}/${repository}/commits/${encodedBranch}`);
  const latestSha = commit.sha;
  if (!latestSha || typeof latestSha !== 'string') throw new Error('GitHub no informó la revisión actual.');

  try {
    const marker = JSON.parse(await readFile(markerPath, 'utf8'));
    if (marker.sha === latestSha) {
      console.log('✓ VozLarga ya está actualizado.');
      return;
    }
  } catch {
    // A ZIP downloaded from GitHub has no revision marker. Its first start
    // performs one synchronization and creates the marker for later checks.
  }

  console.log('Descargando únicamente el paquete del repositorio; los modelos y trabajos se conservarán…');
  const temporary = await mkdtemp(path.join(os.tmpdir(), 'vozlarga-update-'));
  const archive = path.join(temporary, 'repository.zip');
  const extracted = path.join(temporary, 'extracted');
  try {
    const branchPath = branch.split('/').map(encodeURIComponent).join('/');
    await downloadFile(`https://codeload.github.com/${owner}/${repository}/zip/refs/heads/${branchPath}`, archive);
    await mkdir(extracted, { recursive: true });

    let expand;
    if (process.platform === 'win32') {
      const quote = value => value.replaceAll("'", "''");
      const command = `Expand-Archive -LiteralPath '${quote(archive)}' -DestinationPath '${quote(extracted)}' -Force`;
      expand = run('powershell.exe', ['-NoLogo', '-NoProfile', '-NonInteractive', '-Command', command], {
        stdio: 'inherit', encoding: undefined, timeout: 300000,
      });
    } else {
      expand = run('unzip', ['-q', archive, '-d', extracted], {
        stdio: 'inherit', encoding: undefined, timeout: 300000,
      });
    }
    if (expand.error || expand.status !== 0) throw new Error('No se pudo extraer la actualización.');

    const roots = (await readdir(extracted, { withFileTypes: true })).filter(entry => entry.isDirectory());
    if (roots.length !== 1) throw new Error('estructura inesperada en el ZIP de actualización.');
    await copyManagedTree(path.join(extracted, roots[0].name), root);
    await syncRuntime();
    await writeFile(markerPath, `${JSON.stringify({ sha: latestSha, branch, updatedAt: new Date().toISOString() }, null, 2)}\n`, 'utf8');
    console.log('✓ Auto-update completado; modelos, checkpoints y transcripciones conservados.');
  } finally {
    await rm(temporary, { recursive: true, force: true });
  }
}

async function updateGitClone() {
  const gitVersion = run('git', ['--version']);
  if (gitVersion.error || gitVersion.status !== 0)
    throw new Error('Git no está disponible. Se conservará la versión instalada.');

  const branchResult = run('git', ['branch', '--show-current']);
  const branch = branchResult.stdout?.trim();
  if (!branch) throw new Error('el repositorio no está en una rama local.');
  if (run('git', ['remote', 'get-url', 'origin']).status !== 0) {
    console.log('No existe el remoto origin; se conservará la versión instalada.');
    return;
  }

  // Never overwrite source changes made by the user. Ignored portable data,
  // downloaded models and node_modules do not make the checkout dirty.
  const statusResult = run('git', ['status', '--porcelain', '--untracked-files=no']);
  if (statusResult.status !== 0) throw new Error(output(statusResult) || 'no se pudo comprobar el estado de Git.');
  if (statusResult.stdout.trim()) {
    console.warn('Hay cambios locales en archivos del repositorio; no se aplicará el auto-update.');
    console.warn('Los modelos y la carpeta VozLarga-PC-1.0 no cuentan como cambios locales.');
    return;
  }

  console.log(`Buscando actualizaciones para ${branch}…`);
  const fetch = run('git', ['fetch', '--quiet', '--prune', 'origin', branch], { timeout: 120000 });
  if (fetch.error || fetch.status !== 0)
    throw new Error(output(fetch) || 'sin conexión con el repositorio. Se conservará la versión instalada.');

  // FETCH_HEAD works even in single-branch clones with no remote-tracking ref.
  const remoteRef = 'FETCH_HEAD';
  const behindResult = run('git', ['rev-list', '--count', `HEAD..${remoteRef}`]);
  const behind = Number(behindResult.stdout?.trim() || 0);
  if (behindResult.status !== 0 || !Number.isFinite(behind))
    throw new Error(output(behindResult) || 'no se pudo comparar la versión local.');
  if (behind === 0) {
    console.log('✓ VozLarga ya está actualizado.');
    return;
  }

  if (run('git', ['merge-base', '--is-ancestor', 'HEAD', remoteRef]).status !== 0) {
    console.warn('La rama local y la remota han divergido; no se modificará el repositorio automáticamente.');
    return;
  }

  console.log(`Aplicando ${behind} actualización${behind === 1 ? '' : 'es'} sin borrar modelos ni trabajos…`);
  const merge = run('git', ['merge', '--ff-only', '--quiet', remoteRef], { timeout: 120000 });
  if (merge.error || merge.status !== 0) throw new Error(output(merge) || 'Git no pudo aplicar el avance rápido.');
  await syncRuntime();
  console.log('✓ Auto-update completado; modelos, checkpoints y transcripciones conservados.');
}

async function main() {
  if (process.env.VOZLARGA_NO_UPDATE === '1') {
    console.log('Actualización automática desactivada por VOZLARGA_NO_UPDATE=1.');
    return;
  }
  if (existsSync(path.join(root, '.git'))) await updateGitClone();
  else await updateExportedZip();
}

main().catch(error => fail(error.message));
