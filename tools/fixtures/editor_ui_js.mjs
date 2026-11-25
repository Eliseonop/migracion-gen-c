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
const recipes = await load('features/editor/recipeReady.js')
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
const fxRuntime = await load('fx/core/runtime.js')
const maskLib = await load('lib/clipMask.js')
const selection = await load('lib/selectionFrame.js')
const favorites = await load('lib/favorites.js')
const sfxName = await load('lib/sfxName.js')
const projectFps = await load('lib/projectFps.js')
const projectFormat = await load('lib/projectFormat.js')
const panning = await load('lib/panning.js')
const scriptPrompt = await load('features/audio/scriptPrompt.js')
const bgLib = await load('lib/clipBg.js')
const ytQuality = await load('features/editor/ytQuality.js')
const clipFxLib = await load('lib/clipFx.js')
const clipAnim = await load('lib/clipAnim.js')
const clipExtract = await load('features/editor/clipExtract.js')
const motionTarget = await load('features/editor/motionTarget.js')
// confirmCrop de VideoEditor.jsx (con changeReframe, upsertKeyframe, changeTransform y srcTOf) sobre un clip.
const confirmCrop = (clip, crop, rotation, t, fps) => {
  let made = clip
  let seq = 0
  const setClips = (fn) => { made = fn([made])[0] }
  const src = `${root}/features/editor/VideoEditor.jsx`
  const deps = { setClips, clipsRef: { get current() { return [made] } }, cropClipId: clip.id, setCropClipId: () => {},
    newReframe: m.newReframe, keyframesOn: kf.keyframesOn, uid: (p) => `${p}${++seq}`, newTransform: layout.newTransform,
    shouldKeyframe: kf.shouldKeyframe, localTOf: () => Math.max(0, t), upsertKf: (c, tt, p, i) => kf.upsertKeyframeAt(c, tt, p, i, fps),
    flattenPatch: kf.flattenPatch, markKf: () => {}, snapToFrame: projectFps.snapToFrame, kfSnap: projectFps.kfSnap,
    clamp: (v, lo, hi) => Math.min(hi, Math.max(lo, v)), fpsRef: { current: fps }, pendingKfSel: { current: null },
    applyStaticPose, playheadRef: { current: clip.start + t }, timelineToSource: m.timelineToSource }
  const body = ['changeReframe', 'upsertKeyframe', 'changeTransform', 'srcTOf', 'confirmCrop'].map((n) => extractFunction(src, n)).join('\n')
  new Function(...Object.keys(deps), `${body}\nreturn confirmCrop`)(...Object.values(deps))({ crop, rotation })
  return made
}
// startMaskDrag y startMaskBrush de interactions.js con el puntero de mentira: el primer punto empieza el
// arrastre y cada uno de los demás es un pointermove. Dan el último parche (commitMask o changeMask).
const maskPointer = (name, start, moves) => {
  let move = null
  let out = null
  const deps = { listenMove: (fn) => { move = fn }, canvasPointer: (e) => e, clamp: panning.clamp,
    maskHandleBox: maskLib.maskHandleBox, toMaskLocal: maskLib.toMaskLocal, MASK_FEATHER_MAX: maskLib.MASK_FEATHER_MAX }
  const fn = new Function(...Object.keys(deps), `return ${extractFunction(`${root}/features/editor/interactions.js`, name)}`)(
    ...Object.values(deps))
  const ctx = { commitMask: (_, patch) => { out = patch }, changeMask: (_, patch) => { out = patch },
    playingRef: { current: false } }
  start(fn, ctx)
  for (const p of moves) move(p)
  return out
}
const maskDragPatch = (mode, mask, frame, p0, p) =>
  maskPointer('startMaskDrag', (fn, ctx) => fn(p0, null, { id: 'c' }, mask, mode, ctx, frame), [p])
const maskBrushStroke = (mask, frame, pts) =>
  maskPointer('startMaskBrush', (fn, ctx) => fn(pts[0], null, { id: 'c' }, mask, ctx, frame), pts.slice(1))?.brush?.points ?? null
