// Genera src/render/src/vfx_shaders.inc: el GLSL de los efectos de vídeo (`clip.vfx`) del
// runtime de video-yt (frontend/src/fx): el preludio común de core/gl.js y el `frag` de cada
// efecto de motor «shader», con su versión y sus parámetros. gc_render lo traduce a SkSL.
//
//   node tools/gen/gen_vfx_shaders.mjs <video-yt>/frontend/src
//
// Rehacer cuando cambien los efectos en video-yt (el test de paridad lo avisa).
import { writeFileSync } from 'node:fs'
import { pathToFileURL } from 'node:url'

const root = process.argv[2].replace(/[\\/]+$/, '')
const load = (rel) => import(pathToFileURL(`${root}/${rel}`).href)
const { EFFECTS } = await load('fx/registry.js')
const { GLSL_PRELUDE } = await load('fx/core/gl.js')

const shaders = EFFECTS.filter((e) => e.engine === 'shader').map((e) => ({
  id: e.id,
  v: e.v,
  pad: e.pad,
  params: e.paramList.map((p) => ({ id: p.id, type: p.type, def: p.def, min: p.min, max: p.max,
    options: p.options?.map((o) => o.id) })),
  frag: e.frag,
}))
// En trozos de 2000 caracteres: MSVC no admite literales de más de 16 KB.
const json = (v) => JSON.stringify(v).match(/[^]{1,2000}/g).map((p) => `R"json(${p})json"`).join('\n    ')
const out = `// Generado por tools/gen/gen_vfx_shaders.mjs desde frontend/src/fx de video-yt: no editar a mano.
constexpr std::string_view kVfxPreludeJson =
    ${json(GLSL_PRELUDE)};
constexpr std::string_view kVfxShadersJson =
    ${json(shaders)};
`
writeFileSync(new URL('../../src/render/src/vfx_shaders.inc', import.meta.url), out)
console.log(`src/render/src/vfx_shaders.inc: ${shaders.length} efectos`)
