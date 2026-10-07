// Ejecuta el drawComposite REAL del preview de video-yt (features/editor/render/canvas.js)
// con Node (≥ 22) sobre un contexto 2D que GRABA lo que se dibuja.
//
//   node tools/fixtures/frame_js.mjs <video-yt>/frontend/src < casos.json > salida.json
//
// Entrada: [{tracks, clips, out: [w, h], media: {clip_id: [w, h]}, heads}]. El material es
// un objeto con sus dimensiones (sin píxeles). Salida, por caso: {durs: {id: clipDur de JS},
// frames: [[blit…]…]}: por instante, cada drawImage del material, en orden de dibujo:
//   {id, src: [sx, sy, sw, sh], m: [a, b, c, d, e, f], alpha, clip: [[x, y]…] | null,
//    src_t}
// donde m lleva un píxel de la FUENTE al cuadro de salida (la transformación del contexto
// compuesta con el rectángulo de destino) y clip es el último recorte (la cortinilla),
// como cuadrilátero en el cuadro. Lo llama tools/fixtures/export_fixtures.py; video-yt
// solo se lee.
import { readFileSync } from 'node:fs'
import { register } from 'node:module'
import { pathToFileURL } from 'node:url'

register(new URL('./node_hook.mjs', import.meta.url))

const root = process.argv[2].replace(/[\\/]+$/, '')
const url = (rel) => pathToFileURL(`${root}/${rel}`).href
const { drawComposite } = await import(url('features/editor/render/canvas.js'))
const { clipDur, timelineToSource } = await import(url('features/editor/editorModel.js'))
const { clamp } = await import(url('lib/panning.js'))

// Objeto que acepta cualquier cosa (degradados, patrones, medidas de texto…).
const sink = new Proxy(function () {}, {
  get: (_t, k) => (k === Symbol.toPrimitive ? () => 0 : k === 'width' ? 0 : sink),
  apply: () => sink,
})

function recorder(width, height, blits) {
  let m = [1, 0, 0, 1, 0, 0]
  let alpha = 1
  let path = []
  let clipQuad = null
  const stack = []
  const mul = ([a, b, c, d, e, f], [A, B, C, D, E, F]) => [
    a * A + c * B, b * A + d * B, a * C + c * D, b * C + d * D, a * E + c * F + e, b * E + d * F + f,
  ]
  const pt = (x, y) => [m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]]
  const api = {
    canvas: { width, height },
    save() { stack.push([m, alpha, clipQuad]) },
    restore() { if (stack.length) [m, alpha, clipQuad] = stack.pop() },
    translate(x, y) { m = mul(m, [1, 0, 0, 1, x, y]) },
    rotate(r) { const c = Math.cos(r), s = Math.sin(r); m = mul(m, [c, s, -s, c, 0, 0]) },
    scale(x, y) { m = mul(m, [x, 0, 0, y, 0, 0]) },
    transform(a, b, c, d, e, f) { m = mul(m, [a, b, c, d, e, f]) },
    setTransform(a, b, c, d, e, f) { m = typeof a === 'object' ? [a.a, a.b, a.c, a.d, a.e, a.f] : [a, b, c, d, e, f] },
    resetTransform() { m = [1, 0, 0, 1, 0, 0] },
    getTransform() { const [a, b, c, d, e, f] = m; return { a, b, c, d, e, f } },
    beginPath() { path = [] },
    rect(x, y, w, h) { path.push([pt(x, y), pt(x + w, y), pt(x + w, y + h), pt(x, y + h)]) },
    clip() { clipQuad = path.length === 1 ? path[0] : clipQuad },
    measureText: () => ({ width: 0, actualBoundingBoxAscent: 0, actualBoundingBoxDescent: 0 }),
    drawImage(img, ...a) {
      if (!img || img.__clipId === undefined || a.length !== 8) return
      const [sx, sy, sw, sh, dx, dy, dw, dh] = a
      const k = [dw / sw, 0, 0, dh / sh, dx - sx * dw / sw, dy - sy * dh / sh]
      blits.push({ id: img.__clipId, src: [sx, sy, sw, sh], m: mul(m, k), alpha, clip: clipQuad,
                   src_t: img.currentTime })
    },
  }
  return new Proxy(api, {
    get: (t, k) => (k in t ? t[k] : (k === 'globalAlpha' ? alpha : sink)),
    set: (t, k, v) => { if (k === 'globalAlpha') alpha = v; return true },
  })
}

const cases = JSON.parse(readFileSync(0, 'utf8'))
const out = cases.map(({ tracks, clips, out: [w, h], media, heads }) => ({
  durs: Object.fromEntries(clips.map((c) => [c.id, clipDur(c)])),
  frames: heads.map((head) => {
    const els = new Map()
    for (const c of clips) {
      const dims = media[c.id]
      if (!dims) continue
      // El vídeo va sincronizado con la cabeza: su currentTime es el tiempo de fuente.
      const srcT = clamp(timelineToSource(c, head), c.in_point, c.out_point)
      els.set(c.id, { videoWidth: dims[0], videoHeight: dims[1], currentTime: srcT, __clipId: c.id })
    }
    const blits = []
    const env = {
      clipsRef: { current: clips }, tracksRef: { current: tracks }, mediaEls: { current: els },
      outRef: { current: { w, h } },
    }
    drawComposite(recorder(w, h, blits), head, [], env, { x: 0, y: 0, w, h })
    return blits
  }),
}))
process.stdout.write(JSON.stringify(out))