// maskCursor de interactions.js sobre una máscara ya activa (el cursor de girar, «rotate»; ninguno, «»).
const maskCursor = (px, py, mask, frame, drawing) => {
  const src = `${root}/features/editor/interactions.js`
  const handleOf = /const MASK_HANDLE_OF = (\{[^}]*\})/.exec(readFileSync(src, 'utf8'))[1]
  const deps = { activeMask: () => ({ mask }), frameRectOf: () => frame, maskHitMode: maskLib.maskHitMode,
    maskHandleBox: maskLib.maskHandleBox, ROTATE_CURSOR: selection.ROTATE_CURSOR, handleCursor: selection.handleCursor,
    MASK_HANDLE_OF: new Function(`return ${handleOf}`)() }
  const fn = new Function(...Object.keys(deps), `return ${extractFunction(src, 'maskCursor')}`)(...Object.values(deps))
  const got = fn(px, py, { width: 0, height: 0 }, { maskDrawRef: { current: drawing }, viewZoomRef: { current: 1 } })
  return got === selection.ROTATE_CURSOR ? 'rotate' : (got ?? '')
}
// applyFreeLayout de VideoEditor.jsx (con needsFreeLayout y wantsBaseFit) sobre un clip, con un medio de
// srcW × srcH, la salida oW × oH y el cabezal en `playhead`. null si el clip no cambia.
const freeLayout = (clip, srcW, srcH, oW, oH, playhead) => {
  let made = clip
  const setClips = (fn) => { made = fn([made])[0] }
  const src = `${root}/features/editor/VideoEditor.jsx`
  const needsFreeLayout = new Function('isVisualClip', 'isOverlay', `return ${extractFunction(src, 'needsFreeLayout')}`)(
    m.isVisualClip, layout.isOverlay)
  const deps = { setClips, mediaSize: layout.mediaSize, outRef: { current: { w: oW, h: oH } }, needsFreeLayout,
    isOverlay: layout.isOverlay, newReframe: m.newReframe, clamp: panning.clamp, timelineToSource: m.timelineToSource,
    playheadRef: { current: playhead }, cropWindow: layout.cropWindow, sourceCropPx: layout.sourceCropPx }
  const body = ['wantsBaseFit', 'applyFreeLayout'].map((n) => extractFunction(src, n)).join('\n')
  new Function(...Object.keys(deps), `${body}\nreturn applyFreeLayout`)(...Object.values(deps))(
    clip, { videoWidth: srcW, videoHeight: srcH })
  if (made === clip) return null
  return { layout: made.layout ?? null, frame: made.frame ?? null, reframe: made.reframe ?? null,
    transform: made.transform ?? null }
}
// Máscaras de VideoEditor.jsx (addMask, removeMask, duplicateMask, changeMask y commitMask) sobre un clip.
const maskOps = (clip, aspect, fps, t) => {
  let made = clip
  const setClips = (fn) => { made = fn([made])[0] }
  const src = `${root}/features/editor/VideoEditor.jsx`
  const deps = { setClips, clipMasks: maskLib.clipMasks, defaultMask: maskLib.defaultMask, normalizeMask: maskLib.normalizeMask,
    maskId: maskLib.maskId, MASK_KF_KEYS: maskLib.MASK_KF_KEYS, outAspect: aspect, setMaskMode: () => {}, setMaskDraw: () => {},
    shouldKeyframe: kf.shouldKeyframe, localTOf: () => Math.max(0, t), upsertKf: (c, tt, p, i) => kf.upsertKeyframeAt(c, tt, p, i, fps),
    markKf: () => {} }
  const names = ['patchFirstMask', 'retypeMask', 'dropMaskSizeKfs', 'applyStaticMask']
  const body = names.map((n) => extractFunction(src, n)).join('\n')
  const make = (name) => new Function(...Object.keys(deps), 'selectedClip',
    `${body}\n${extractFunction(src, name)}\nreturn ${name}`)(...Object.values(deps), clip)
  return { run: (name, ...args) => { make(name)(...args); return made } }
}
const masksOf = (c) => (c.masks || []).map(({ id, ...m }) => m)
const noUid = (e) => { if (!e) return null; const { uid, ...rest } = e; return rest }

