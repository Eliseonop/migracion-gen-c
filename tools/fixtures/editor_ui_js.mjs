// Ejecuta las funciones REALES de la timeline del editor de video-yt (editorModel.js) con Node (≥ 22).
//
//   node tools/fixtures/editor_ui_js.mjs <video-yt>/frontend/src < casos.json > salida.json
//
// Entrada: [{tracks, clips, markers, geo, calls: [[fn, ...args]]}]: `geo` son las filas en pantalla.; los clips se nombran por id en los argumentos.
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
const align = await load('features/editor/timelineAlign.js')
const drop = await load('features/editor/dropIntent.js')
const magnetic = await load('features/editor/magnetic.js')
const noOverlap = await load('features/editor/noOverlap.js')
const beats = await load('lib/beats.js')
const kf = await load('lib/clipKeyframes.js')
const layout = await load('lib/clipLayout.js')
const textstyles = await load('lib/textstyles.js')
const karaoke = await load('lib/textKaraoke.js')
const shapes = await load('lib/shapes.js')
const themes = await load('lib/subtitleThemes.js')

// applyStaticPose vive dentro de VideoEditor.jsx (no se exporta): se extrae su texto y se evalúa con
// lo que usa. Es JS sin JSX.
function extractFunction(file, name) {
  const src = readFileSync(file, 'utf8')
  const start = src.indexOf(`function ${name}(`)
  let depth = 0
  for (let i = src.indexOf('{', start); i < src.length; i++) {
    if (src[i] === '{') depth++
    else if (src[i] === '}' && --depth === 0) return src.slice(start, i + 1)
  }
  throw new Error(`no encuentro ${name}`)
}
const applyStaticPose = new Function('clampVolume', 'AUDIO_FX_KEYS', 'newTransform', 'newReframe',
  `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'applyStaticPose')}`)(
  kf.clampVolume, kf.AUDIO_FX_KEYS, layout.newTransform, m.newReframe)
const extract = await load('features/editor/clipExtract.js')
const dragPayload = new Function('IMAGE_DEFAULT_DUR', 'materialDuration', 'segmentRange', 'hasFaceTrack',
  `return ${extractFunction(`${root}/features/editor/MaterialClipGrid.jsx`, 'dragPayload')}`)(
  m.IMAGE_DEFAULT_DUR, extract.materialDuration, extract.segmentRange, extract.hasFaceTrack)
// placeAsset de VideoEditor.jsx con su setClips capturado (solo vídeo, imagen y audio).
const placeAsset = (payload, trackId, start, fitDur) => {
  let made = null
  const setClips = (fn) => { made = fn([])[0] }
  const fn = new Function('makeClip', 'isGeneratedDurationClip', 'clipDur', 'clipSpeed', 'setClips', 'setSelClipId',
    'setSelClipIds', 'chromaBg', `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'placeAsset')}`)(
    m.makeClip, m.isGeneratedDurationClip, m.clipDur, m.clipSpeed, setClips, () => {}, () => {}, () => ({}))
  fn(payload, trackId, start, fitDur)
  return made
}
// newTextStyle y changeStyle de VideoEditor.jsx con sus refs de React como argumentos.
const newTextStyle = (trackStyle) => new Function('tracksRef', 'defaultTextStyle', 'subtitleStyle', 'applyThemeToStyle',
  `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'newTextStyle')}`)(
  { current: [{ id: 'T', style: trackStyle ?? undefined }] }, textstyles.defaultTextStyle, textstyles.subtitleStyle,
  karaoke.applyThemeToStyle)('T')
const changeStyle = (clip, patch, t, fps, fixed) => {
  let made = clip
  const setClips = (fn) => { made = fn([clip])[0] }
  new Function('setClips', 'selIdsRef', 'patchClipsStyle', 'localTOf', 'flattenPatch', 'shouldKeyframe', 'upsertKf',
    'textStyleKfKeys', 'upsertStyleKf', 'fpsRef', 'markKf',
    `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'changeStyle')}`)(
    setClips, { current: [clip.id] }, m.patchClipsStyle, () => Math.max(0, t), kf.flattenPatch, kf.shouldKeyframe,
    (c, tt, p, i) => kf.upsertKeyframeAt(c, tt, p, i, fps), kf.textStyleKfKeys, kf.upsertStyleKf, { current: fps },
    () => {})(clip.id, patch, fixed ? { fixed: true } : undefined)
  return made
}
const changeShape = (clip, patch, t, fps) => {
  let made = clip
  const setClips = (fn) => { made = fn([clip])[0] }
  new Function('setClips', 'selIdsRef', 'flattenPatch', 'shouldKeyframe', 'localTOf', 'upsertKf', 'markKf',
    `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'changeShape')}`)(
    setClips, { current: [clip.id] }, kf.flattenPatch, kf.shouldKeyframe, () => Math.max(0, t),
    (c, tt, p, i) => kf.upsertKeyframeAt(c, tt, p, i, fps), () => {})(clip.id, patch)
  return made
}
// Sin ids (salen de la hora): el del clip y los de los keyframes del encuadre.
const clipNoIds = (c) => {
  if (!c) return c
  const { id, ...rest } = c
  if (rest.reframe) {
    const strip = (arr) => (arr || []).map(({ id: _, ...k }) => k)
    rest.reframe = { ...rest.reframe, keyframes: strip(rest.reframe.keyframes), keyframes2: strip(rest.reframe.keyframes2) }
  }
  return rest
}

