// Hook de resolución de módulos para importar el frontend de video-yt con Node: Vite
// admite imports relativos sin extensión ('../../lib/panning'); Node no. Prueba la ruta
// tal cual, con .js, con .jsx y como carpeta con index.js. No transforma el código.
import { existsSync, statSync } from 'node:fs'
import { fileURLToPath, pathToFileURL } from 'node:url'

export async function resolve(specifier, context, next) {
  if ((specifier.startsWith('.') || specifier.startsWith('/')) && context.parentURL?.startsWith('file:')) {
    const base = fileURLToPath(new URL(specifier, context.parentURL))
    for (const cand of [base, `${base}.js`, `${base}.jsx`, `${base}/index.js`]) {
      if (existsSync(cand) && statSync(cand).isFile()) return next(pathToFileURL(cand).href, context)
    }
  }
  return next(specifier, context)
}