// «Eliminar fondo» de VideoEditor.jsx (patchBg, patchBgAuto, bgClipDescriptor, startBgAutoFor, toggleBgAuto y
// changeBgAuto) sobre un clip: el bg_removal con el que queda y el cuerpo del POST /bg-removal.
let bgCatalog = []
const bgOps = (clip) => {
  let made = clip
  let body = null
  const setClips = (fn) => { made = fn([made])[0] }
  const src = `${root}/features/editor/VideoEditor.jsx`
  const deps = { setClips, clipBg: bgLib.clipBg, defaultBg: bgLib.defaultBg, normalizeBg: bgLib.normalizeBg,
    isInteractiveProvider: bgLib.isInteractiveProvider, DEFAULT_PROVIDER: bgLib.DEFAULT_PROVIDER,
    recommendedProvider: bgLib.recommendedProvider, providerMaskHeight: bgLib.providerMaskHeight,
    bgCapable: bgLib.bgCapable, resetCutout: () => {}, stopBgAnalyze: () => {}, setBgBrush: () => {},
    setBgNotice: () => {}, setBgJob: () => {}, setChromaPick: () => {}, project: { id: 'p' },
    createBgRemovalJob: (_pid, b) => { body = b; return new Promise(() => {}) },
    autoActive: bgLib.autoActive, setCutoutJob: () => {}, useCallback: (f) => f, onBgBrush: () => {},
    preferredSamProvider: bgLib.preferredSamProvider, DEFAULT_MASK_HEIGHT: bgLib.DEFAULT_MASK_HEIGHT, get bgInfo() { return { providers: bgCatalog } },
    createBgCutoutJob: (_pid, b) => { body = b; return new Promise(() => {}) } }
  const fns = ['patchBg', 'patchBgAuto', 'patchBgChroma', 'patchBgOutline', 'bgClipDescriptor', 'startBgAutoFor', 'exportBgCutout', 'undoBgEdit', 'clearBgEdits'].map((n) => extractFunction(src, n))
  const arrows = ['toggleBgAuto', 'changeBgAuto', 'toggleBgChroma', 'changeBgChroma', 'resetBgChroma', 'toggleBgCustom'].map((n) => extractArrow(src, n))
  const hooks = ['addBgStroke', 'extendBgStroke'].map((n) => extractCallback(src, n))
  const api = new Function(...Object.keys(deps), 'selectedClip',
    [...fns, ...arrows, ...hooks, 'return { patchBgAuto, patchBgOutline, exportBgCutout, undoBgEdit, clearBgEdits, toggleBgCustom, addBgStroke, extendBgStroke, startBgAutoFor, toggleBgAuto, changeBgAuto, toggleBgChroma, changeBgChroma, resetBgChroma }'].join(String.fromCharCode(10)))(
    ...Object.values(deps), clip)
  return { api, get bg() { return made.bg_removal ?? null }, get body() { return body } }
}
// El final del job (el useEffect de bgJob.status de VideoEditor.jsx), con su patchBgAuto.
const bgFinished = (clip, job) => {
  const ops = bgOps(clip)
  if (job.status === 'done' && job.bg_removal) {
    const r = job.bg_removal
    ops.api.patchBgAuto(clip.id, { enabled: true, status: 'ready', error: null, base_key: r.base_key,
      provider: r.provider, model_version: r.model_version, mask_fps: r.mask_fps, mask_height: r.mask_height })
  } else if (job.status === 'error') {
    ops.api.patchBgAuto(clip.id, { status: 'error', error: job.error || 'Error' })
  } else {
    return null
  }
  return ops.bg
}
// startBgBrush de interactions.js con el puntero de mentira: el trazo y los puntos que van añadiéndose.
const bgDrag = (clip, brush, head, pts) => {
  let stroke = null
  const extended = []
  let move = null
  const deps = { canvasPointer: (e) => e, canvasToSourceNorm: (_c, x, y) => ({ x, y }), listenMove: (fn) => { move = fn },
    clipBg: bgLib.clipBg, isInteractiveProvider: bgLib.isInteractiveProvider, clamp: panning.clamp, timelineToSource: m.timelineToSource }
  const fn = new Function(...Object.keys(deps), `return ${extractFunction(`${root}/features/editor/interactions.js`, 'startBgBrush')}`)(...Object.values(deps))
  const ctx = { addBgStroke: (_id, s) => { stroke = s }, extendBgStroke: (_id, p) => extended.push(p), playingRef: { current: false },
    stopPlayback: () => {}, bgBrushRef: { current: brush }, playheadRef: { current: head }, onBgStroke: () => {} }
  fn(pts[0], null, clip, ctx, {})
  for (const p of pts.slice(1)) move(p)
  return { stroke, extended }
}
// canvasToSourceNorm y bgBrushRadiusPx de render/canvas.js, sobre un material de mentira y el cuadro como lienzo.
const bgGeo = (name, clip, args, vw, vh, oW, oH, head) => {
  const src = `${root}/features/editor/render/canvas.js`
  const deps = { mediaSize: layout.mediaSize, clamp: panning.clamp, timelineToSource: m.timelineToSource, clipFxAt: clipFxLib.clipFxAt, clipDur: m.clipDur,
    clipFlip: clipAnim.clipFlip, isOverlay: layout.isOverlay, clipRenderPose: clipAnim.clipRenderPose, cropWindow: layout.cropWindow, slotAspectOf: layout.slotAspectOf,
    sourceCropPx: layout.sourceCropPx, destRectOnFrame: layout.destRectOnFrame, posedRenderTransform: clipAnim.posedRenderTransform }
  const body = ['overlayDest', name].map((n) => extractFunction(src, n)).join('\n')
  const fn = new Function(...Object.keys(deps), `${body}\nreturn ${name}`)(...Object.values(deps))
  const el = { videoWidth: vw, videoHeight: vh, currentTime: m.timelineToSource(clip, head) }
  const env = { mediaEls: { current: new Map([[clip.id, el]]) }, outRef: { current: { w: oW, h: oH } } }
  return fn(clip, ...args, { x: 0, y: 0, w: oW, h: oH }, env, head)
}
// applyFaceTrackToClips de VideoEditor.jsx sobre un clip (sin elementos de medios: el objeto libre lo hace
// el editor al guardar). Lo que cambia, con los keyframes del encuadre sin ids.
const faceTrackClip = (clip, kfs, mode) => {
  let made = clip
  const setClips = (fn) => { made = fn([made])[0] }
  const deps = { setClips, isVisualClip: m.isVisualClip, applyFaceTrack: m.applyFaceTrack, newReframe: m.newReframe,
    mediaEls: { current: new Map() }, applyFreeLayout: () => {} }
  const fn = new Function(...Object.keys(deps), `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'applyFaceTrackToClips')}`)(...Object.values(deps))
  fn([clip.id], kfs, mode)
  const strip = (arr) => (arr || []).map(({ id: _, ...k }) => k)
  const rf = made.reframe ? { ...made.reframe, keyframes: strip(made.reframe.keyframes), keyframes2: strip(made.reframe.keyframes2) } : null
  return { layout: made.layout ?? null, frame: made.frame ?? null, reframe: rf, transform: made.transform ?? null }
}
// normalizeHistory y removeHistory de EdMaterial.jsx (la segunda, con la lista que se guarda en vez de guardarla).
const normalizeHistory = new Function(`return ${extractFunction(`${root}/features/editor/EdMaterial.jsx`, 'normalizeHistory')}`)()
const removeHistory = (list, item) => {
  let next = null
  const deps = { ytHistory: list, setYtHistory: (v) => { next = v }, putSettings: () => Promise.resolve() }
  new Function(...Object.keys(deps), `return async ${extractFunction(`${root}/features/editor/EdMaterial.jsx`, 'removeHistory')}`)(...Object.values(deps))(item)
  return next
}
// «Subir calidad» de EdMaterial.jsx: isYtUrl y canUpgradeFrom, y checkUpgrade y runUpgrade con la respuesta del
// backend de mentira ({throw: texto} lanza un Error con ese texto). Dan lo que piden y el aviso que sale.
const edMaterial = `${root}/features/editor/EdMaterial.jsx`
const materialMenu = await load('features/editor/materialMenu.js')
const isYtUrl = new Function(`return ${extractFunction(edMaterial, 'isYtUrl')}`)()
const canUpgradeFrom = new Function('isYtUrl', `return ${extractFunction(edMaterial, 'canUpgradeFrom')}`)(isYtUrl)
const checkUpgrade = async (item, info) => {
  let ask = null
  let toast = null
  let asked = null
  const deps = { materialIdent: materialMenu.materialIdent, usedUpgrades: ytQuality.usedUpgrades, project: { id: 'p' },
    getClipVersions: async (pid, ident) => { asked = ident; if (info && info.throw !== undefined) throw new Error(info.throw); return info },
    setUpgradeAsk: (v) => { ask = v }, setMatToast: (t) => { toast = t } }
  await new Function(...Object.keys(deps), `return async ${extractFunction(edMaterial, 'checkUpgrade')}`)(...Object.values(deps))(item)
  return { asked, ask, toast }
}
const runUpgrade = async (ask, res) => {
  let sent = null
  let toast = null
  let closed = false
  const deps = { upgradeAsk: ask, upgrading: false, setUpgrading: () => {}, usedUpgrades: ytQuality.usedUpgrades,
    onFlushTimeline: async () => {}, onReloadTimeline: async () => {}, onRefresh: () => {}, project: { id: 'p' },
    replaceClipVersions: async (pid, ident, olds) => { sent = { ident, olds }; if (res && res.throw !== undefined) throw new Error(res.throw); return res },
    setUpgradeAsk: (v) => { closed = v === null }, qualityShort: ytQuality.qualityShort, setMatToast: (t) => { toast = t } }
  await new Function(...Object.keys(deps), `return async ${extractFunction(edMaterial, 'runUpgrade')}`)(...Object.values(deps))()
  return { sent, closed, toast }
}
// Como extractArrow, para `const name = useCallback(…)`.
function extractCallback(file, name) {
  const src = readFileSync(file, 'utf8')
  const start = src.indexOf(`const ${name} = useCallback(`)
  if (start < 0) throw new Error(`no encuentro ${name}`)
  let depth = 0
  for (let i = src.indexOf('useCallback(', start) + 11; i < src.length; i++) {
    if (src[i] === '(') depth++
    else if (src[i] === ')' && --depth === 0) return src.slice(start, i + 1)
  }
  throw new Error(`no encuentro ${name}`)
}
// Como extractFunction, para `const name = (…) => {…}`.
function extractArrow(file, name) {
  const src = readFileSync(file, 'utf8')
  const start = src.indexOf(`const ${name} = (`)
  if (start < 0) throw new Error(`no encuentro ${name}`)
  let depth = 0
  for (let i = src.indexOf('=> {', start) + 3; i < src.length; i++) {
    if (src[i] === '{') depth++
    else if (src[i] === '}' && --depth === 0) return src.slice(start, i + 1)
  }
  throw new Error(`no encuentro ${name}`)
}