// toggleKeyframeAtPlayhead de VideoEditor.jsx para un clip (`localT` = cabezal − inicio).
const toggleKf = (c, localT, fps) => {
  if (!kf.canKeyframe(c)) return c
  const t = Math.min(m.clipDur(c), Math.max(0, localT))
  const st = kf.kfState(c, t, fps)
  const id = kf.keyframeIdAt(c, t, fps)
  if (st === 'on') {
    if (c.kind === 'text' && kf.textStyleKfKeys(c).size) return kf.removePoseKf(c, id)
    if (c.keyframes?.enabled) return kf.deleteKeyframeItem(c, id)
    if (!c.reframe) return c
    const keep = (arr) => (arr || []).filter((k) => k.id !== id)
    return { ...c, reframe: { ...c.reframe, keyframes: keep(c.reframe.keyframes), keyframes2: keep(c.reframe.keyframes2) } }
  }
  const src = Math.min(c.out_point, Math.max(c.in_point, m.timelineToSource(c, c.start + localT)))
  return kf.upsertKeyframeAt(kf.enableKeyframes(c, t, src), t, {}, undefined, fps)
}

// commitPose de VideoEditor.jsx para un clip (sus refs y la selección aparte).
const commitPose = (c, patch, localT, fps) => {
  if (!kf.canKeyframe(c)) return c
  const next = applyStaticPose(c, patch)
  return kf.shouldKeyframe(c, patch) ? kf.upsertKeyframeAt(next, Math.max(0, localT), patch, undefined, fps) : next
}
// Sin los ids de los keyframes (salen de la hora).
const noIds = (c) => ({ ...c, keyframes: c.keyframes && { ...c.keyframes, items: (c.keyframes.items || []).map(({ id, ...k }) => k) } })
const poseView = (c) => {
  const { keyframes, style, shape, transform, opacity, volume, audio_fx, frame, reframe } = noIds(c)
  return { keyframes: keyframes ?? null, style: style ?? null, shape: shape ?? null, transform: transform ?? null,
    opacity: opacity ?? null, volume: volume ?? null, audio_fx: audio_fx ?? null, frame: frame ?? null,
    zoom: reframe?.zoom ?? null }
}

