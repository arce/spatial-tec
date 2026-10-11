// Runs a Spatial TEC command compiled to WASM (Emscripten) on an in-memory filesystem.
// Same code runs in Node (Netlify Functions) and in Cloudflare Workers.
import createSpatialInfo from '../wasm/spatial_info.mjs';

export const COMMANDS = { spatial_info: createSpatialInfo };

/**
 * @param {string} command   e.g. "spatial_info"
 * @param {string[]} args    argv after the program name; file args refer to names in `files`
 * @param {Record<string, Uint8Array|string>} files  input files by name (written to /work)
 * @param {{wasmModule: WebAssembly.Module}} opts    precompiled module of <command>.wasm
 * @returns {Promise<{code:number, stdout:string, stderr:string, outputs:Record<string,Uint8Array>}>}
 */
export async function runCommand(command, args, files, { wasmModule }) {
  const factory = COMMANDS[command];
  if (!factory) throw new Error(`Unknown command: ${command}`);

  let stdout = '', stderr = '';
  const m = await factory({
    noInitialRun: true,
    print: (s) => { stdout += s + '\n'; },
    printErr: (s) => { stderr += s + '\n'; },
    // Workers forbid compiling wasm from bytes at runtime, so we always instantiate
    // an already-compiled module that the platform bundled for us.
    instantiateWasm: (imports, done) => {
      WebAssembly.instantiate(wasmModule, imports).then((inst) => done(inst));
      return {};
    },
  });

  m.FS.mkdir('/work');
  m.FS.chdir('/work');
  for (const [name, data] of Object.entries(files)) m.FS.writeFile(`/work/${name}`, data);
  const inputs = new Set(Object.keys(files));

  let code = 0;
  try {
    code = m.callMain([...args]); // callMain mutates its argument
  } catch (e) {
    if (e && typeof e.status === 'number') code = e.status;
    else { code = 70; stderr += `Internal error: ${e}\n`; }
  }

  const outputs = {};
  for (const n of m.FS.readdir('/work')) {
    if (n === '.' || n === '..' || inputs.has(n)) continue;
    outputs[n] = m.FS.readFile(`/work/${n}`);
  }
  return { code: code ?? 0, stdout, stderr, outputs };
}
