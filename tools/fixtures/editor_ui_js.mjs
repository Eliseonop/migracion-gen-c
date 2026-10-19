// Ejecuta las funciones REALES de la timeline del editor de video-yt (editorModel.js) con Node (≥ 22).
//
//   node tools/fixtures/editor_ui_js.mjs <video-yt>/frontend/src < casos.json > salida.json
//
// Entrada: [{tracks, clips, calls: [[fn, ...args]]}]; los clips se nombran por id en los argumentos.
// Salida: por caso, el resultado de cada llamada (Infinity como "inf").
// Lo llama tools/fixtures/export_fixtures.py; video-yt solo se lee.
import { readFileSync } from 'node:fs'
import { pathToFileURL } from 'node:url'

const root = process.argv[2].replace(/[\\/]+$/, '')
const load = (rel) => import(pathToFileURL(`${root}/${rel}`).href)
const m = await load('features/editor/editorModel.js')
const scale = await load('features/editor/timelineScale.js')
const rows = await load('features/editor/trackRows.js')
const stack = await load('features/editor/trackStack.js')

const inf = (x) => (x === Infinity ? 'inf' : x)
const cases = JSON.parse(readFileSync(0, 'utf8'))
const out = cases.map(({ tracks, clips, calls }) => {
  const byId = (id) => clips.find((c) => c.id === id)
  const run = {
    toFixed: (x) => +x.toFixed(3),
    clipDur: (id) => m.clipDur(byId(id)),
    clipsOnTrackSorted: (track) => m.clipsOnTrackSorted(clips, track).map((c) => c.id),
    freeStartOnTrack: (track, desired, dur, exclude) => m.freeStartOnTrack(clips, track, desired, dur, { excludeIds: exclude }),
    clampStartNoOverlap: (track, desired, dur, exclude) => m.clampStartNoOverlap(clips, track, desired, dur, { excludeIds: exclude }),
    trimBounds: (id) => { const b = m.trimBounds(clips, byId(id)); return { left: b.left, right: inf(b.right) } },
    clampTrimDelta: (id, mode, delta) => m.clampTrimDelta(clips, byId(id), mode, delta),
    trimClipPatch: (id, mode, delta) => m.trimClipPatch(byId(id), mode, delta),
    rangeSelectOnTrack: (track, from, to) => m.rangeSelectOnTrack(clips, track, from, to),
    nextClipSelection: (ids, anchor, clicked, mods) => m.nextClipSelection(clips, ids, anchor, clicked, mods),
    maxPps: (fps, duration) => scale.maxPps(fps, duration),
    minPps: (duration, viewW) => scale.minPps(duration, viewW),
    clampPps: (pps, duration, viewW, fps) => scale.clampPps(pps, duration, viewW, fps),
    zoomByDrag: (pps, dx, duration, viewW, fps) => scale.zoomByDrag(pps, dx, duration, viewW, fps),
    tickSteps: (fps) => scale.tickSteps(fps),
    tickStep: (pps, fps, dense) => scale.tickStep(pps, fps, dense),
    fmtRuler: (t, opts) => scale.fmtRuler(t, opts),
    buildTicks: (maxT, pps, opts) => scale.buildTicks(maxT, pps, opts).map((k) => [k.t, k.major, k.step]),
    clampRowHeight: (raw) => rows.clampRowHeight(raw),
    trackRowHeight: (kind, rowH) => rows.trackRowHeight(kind, rowH),
    stepRowHeight: (rowH, dy) => rows.stepRowHeight(rowH, dy),
    displayTracks: () => stack.displayTracks(tracks).map((t) => t.id),
    groupMoveFromOrig: (origs, delta) => {
      const moved = m.groupMoveFromOrig(origs.map((o) => ({ ...o })), origs, delta)
      return moved.map((c) => c.start)
    },
  }
  return calls.map(([fn, ...args]) => run[fn](...args))
})
process.stdout.write(JSON.stringify(out))