const inf = (x) => (x === Infinity ? 'inf' : x)
const cases = JSON.parse(readFileSync(0, 'utf8'))
const out = cases.map(({ tracks, clips, markers, geo, calls }) => {
  const byId = (id) => clips.find((c) => c.id === id)
  const others = (exclude, beatsToo) => [...align.alignOthers(clips, exclude), ...beats.snapTargets(markers, clips, beatsToo)]
  const zone = (z) => z && { trackId: z.trackId ?? null, slot: z.slot ?? null, lineY: z.lineY ?? 0 }
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
    alignThresholdSec: (pps) => align.alignThresholdSec(pps),
    clipBeatTimes: (id) => beats.clipBeatTimes(byId(id)),
    snapTargets: (exclude) => beats.snapTargets(markers, clips, exclude).map((t) => [t.trackId, t.start, t.end]),
    timelineAlignHits: (ids, threshold, which) =>
      align.timelineAlignHits(ids.map((i) => align.asAlignClip(byId(i))), others(ids, ids), threshold, which),
    nearestAlignTime: (t, track, threshold) => align.nearestAlignTime(t, track, others([], []), threshold),
    snapClipMove: (id, delta, threshold, track) => align.snapClipMove(byId(id), delta, others([id], [id]), threshold, track),
    snapClipTrim: (id, mode, delta, threshold) => align.snapClipTrim(byId(id), mode, delta, others([id], []), threshold),
    snapClipGroup: (ids, delta, threshold) => align.snapClipGroup(ids.map(byId), delta, others(ids, ids), threshold),
    clampGroupDelta: (ids, delta) => noOverlap.clampGroupDelta(clips, ids.map(byId), delta, tracks),
    clipAtTime: (track, t, exclude) => drop.clipAtTime(clips, track, t, exclude)?.id ?? null,
    clipCollidingWith: (track, start, dur, exclude) => drop.clipCollidingWith(clips, track, start, dur, exclude)?.id ?? null,
    zoneAt: (kind, y, originId, originAlone) => zone(drop.zoneAt(tracks, geo, kind, y, { originId, originAlone })),
    placeOnTrack: (track, desired, dur, exclude, stickSec, snapSec) =>
      drop.placeOnTrack(clips, track, desired, dur, { excludeIds: exclude, stickSec, snapSec })?.start ?? null,
    liftTarget: (from, start, dur, exclude) => {
      const l = drop.liftTarget(tracks, clips, from, start, dur, exclude)
      return l && { trackId: l.trackId ?? null, slot: l.slot ?? null }
    },
    magneticCut: (track, t, exclude) => magnetic.magneticCut(clips, track, t, exclude),
    dropTarget: (q) => {
      const d = drop.dropTarget({ tracks, clips, geo, ...q })
      return d && { trackId: d.trackId ?? null, slot: d.slot ?? null, lineY: d.lineY ?? 0, start: d.start,
        replaceId: d.replaceId ?? null, insert: !!d.insert, lifted: !!d.lifted }
    },
    newTrackIndex: (kind, side) => drop.newTrackIndex(tracks, kind, side),
    slotBoundary: (slot) => drop.slotBoundary(tracks, slot),
    slotIndex: (slot) => drop.slotIndex(tracks, slot),
    normalizeMarkers: (list) => beats.normalizeMarkers(list).map((x) => [x.t, x.label ?? null, x.color ?? null]),
    toggleMarkerAt: (t, fps) => beats.toggleMarkerAt(markers, t, fps).map((x) => x.t),
    nextSnapTime: (t, dir) => beats.nextSnapTime(markers, clips, t, dir),
    poseKeyframed: (id) => kf.poseKeyframed(byId(id)),
    shouldKeyframe: (id, patch) => kf.shouldKeyframe(byId(id), patch),
    upsertKeyframeAt: (id, t, patch, interp, fps) => noIds(kf.upsertKeyframeAt(byId(id), t, patch, interp ?? undefined, fps)).keyframes,
    applyStaticPose: (id, patch) => poseView(applyStaticPose(byId(id), patch)),
    commitPose: (id, patch, t, fps) => poseView(commitPose(byId(id), patch, t, fps)),
    dragPayload: (assetKind, item) => JSON.parse(dragPayload(assetKind, item)),
    makeClip: (assetKind, item, track, start, dur) => clipNoIds(m.makeClip(assetKind, item, track, start, dur ?? undefined)),
    placeAsset: (payload, track, start, fit) => clipNoIds(placeAsset(payload, track, start, fit ?? undefined)),
    segmentRange: (item) => extract.segmentRange(item),
    materialDuration: (item) => extract.materialDuration(item),
    kfState: (id, t, fps) => kf.kfState(byId(id), t, fps),
    keyframeIdAt: (id, t, fps) => kf.keyframeIdAt(byId(id), t, fps) ?? null,
    textStyleKfKeys: (id) => [...kf.textStyleKfKeys(byId(id))],
    toggleKeyframe: (id, t, fps) => {
      const c = noIds(toggleKf(byId(id), t, fps))
      return { keyframes: c.keyframes ?? null, reframeKfs: (c.reframe?.keyframes || []).map((k) => k.t) }
    },
    applyVolumeFade: (id, side) => noIds(kf.applyVolumeFade(byId(id), m.clipDur(byId(id)), side)).keyframes,
    frameOf: (id) => layout.frameOf(byId(id)),
    slotAspectOf: (id, a) => layout.slotAspectOf(byId(id), a),
    toCapcutPos: (x, y, w, h) => layout.toCapcutPos(x, y, w, h),
    fromCapcutPos: (X, Y, w, h) => layout.fromCapcutPos(X, Y, w, h),
    frameRectOf: (cw, ch, z, a) => layout.frameRectOf(cw, ch, z ?? undefined, a ?? undefined),
    destRectOnFrame: (tr, px, w, h, frame) => layout.destRectOnFrame(tr, px, w, h, frame),
    sourceCropPx: (crop, w, h) => layout.sourceCropPx(crop, w, h),
    clampCrop: (cx, cy, wf, hf) => layout.clampCrop(cx, cy, wf, hf),
    cropSizeFromCorner: (nx, ny, cx, cy) => layout.cropSizeFromCorner(nx, ny, cx, cy),
    cropHandleAt: (nx, ny, crop, rect, px) => layout.cropHandleAt(nx, ny, crop, rect, px),
    insideCrop: (nx, ny, crop) => layout.insideCrop(nx, ny, crop),
    resizeCropFree: (start, hx, hy, nx, ny) => layout.resizeCropFree(start, hx, hy, nx, ny),
    resizeCropLocked: (start, hx, hy, nx, ny, r) => layout.resizeCropLocked(start, hx, hy, nx, ny, r),
    cropRatioNorm: (id, src, out) => layout.cropRatioNorm(id, src, out),
    fitCropRatio: (crop, rn) => layout.fitCropRatio(crop, rn),
    cropCursor: (hx, hy) => layout.cropCursor(hx, hy),
    freeFrameAt: (id, t, sw, sh, ow, oh) => layout.freeFrameAt(byId(id), t, sw, sh, ow, oh),
    hitTransformHandle: (px, py, dest) => layout.hitTransformHandle(px, py, dest),
    defaultShape: (type) => shapes.defaultShape(type ?? undefined),
    makeShapeClip: (track, start, dur, preset) => {
      const { id, ...rest } = m.makeShapeClip(track, start, dur ?? undefined, preset ?? undefined)
      return rest
    },
    makeAdjustmentClip: (track, start, dur) => {
      const { id, ...rest } = m.makeAdjustmentClip(track, start, dur ?? undefined)
      return rest
    },
    copyKeyframeAt: (id, t, fps) => kf.copyKeyframeAt(byId(id), t, fps),
    pasteKeyframeAt: (id, t, board, groups, fps) => noIds(kf.pasteKeyframeAt(byId(id), t, board, groups ?? undefined, fps)).keyframes ?? null,
    duplicateKeyframeAt: (id, kfId, target, fps) => noIds(kf.duplicateKeyframeAt(byId(id), kfId, target, fps)).keyframes ?? null,
    changeShape: (id, patch, t, fps) => {
      const c = noIds(changeShape(byId(id), patch, t, fps))
      return { shape: c.shape ?? null, keyframes: c.keyframes ?? null }
    },
    subtitleThemes: () => themes.SUBTITLE_THEMES,
    textLooks: () => textstyles.TEXT_LOOKS,
    applyThemeToStyle: (cur, id) => karaoke.applyThemeToStyle(cur, themes.themeById(id)),
    clearTextTheme: (cur) => textstyles.clearTextTheme(cur),
    selectedSubtitleThemeId: (st) => textstyles.selectedSubtitleThemeId(st),
    applyOrClearTheme: (cur, id) => textstyles.applyOrClearTheme(cur, id ? themes.themeById(id) : null),
    lookPatch: (id) => textstyles.lookPatch(textstyles.TEXT_LOOKS.find((l) => l.id === id) || null),
    activeLookId: (st) => textstyles.activeLookId(st),
    defaultTextStyle: () => textstyles.defaultTextStyle(),
    subtitleStyle: () => textstyles.subtitleStyle(),
    newTextStyle: (trackStyle) => newTextStyle(trackStyle),
    makeTextClip: (track, start, dur, text, style) => {
      const { id, asset_id, ...rest } = m.makeTextClip(track, start, dur, text, style ?? undefined)
      return rest
    },
    newTextDuration: (playhead) => {
      const end = clips.reduce((acc, c) => Math.max(acc, m.clipEnd(c)), 0)
      return Math.max(3, +(end - playhead).toFixed(3))
    },
    upsertStyleKf: (id, t, patch, fps) => noIds(kf.upsertStyleKf(byId(id), t, patch, fps)).keyframes ?? null,
    changeStyle: (id, patch, t, fps, fixed) => {
      const c = noIds(changeStyle(byId(id), patch, t, fps, fixed))
      return { style: c.style ?? null, keyframes: c.keyframes ?? null }
    },
    applyAudioSpeedToLinkedText: (audioId, speed) => m.applyAudioSpeedToLinkedText(clips, tracks, byId(audioId), speed)
      .filter((c, i) => c !== clips[i]).map((c) => [c.id, c.start, c.out_point, c.words]),
    emptiedTracks: (candidates) => m.emptiedTracks(tracks, clips, candidates),
    groupMoveFromOrig: (origs, delta) => {
      const moved = m.groupMoveFromOrig(origs.map((o) => ({ ...o })), origs, delta)
      return moved.map((c) => c.start)
    },
  }
  return calls.map(([fn, ...args]) => run[fn](...args))
})
process.stdout.write(JSON.stringify(out))
