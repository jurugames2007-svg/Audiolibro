import { readFile, appendFile, stat } from 'node:fs/promises';
import path from 'node:path';

const args = process.argv.slice(2);
if (args.length < 2) {
  console.error('Uso: npm run evaluate:wer -- referencia.txt hipotesis.txt [resultados.csv]');
  process.exit(2);
}

function normalize(text) {
  return text.normalize('NFC').toLocaleLowerCase('es')
    .replace(/[\p{P}\p{S}]+/gu, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

function best(candidates) {
  return candidates.reduce((a, b) => b.cost < a.cost ? b : a);
}

function calculate(reference, hypothesis) {
  const ref = normalize(reference).split(' ').filter(Boolean);
  const hyp = normalize(hypothesis).split(' ').filter(Boolean);
  let previous = Array.from({ length: hyp.length + 1 }, (_, i) => ({ cost: i, s: 0, d: 0, i }));
  for (let r = 1; r <= ref.length; r++) {
    const current = [{ cost: r, s: 0, d: r, i: 0 }];
    for (let h = 1; h <= hyp.length; h++) {
      if (ref[r - 1] === hyp[h - 1]) {
        current.push({ ...previous[h - 1] });
      } else {
        const substitution = { ...previous[h - 1], cost: previous[h - 1].cost + 1, s: previous[h - 1].s + 1 };
        const deletion = { ...previous[h], cost: previous[h].cost + 1, d: previous[h].d + 1 };
        const insertion = { ...current[h - 1], cost: current[h - 1].cost + 1, i: current[h - 1].i + 1 };
        current.push(best([substitution, deletion, insertion]));
      }
    }
    previous = current;
  }
  const result = previous[hyp.length];
  return {
    referenceWords: ref.length,
    hypothesisWords: hyp.length,
    substitutions: result.s,
    deletions: result.d,
    insertions: result.i,
    errors: result.cost,
    werPercent: ref.length ? result.cost / ref.length * 100 : (hyp.length ? Infinity : 0),
  };
}

function csv(value) {
  const text = String(value ?? '');
  return /[",\r\n]/.test(text) ? `"${text.replaceAll('"', '""')}"` : text;
}

const [referencePath, hypothesisPath, csvPath] = args;
const [reference, hypothesis] = await Promise.all([
  readFile(referencePath, 'utf8'),
  readFile(hypothesisPath, 'utf8'),
]);
const result = calculate(reference, hypothesis);
console.log(`Referencia:    ${result.referenceWords} palabras`);
console.log(`Hipótesis:     ${result.hypothesisWords} palabras`);
console.log(`Sustituciones: ${result.substitutions}`);
console.log(`Eliminaciones: ${result.deletions}`);
console.log(`Inserciones:   ${result.insertions}`);
console.log(`WER:           ${Number.isFinite(result.werPercent) ? result.werPercent.toFixed(3) + '%' : 'infinito'}`);

if (csvPath) {
  let empty = true;
  try { empty = (await stat(csvPath)).size === 0; } catch {}
  if (empty) await appendFile(csvPath, 'fecha,referencia,hipotesis,palabras_referencia,sustituciones,eliminaciones,inserciones,wer_porcentaje\r\n', 'utf8');
  const row = [new Date().toISOString(), path.resolve(referencePath), path.resolve(hypothesisPath),
    result.referenceWords, result.substitutions, result.deletions, result.insertions,
    Number.isFinite(result.werPercent) ? result.werPercent.toFixed(6) : 'inf'].map(csv).join(',');
  await appendFile(csvPath, `${row}\r\n`, 'utf8');
  console.log(`Resultado añadido a: ${csvPath}`);
}
