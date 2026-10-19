// Genera src/core/src/text_themes.inc: los temas de subtítulos (subtitleThemes.js) y los estilos
// preestablecidos «Aa» (TEXT_LOOKS de textstyles.js) del editor de video-yt, como JSON.
//
//   node tools/gen/gen_text_themes.mjs <video-yt>/frontend/src
//
// Rehacer cuando cambien esas tablas en video-yt (el test de paridad lo avisa).
import { writeFileSync } from 'node:fs'
import { pathToFileURL } from 'node:url'

const root = process.argv[2].replace(/[\\/]+$/, '')
const load = (rel) => import(pathToFileURL(`${root}/${rel}`).href)
const themes = await load('lib/subtitleThemes.js')
const styles = await load('lib/textstyles.js')
// En trozos de 2000 caracteres: MSVC no admite literales de más de 16 KB.
const json = (v) => JSON.stringify(v).match(/[^]{1,2000}/g).map((p) => `R"json(${p})json"`).join('\n    ')
const out = `// Generado por tools/gen/gen_text_themes.mjs desde el editor de video-yt: no editar a mano.
constexpr std::string_view kSubtitleThemesJson =
    ${json(themes.SUBTITLE_THEMES)};
constexpr std::string_view kTextLooksJson =
    ${json(styles.TEXT_LOOKS)};
constexpr std::string_view kLookBaseJson =
    ${json(styles.lookPatch(null))};
`
writeFileSync(new URL('../../src/core/src/text_themes.inc', import.meta.url), out)
