// Cloudflare Worker. wrangler bundles the .wasm import as a precompiled WebAssembly.Module.
import wasmModule from '../wasm/spatial_info.wasm';
import { handle } from '../src/http.mjs';

export default {
  async fetch(request) {
    return handle(request, 'spatial_info', wasmModule);
  },
};
