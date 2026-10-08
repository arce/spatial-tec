# Spatial TEC como función en la nube (prueba de concepto: `spatial_info`)

`spatial_info` (C++17/STL) compilado a **WebAssembly** y expuesto como una función HTTP,
con el mismo código para **Netlify Functions** (Node) y **Cloudflare Workers**.
No se tradujo nada a JavaScript: la lógica sigue siendo el C++ original.

```
faas-poc/
├── wasm/spatial_info.{mjs,wasm}   # salida de build.sh (227 KB el .wasm)
├── src/core.mjs                   # ejecuta un comando WASM sobre un sistema de archivos en memoria
├── src/http.mjs                   # capa HTTP común (Request/Response estándar)
├── netlify/functions/spatial_info.mjs + netlify.toml
├── cloudflare/worker.mjs + wrangler.toml
├── test/compare.mjs + test/data/  # compara WASM vs binario nativo
└── build.sh                       # C++ -> WASM
```

## Estado de las pruebas

| Qué | Resultado |
|---|---|
| WASM en Node vs binario nativo (6 casos: 2 CSV, 1 ASC, sin argumentos, extensión inválida, archivo inexistente) | **Idéntico byte a byte** (stdout, stderr y código de salida) |
| Handler de Netlify y Worker de Cloudflare | **Escritos pero NO probados** (ni con `wrangler dev` ni con `netlify dev`): el entorno donde se generó no tenía salida a esos servicios. Pruébalos localmente antes de publicar (abajo). |

## Cambio recomendado en el código C++ (1 línea)

`include/core/spatial_types.hpp`, en `VectorFeature`:

```cpp
GeometryType type = GeometryType::POINT;   // antes: GeometryType type;  (sin inicializar)
```

Con el campo sin inicializar, si una fila no parsea geometría (p. ej. `data/Distritos_Liberia.csv`, cuyo
encabezado trae caracteres BOM) el tipo es basura: el nativo imprimió `POINT` por casualidad y el WASM un
tipo vacío. El `.wasm` incluido se compiló **con** esta corrección; el repo aún no la tiene.
Al aplicarla, el nativo y el WASM coinciden en los 6 casos.

## Compilar

Necesitas Emscripten (≥ 4.0) en el PATH (`source emsdk_env.sh`):

```bash
faas-poc/build.sh spatial_info      # regenera wasm/spatial_info.{mjs,wasm}
cd faas-poc && npm install
node test/compare.mjs ../build/spatial_info   # requiere el binario nativo de tu plataforma (make spatial_info)
```

> El `.wasm` incluido se generó con una variante de Emscripten (4.0.14 sobre LLVM 21). Con un emsdk
> estándar el resultado debe ser equivalente; si cambia algo, `compare.mjs` lo detecta.

## Probar localmente

```bash
cd faas-poc && npm install
npx wrangler dev          # Cloudflare -> http://127.0.0.1:8787/
npx netlify dev           # Netlify    -> http://localhost:8888/api/spatial_info
```

## Publicar

**Cloudflare**
```bash
npx wrangler login
npx wrangler deploy       # -> https://spatial-tec-info.<tu-subdominio>.workers.dev
```

**Netlify**
```bash
npx netlify login
npx netlify init          # o: npx netlify link, si el sitio ya existe
npx netlify deploy --prod # -> https://<tu-sitio>.netlify.app/api/spatial_info
```
(También sirve conectar el repo a Netlify con *base directory* `faas-poc`.)

## Cómo usarlo una vez publicado

Contrato: `POST` con **el archivo como cuerpo** y su nombre en `?filename=` (con la extensión correcta,
`.csv`, `.tsv`, `.asc` o `.grd`, porque el comando decide el formato por la extensión).

```bash
# Vector (CSV con geometría WKT)
curl -X POST --data-binary @data/poblados2008wgs84.csv \
  "https://<tu-url>/api/spatial_info?filename=poblados2008wgs84.csv"     # Netlify
curl -X POST --data-binary @data/poblados2008wgs84.csv \
  "https://spatial-tec-info.<subdominio>.workers.dev/?filename=poblados2008wgs84.csv"   # Cloudflare

# Raster (ASCII Grid)
curl -X POST --data-binary @dem.asc "https://<tu-url>/api/spatial_info?filename=dem.asc"
```

Respuesta (HTTP 200):

```json
{
  "command": "spatial_info",
  "exitCode": 0,
  "stdout": "\n========================================\n  VECTOR INFORMATION\n ...",
  "stderr": "",
  "ms": 12
}
```

- `exitCode` es el código de salida del comando (`0` bien, `1` error de uso o de lectura). Un
  `exitCode` ≠ 0 **sigue siendo HTTP 200**: es el resultado del comando; el motivo viene en `stderr`.
- Errores HTTP: `400` (falta o es inválido `filename`), `405` (método), `413` (cuerpo > 5 MB).
- `GET` devuelve una descripción breve del uso.

Desde JavaScript:

```js
const r = await fetch(`${BASE}/api/spatial_info?filename=ciudades.csv`, {
  method: 'POST',
  body: await file.arrayBuffer(),
});
const { exitCode, stdout, stderr } = await r.json();
```

## Límites a tener en cuenta

- **Tamaño de entrada**: Netlify Functions síncronas aceptan ~6 MB de cuerpo; Cloudflare permite más
  (según plan), pero un Worker tiene ~128 MB de memoria. `http.mjs` limita a 5 MB (`MAX_BYTES`).
- **Memoria**: los rasters se cargan completos (8 bytes/celda); un `.asc` grande puede no caber.
- **Seguridad**: el endpoint es público y sin autenticación; agrégale una clave o restricción de acceso
  antes de usarlo en serio. El nombre de archivo se valida para impedir rutas (`../`).
- Los archivos viven solo en memoria durante la invocación; nada se guarda.

## Extender a otros comandos

1. `faas-poc/build.sh <comando>` (p. ej. `spatial_filter_vector`).
2. Registrar la fábrica en `COMMANDS` de `src/core.mjs`.
3. Los comandos con salida a archivo (`<entrada> <salida> ...`) ya están soportados por `core.mjs`
   (devuelve en `outputs` los archivos nuevos); falta decidir cómo exponerlos en `http.mjs`
   (JSON con base64, o multipart). Comandos con archivos acompañantes (`.shp/.shx/.dbf`, `.network`)
   necesitan recibir varios archivos por petición.
4. Antes de publicar cada comando, repetir `compare.mjs` con casos suyos.
