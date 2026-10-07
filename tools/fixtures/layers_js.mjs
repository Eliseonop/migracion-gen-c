// Ejecuta el layersAt/videosAt/stackLayers REAL del preview de video-yt con Node (≥ 22).
//
//   node tools/fixtures/layers_js.mjs <video-yt>/frontend/src < casos.json > salida.json
//
// Entrada: [{tracks, clips, heads}]. Salida, por caso: {stack: [[track_id, capa]…],
// layers: [[i…]…], videos: [[i…]…]}: por instante, la POSICIÓN de cada clip en `clips`
// (los ids pueden repetirse).
// Lo llama tools/fixtures/export_fixtures.py; video-yt solo se lee.
import { readFileSync } from 'node:fs'
import { pathToFileURL } from 'node:url'

const root = process.argv[2].replace(/[\\/]+$/, '')
const url = (rel) => pathToFileURL(`${root}/${rel}`).href
const { layersAt, videosAt } = await import(url('lib/clipLayout.js'))
const { stackLayers } = await import(url('features/editor/trackStack.js'))

const cases = JSON.parse(readFileSync(0, 'utf8'))
const out = cases.map(({ tracks, clips, heads }) => {
  const pos = (list) => list.map((c) => clips.indexOf(c))
  return {
    stack: [...stackLayers(tracks)],
    layers: heads.map((h) => pos(layersAt(h, clips, tracks))),
    videos: heads.map((h) => pos(videosAt(h, clips, tracks))),
  }
})
process.stdout.write(JSON.stringify(out))
