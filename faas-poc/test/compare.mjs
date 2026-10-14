// Compares the WASM build of spatial_info against the native binary, byte for byte.
//   node test/compare.mjs /path/to/native/spatial_info
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { runCommand } from '../src/core.mjs';

const native = process.argv[2];
if (!native) { console.error('usage: node test/compare.mjs <native spatial_info binary>'); process.exit(2); }
const dataDir = new URL('./data/', import.meta.url).pathname;
const wasmModule = await WebAssembly.compile(readFileSync(new URL('../wasm/spatial_info.wasm', import.meta.url)));

const cases = [
  ['Distritos_Liberia.csv', ['Distritos_Liberia.csv'], ['Distritos_Liberia.csv']],
  ['poblados2008wgs84.csv', ['poblados2008wgs84.csv'], ['poblados2008wgs84.csv']],
  ['test.asc', ['test.asc'], ['test.asc']],
  ['no arguments', [], []],
  ['unsupported extension', ['foo.xyz'], []],
  ['missing file', ['nada.csv'], []],
];
let ok = true;
for (const [name, args, fileNames] of cases) {
  const files = Object.fromEntries(fileNames.map((f) => [f, readFileSync(dataDir + f)]));
  const w = await runCommand('spatial_info', args, files, { wasmModule });
  const n = spawnSync(native, args, { cwd: dataDir, encoding: 'utf8' });
  const same = w.stdout === n.stdout && w.stderr === n.stderr && w.code === n.status;
  ok &&= same;
  console.log(`${same ? 'OK  ' : 'DIFF'} ${name} (exit wasm=${w.code} native=${n.status})`);
}
process.exit(ok ? 0 : 1);
