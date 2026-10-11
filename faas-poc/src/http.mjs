// Platform-neutral HTTP layer (Web standard Request/Response).
//
//   POST /...?filename=cities.csv   body = raw file bytes
//   -> 200 {"command","exitCode","stdout","stderr"}    (exitCode != 0 is still HTTP 200:
//                                                       it is the command's own result)
//   GET  -> usage text
import { runCommand } from './core.mjs';

const MAX_BYTES = 5 * 1024 * 1024; // Netlify sync functions cap request bodies at ~6 MB

const json = (obj, status = 200) =>
  new Response(JSON.stringify(obj, null, 2), {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8' },
  });

export async function handle(request, command, wasmModule) {
  if (request.method === 'GET') {
    return json({
      command,
      usage: `POST ?filename=<name.csv|name.asc> with the file as the request body`,
    });
  }
  if (request.method !== 'POST') return json({ error: 'Method not allowed' }, 405);

  const url = new URL(request.url);
  const filename = url.searchParams.get('filename') ?? '';
  // Only a bare file name: no paths, so a caller cannot escape the in-memory /work dir.
  if (!/^[\w][\w.\-]{0,100}$/.test(filename)) {
    return json({ error: 'filename query parameter required (letters, digits, _ . -)' }, 400);
  }
  const body = new Uint8Array(await request.arrayBuffer());
  if (body.length > MAX_BYTES) return json({ error: `Body too large (max ${MAX_BYTES} bytes)` }, 413);

  const t0 = Date.now();
  const r = await runCommand(command, [filename], { [filename]: body }, { wasmModule });
  return json({ command, exitCode: r.code, stdout: r.stdout, stderr: r.stderr, ms: Date.now() - t0 });
}
