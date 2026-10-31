// Ejecuta la caché LRU con tope en bytes REAL de video-yt (lib/lruBytes.js) con Node (≥ 22).
//
//   node tools/fixtures/lru_js.mjs <video-yt>/frontend/src < casos.json > salida.json
//
// Entrada: [{name, maxBytes (o null: el de por defecto), ops: [[op, ...args]]}], con op en set (clave,
// valor, bytes), get, peek, has, resize (clave, bytes), delete, purge (claves que se quedan) y clear.
// Salida: por caso, tras cada op: {ret, keys, bytes, size, maxBytes, evicted: [[clave, valor]]}.
// Lo llama tools/fixtures/export_fixtures.py (--solo lru); video-yt solo se lee.
import { readFileSync } from 'node:fs'
import { pathToFileURL } from 'node:url'

const root = process.argv[2].replace(/[\\/]+$/, '')
const { createLruBytes } = await import(pathToFileURL(`${root}/lib/lruBytes.js`).href)

const cases = JSON.parse(readFileSync(0, 'utf8'))
const out = cases.map((c) => {
  let evicted = []
  const opts = { onEvict: (k, v) => evicted.push([k, v]) }
  if (c.maxBytes !== null) opts.maxBytes = c.maxBytes
  const cache = createLruBytes(opts)
  return c.ops.map(([op, ...a]) => {
    evicted = []
    let ret = null
    switch (op) {
      case 'set': ret = cache.set(a[0], a[1], a[2]); break
      case 'get': ret = cache.get(a[0]) ?? null; break
      case 'peek': ret = cache.peek(a[0]) ?? null; break
      case 'has': ret = cache.has(a[0]); break
      case 'resize': cache.resize(a[0], a[1]); break
      case 'delete': cache.delete(a[0]); break
      case 'purge': cache.purge((k) => a[0].includes(k)); break
      case 'clear': cache.clear(); break
      default: throw new Error(`op desconocida: ${op}`)
    }
    return { ret, keys: cache.keys(), bytes: cache.bytes, size: cache.size, maxBytes: cache.maxBytes, evicted }
  })
})
process.stdout.write(JSON.stringify(out))
