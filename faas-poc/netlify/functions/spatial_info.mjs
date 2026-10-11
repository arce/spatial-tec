// Netlify Function (Node runtime, v2 API: Web Request/Response).
import { readFileSync } from 'node:fs';
import { handle } from '../../src/http.mjs';

// Compiled once per cold start; reused across warm invocations.
const wasmModule = await WebAssembly.compile(
  readFileSync(new URL('../../wasm/spatial_info.wasm', import.meta.url)),
);

export default async (request) => handle(request, 'spatial_info', wasmModule);

export const config = { path: '/api/spatial_info' };