// applyStaticPose vive dentro de VideoEditor.jsx (no se exporta): se extrae su texto y se evalúa con
// lo que usa. Es JS sin JSX.
function extractFunction(file, name) {
  const src = readFileSync(file, 'utf8')
  const start = src.indexOf(`function ${name}(`)
  let depth = 0
  for (let i = src.indexOf(') {', start) + 2; i < src.length; i++) { // tras los parámetros (puede haber `x = {}`)
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
// placeAsset de VideoEditor.jsx con su setClips capturado (vídeo, imagen, audio, figuras y textos).
const placeAsset = (payload, trackId, start, fitDur, trackStyle) => {
  let made = null
  const setClips = (fn) => { made = fn([])[0] }
  const styleFor = new Function('tracksRef', 'defaultTextStyle', 'subtitleStyle', 'applyThemeToStyle',
    `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'newTextStyle')}`)(
    { current: [{ id: trackId, style: trackStyle ?? undefined }] }, textstyles.defaultTextStyle, textstyles.subtitleStyle,
    karaoke.applyThemeToStyle)
  const fn = new Function('makeClip', 'isGeneratedDurationClip', 'clipDur', 'clipSpeed', 'setClips', 'setSelClipId',
    'setSelClipIds', 'chromaBg', 'makeShapeClip', 'SHAPE_DEFAULT_DUR', 'themeById', 'TEXT_DEFAULT_DUR', 'DEFAULT_TEXT',
    'selectNewText', 'makeTextClip', 'newTextStyle',
    `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'placeAsset')}`)(
    m.makeClip, m.isGeneratedDurationClip, m.clipDur, m.clipSpeed, setClips, () => {}, () => {}, bgLib.chromaBg,
    m.makeShapeClip, shapes.SHAPE_DEFAULT_DUR, themes.themeById, m.TEXT_DEFAULT_DUR, m.DEFAULT_TEXT,
    (clip) => { made = clip }, m.makeTextClip, styleFor)
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
const moveKeyframe = (clip, kfId, newT, fps) => {
  let made = clip
  const setClips = (fn) => { made = fn([clip])[0] }
  new Function('setClips', 'patchKeyframe', 'fpsRef', 'seek', 'clipsRef', 'clips', 'sourceToTimeline',
    `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'moveKeyframe')}`)(
    setClips, kf.patchKeyframe, { current: fps }, () => {}, { current: [clip] }, [clip], m.sourceToTimeline)(clip.id, kfId, newT)
  return made
}
const toggleStyleKf = (clip, trackStyle, keys, t, fps) => {
  let made = clip
  const setClips = (fn) => { made = fn([clip])[0] }
  new Function('fpsRef', 'SHADOW_DEFAULTS', 'setClips', 'clamp', 'localTOf', 'clipDur', 'styleKfState', 'removeStyleKfAt',
    'tracksRef', 'withTextStyleKf', 'effectiveTextStyle', 'textStyleAt', 'upsertStyleKf', 'markKf',
    `return ${extractFunction(`${root}/features/editor/VideoEditor.jsx`, 'toggleStyleKf')}`)(
    { current: fps }, textstyles.SHADOW_DEFAULTS, setClips, (v, lo, hi) => Math.min(hi, Math.max(lo, v)), () => Math.max(0, t),
    m.clipDur, kf.styleKfState, kf.removeStyleKfAt, { current: [{ id: clip.track_id, style: trackStyle ?? undefined }] },
    kf.withTextStyleKf, textstyles.effectiveTextStyle, kf.textStyleAt, kf.upsertStyleKf, () => {})(clip.id, keys)
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
const out = await Promise.all(cases.map(async ({ tracks, clips, markers, geo, calls }) => {
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
    canLayerClip: (id) => m.canLayerClip(byId(id)),
    clipLayerInfo: (id) => m.clipLayerInfo(clips, id),
    moveClipLayer: (id, action) => m.moveClipLayer(clips, id, action).map((c) => c.id),
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
    placeText: (payload, track, start, fit, trackStyle) => {
      const { asset_id, ...rest } = clipNoIds(placeAsset(payload, track, start, fit ?? undefined, trackStyle))
      return rest
    },
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
    maskOp: (id, op, arg, aspect, t, fps) => {
      const ops = maskOps(byId(id), aspect, fps, t)
      const c = op === 'addMask' ? ops.run('addMask', arg, 'clip') : op === 'addAdjust' ? ops.run('addMask', arg, 'adjust')
        : op === 'changeMask' || op === 'commitMask' ? ops.run(op, id, arg) : ops.run(op)
      return { masks: masksOf(c), keyframes: noIds(c).keyframes ?? null }
    },
    freeLayout: (id, srcW, srcH, oW, oH, playhead) => freeLayout(byId(id), srcW, srcH, oW, oH, playhead),
    followTrackMaskKeys: (id, track, dims, gap) => layout.followTrackMaskKeys(byId(id), track, dims, gap ?? undefined),
    enableKeyframes: (id, t, src) => noIds(kf.enableKeyframes(byId(id), t, src)).keyframes ?? null,
    // El setClips de followMaskFace (VideoEditor.jsx): los keyframes encendidos en el primero y mx/my en cada uno.
    followMaskFace: (id, track, dims, src, fps) => {
      const c = byId(id)
      const keys = layout.followTrackMaskKeys(c, track, dims)
      if (!keys.length) return null
      let next = kf.keyframesOn(c) ? c : kf.enableKeyframes(c, keys[0].t, src)
      for (const k of keys) next = kf.upsertKeyframeAt(next, k.t, { mx: k.mx, my: k.my }, undefined, fps)
      return noIds(next).keyframes ?? null
    },
    bgToggleAuto: (id, on) => { const o = bgOps(byId(id)); o.api.toggleBgAuto(on); return o.bg },
    bgChangeAuto: (id, patch) => { const o = bgOps(byId(id)); o.api.changeBgAuto(patch); return o.bg },
    bgStart: (id) => {
      const o = bgOps(byId(id))
      o.api.startBgAutoFor(byId(id))
      return o.body ? { body: o.body, bg: o.bg } : null
    },
    bgFinished: (id, job) => bgFinished(byId(id), job),
    bgToggleCustom: (id, on, cat) => { bgCatalog = cat; const o = bgOps(byId(id)); o.api.toggleBgCustom(on); return o.bg },
    bgUndoEdit: (id) => { const o = bgOps(byId(id)); o.api.undoBgEdit(); return o.bg },
    bgClearEdits: (id) => { const o = bgOps(byId(id)); o.api.clearBgEdits(); return o.bg },
    bgDrag: (id, brush, head, pts) => bgDrag(byId(id), brush, head, pts),
    bgAddStroke: (id, s) => { const o = bgOps(byId(id)); o.api.addBgStroke(id, s); return o.bg },
    bgExtendStroke: (id, p) => { const o = bgOps(byId(id)); o.api.extendBgStroke(id, p); return o.bg },
    bgMarkFrames: (e, f) => bgLib.samMarkFrames(e, f ?? undefined),
    bgEditsAtFrame: (e, i, f) => bgLib.editsAtFrame(e, i, f ?? undefined),
    bgMatteTime: (i, f) => bgLib.matteFrameTime(i, f ?? undefined),
    bgMatteIndex: (t, f) => bgLib.matteFrameIndex(t ?? undefined, f ?? undefined),
    bgManualMark: (e) => bgLib.isManualMark(e),
    bgPreferredSam: (c, cat) => bgLib.preferredSamProvider(c, cat),
    bgToSource: (id, x, y, vw, vh, oW, oH, head) => {
      const p = bgGeo('canvasToSourceNorm', byId(id), [x, y], vw, vh, oW, oH, head)
      return p ? [+p.x.toFixed(6), +p.y.toFixed(6)] : null
    },
    bgBrushRadius: (id, size, vw, vh, oW, oH, head) => +bgGeo('bgBrushRadiusPx', byId(id), [size], vw, vh, oW, oH, head).toFixed(6),
    bgCutout: (id) => { const o = bgOps(byId(id)); o.api.exportBgCutout(); return o.body },
    bgToggleChroma: (id, on) => { const o = bgOps(byId(id)); o.api.toggleBgChroma(on); return o.bg },
    bgChangeChroma: (id, patch) => { const o = bgOps(byId(id)); o.api.changeBgChroma(patch); return o.bg },
    bgResetChroma: (id) => { const o = bgOps(byId(id)); o.api.resetBgChroma(); return o.bg },
    bgChangeOutline: (id, patch) => { const o = bgOps(byId(id)); o.api.patchBgOutline(id, patch); return o.bg },
    faceTrackClip: (id, kfs, mode) => faceTrackClip(byId(id), kfs, mode),
    applyFaceTrack: (rf, kfs, mode) => {
      const out = m.applyFaceTrack(rf ?? undefined, kfs ?? undefined, mode)
      const strip = (arr) => (arr || []).map(({ id: _, ...k }) => k)
      return { ...out, keyframes: strip(out.keyframes), keyframes2: strip(out.keyframes2) }
    },
    ytQualities: (r) => ytQuality.videoQualities(r),
    ytQualityShort: (q) => ytQuality.qualityShort(q),
    ytQualityTag: (q) => ytQuality.qualityTag(q),
    ytFmtBytes: (b) => ytQuality.fmtBytes(b ?? undefined),
    ytQualityMeta: (i) => ytQuality.qualityMeta(i),
    ytNormalizeHistory: (raw) => normalizeHistory(raw),
    ytRemoveHistory: (list, item) => removeHistory(list, item),
    ytIsUrl: (u) => isYtUrl(u),
    ytCanUpgradeFrom: (item) => canUpgradeFrom({ kind: 'clips', item }),
    ytUsedUpgrades: (list) => ytQuality.usedUpgrades(list),
    ytUpgradeMessage: (versions, q) => ytQuality.upgradeMessage(versions, q),
    ytCheckUpgrade: (item, info) => checkUpgrade(item, info),
    ytRunUpgrade: (ask, res) => runUpgrade(ask, res),
    buildScriptPrompt: (topic, clips) => scriptPrompt.buildScriptPrompt(topic, clips),
    canCaptionClip: (id) => m.canCaptionClip(byId(id)),
    sourceRefOf: (id) => m.sourceRefOf(byId(id)),
    textClipsFromTranscript: (id, segments, track, style, transcript, ref) =>
      m.textClipsFromTranscript(byId(id), segments, track, style ?? undefined, transcript, ref)
        .map(({ id: _, asset_id: __, ...rest }) => rest),
    maskGeometry: (mask, frame) => maskLib.maskGeometry(maskLib.normalizeMask(mask), frame),
    maskHandleBox: (mask, frame) => maskLib.maskHandleBox(maskLib.normalizeMask(mask), frame),
    maskHandles: (mask, frame) => maskLib.maskHandles(maskLib.normalizeMask(mask), frame),
    toMaskLocal: (px, py, mask, frame) => maskLib.toMaskLocal(px, py, maskLib.normalizeMask(mask), frame),
    maskHitMode: (px, py, mask, frame, pad) => maskLib.maskHitMode(px, py, maskLib.normalizeMask(mask), frame, pad ?? undefined) ?? null,
    maskDragPatch: (mode, mask, frame, p0, p) => maskDragPatch(mode, maskLib.normalizeMask(mask), frame, p0, p),
    maskBrushStroke: (mask, frame, pts) => maskBrushStroke(maskLib.normalizeMask(mask), frame, pts),
    maskCursor: (px, py, mask, frame, drawing) => maskCursor(px, py, maskLib.normalizeMask(mask), frame, drawing),
    resizeCursor: (deg) => selection.resizeCursor(deg),
    starPoints: (hw, hh, spikes, inner) => maskLib.starPoints(hw, hh, spikes ?? undefined, inner ?? undefined),
    heartPoints: (hw, hh, n) => maskLib.heartPoints(hw, hh, n ?? undefined),
    confirmCrop: (id, crop, rotation, t, fps) => {
      const c = confirmCrop(byId(id), crop, rotation, t, fps)
      const strip = (arr) => (arr || []).map(({ id: _, ...k }) => k)
      return { reframe: c.reframe ? { ...c.reframe, keyframes: strip(c.reframe.keyframes) } : null, transform: c.transform ?? null,
        frame: c.frame ?? null, keyframes: noIds(c).keyframes ?? null }
    },
    styleKfState: (id, t, keys, fps) => kf.styleKfState(byId(id), t, keys, fps),
    toggleStyleKf: (id, trackStyle, keys, t, fps) => noIds(toggleStyleKf(byId(id), trackStyle, keys, t, fps)).keyframes ?? null,
    pseudoWaveform: (seed, bars) => panning.pseudoWaveform(seed, bars ?? undefined),
    normalizeFavorites: (raw) => favorites.normalizeFavorites(raw),
    recipeCatalog: (list) => list,
    recipeReady: (recipe, kinds) => recipes.recipeReady(recipe, kinds.map((kind) => ({ kind }))),
    sfxNameFromFile: (name) => sfxName.sfxNameFromFile(name),
    realSfxCategories: (cats) => sfxName.realSfxCategories(cats),
    upsertSfxCategory: (cats, cat) => sfxName.upsertSfxCategory(cats, cat),
    matchSfxCategory: (text, cats) => sfxName.matchSfxCategory(text, cats),
    sfxDraftFromSaved: (sfx) => sfxName.sfxDraftFromSaved(sfx),
    sfxSavePayload: (row, cats) => sfxName.sfxSavePayload(row, cats),
    nameTextStyleFavorite: (st, existing) => favorites.nameTextStyleFavorite(st, existing),
    splitClipByMaxWords: (id, n) => m.splitClipByMaxWords(byId(id), n).map(({ id: _, asset_id: __, ...rest }) => rest),
    splitTrackText: (track, n) => m.splitTrackTextByMaxWords(clips, track, n).map((c) => [c.text ?? null, c.start, c.out_point]),
    normalizeVfxEntry: (raw) => noUid(fxRuntime.normalizeVfxEntry(raw)),
    keepsUid: (raw) => fxRuntime.normalizeVfxEntry(raw)?.uid ?? null,
    makeVfxEntry: (id) => noUid(fxRuntime.makeVfxEntry(id)),
    patchKeyframe: (id, kfId, patch, fps) => noIds(kf.patchKeyframe(byId(id), kfId, patch, fps)).keyframes ?? null,
    moveKeyframe: (id, kfId, newT, fps) => {
      const c = noIds(moveKeyframe(byId(id), kfId, newT, fps))
      return { keyframes: c.keyframes ?? null, reframe: (c.reframe?.keyframes || []).map((k) => [k.id ?? null, k.t]) }
    },
    targetInterpItem: (id, t, fps) => {
      const k = kf.targetInterpItem(byId(id), null, t, fps)
      return k ? [k.t, kf.interpBezier(k)] : null
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
    matchClipsToFirstDuration: (ids) => m.matchClipsToFirstDuration(clips, ids)
      .map((c) => [c.id, c.track_id, c.start, c.out_point, c.source_duration ?? 0]),
    overlapPairs: () => noOverlap.overlapPairs(clips),
    // separateClipOverlaps de VideoEditor.jsx, con su buildTrack (el id con un sufijo fijo, s0, s1…).
    separateOverlaps: () => {
      let k = 0
      const makeTrack = (kind, style, list) => {
        const prefix = kind === 'video' ? 'V' : kind === 'audio' ? 'A' : 'T'
        const nums = (list || []).filter((t) => t.kind === kind).map((t) => parseInt(String(t.name).replace(/\D/g, ''), 10) || 0)
        const n = (nums.length ? Math.max(...nums) : 0) + 1
        const nt = { id: `${prefix}${n}-s${k++}`, kind, name: `${prefix}${n}`, hidden: false, muted: false, locked: false, linked_track_id: null }
        if (kind === 'text') nt.style = style ? { ...style } : { sub: 1 }
        return nt
      }
      const out = noOverlap.separateOverlaps(clips, tracks, { makeTrack })
      return {
        clips: out.clips.map((c) => [c.id, c.track_id, c.start]),
        tracks: out.tracks.map((t) => [t.id, t.kind, t.name, t.style ?? null]),
        moved: out.moved,
      }
    },
    evenDim: (n) => projectFormat.evenDim(n),
    sizeForRatio: (rw, rh, side) => projectFormat.sizeForRatio(rw, rh, side),
    aspectOf: (w, h) => projectFormat.aspectOf(w, h),
    resolutionOf: (w, h) => projectFormat.resolutionOf(w, h),
    withAspect: (id, w, h) => projectFormat.withAspect(id, w, h),
    withResolution: (side, w, h) => projectFormat.withResolution(side, w, h),
    withMediaAspect: (mw, mh, w, h) => projectFormat.withMediaAspect(mw, mh, w, h),
    setMark: (mark, which, t) => motionTarget.setMark(mark, which, t),
    hasMarkRange: (mark) => motionTarget.hasMarkRange(mark),
    dragMark: (mark, which, t, duration) => clipExtract.dragMark(mark, which, t, duration ?? undefined),
    markToSourceRange: (mark) => {
      const r = clipExtract.markToSourceRange(clips, mark)
      return r.error ? { error: r.error } : { clip: r.clip.id, start: r.start, end: r.end }
    },
    segmentLabel: (title, start, end) => clipExtract.segmentLabel(title, start, end),
    segmentDescription: (ask) => clipExtract.segmentDescription(ask),
    groupMoveFromOrig: (origs, delta) => {
      const moved = m.groupMoveFromOrig(origs.map((o) => ({ ...o })), origs, delta)
      return moved.map((c) => c.start)
    },
  }
  return Promise.all(calls.map(([fn, ...args]) => run[fn](...args))) // checkUpgrade y runUpgrade son async
}))
process.stdout.write(JSON.stringify(out))
