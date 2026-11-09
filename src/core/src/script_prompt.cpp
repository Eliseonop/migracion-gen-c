#include "gc/core/script_prompt.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>

#include "gc/core/timeline_ui.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

// Lo que quita String.prototype.trim: los espacios y finales de línea de ECMAScript, en UTF-8.
constexpr std::array<std::string_view, 25> kJsSpaces = {
    "\t",           "\n",           "\v",           "\f",           "\r",           " ",           "\xC2\xA0",
    "\xE1\x9A\x80", "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
    "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89", "\xE2\x80\x8A", "\xE2\x80\xA8", "\xE2\x80\xA9",
    "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80", "\xEF\xBB\xBF"};

std::string_view js_trim(std::string_view text) {
    for (bool again = true; again;) {
        again = false;
        for (const std::string_view space : kJsSpaces) {
            if (text.starts_with(space)) {
                text.remove_prefix(space.size());
                again = true;
            }
            if (text.ends_with(space)) {
                text.remove_suffix(space.size());
                again = true;
            }
        }
    }
    return text;
}

// String(n) de JS en lo que aparece aquí: los enteros sin decimales y el resto, con el repr más corto.
std::string js_number_text(double n) {
    constexpr double kMaxExact = 1e21; // de ahí en adelante, JS pasa a la notación exponencial
    if (std::trunc(n) == n && std::abs(n) < kMaxExact) {
        return js_to_fixed(n, 0);
    }
    return detail::py_repr(n);
}

// `a || b || c`: el primer valor verdadero de JS, ya como String(...).
std::string first_truthy_text(const Json& clip, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        const Json value = clip.value(key, Json()); // sin iteradores: GCC 15 da un falso -Wnull-dereference
        if (value.is_string()) {
            if (const auto text = value.get<std::string>(); !text.empty()) {
                return text;
            }
        } else if (value.is_boolean()) {
            if (value.get<bool>()) {
                return "true";
            }
        } else if (value.is_number()) {
            const double n = value.get<double>();
            if (n != 0 && !std::isnan(n)) {
                return js_number_text(n);
            }
        }
    }
    return {};
}

// Number(c?.campo): sin el campo, NaN (undefined); con null, 0.
double js_number_of(const Json& clip, const char* key) {
    return clip.contains(key) ? js_number(clip.at(key)) : std::numeric_limits<double>::quiet_NaN();
}

// buildClipList: una línea por clip con descripción; «» si no hay ninguna.
std::string build_clip_list(const Json& clips) {
    if (!clips.is_array()) {
        return {};
    }
    std::string out;
    std::size_t row = 0;
    for (const Json& c : clips) {
        if (!c.is_object()) {
            continue;
        }
        const std::string raw = first_truthy_text(c, {"description", "label", "filename"});
        const std::string_view desc = js_trim(raw);
        if (desc.empty()) {
            continue;
        }
        const double start = js_number_of(c, "start");
        const double end = js_number_of(c, "end");
        double dur = 0;
        if (std::isfinite(start) && std::isfinite(end) && end > start) {
            dur = end - start;
        } else if (const double whole = js_number_of(c, "duration"); !std::isnan(whole)) { // Number(...) || 0
            dur = whole;
        }
        ++row;
        const Json index = c.value("index", Json());
        std::string id = std::to_string(row); // r.id != null ? r.id : i + 1
        if (index.is_number()) {
            id = js_number_text(index.get<double>());
        } else if (index.is_string()) {
            id = index.get<std::string>();
        } else if (index.is_boolean()) {
            id = index.get<bool>() ? "true" : "false";
        }
        if (!out.empty()) {
            out += '\n';
        }
        out += "[Clip " + id + "] ";
        out += desc;
        if (dur > 0) {
            out += " (" + js_to_fixed(dur, 1) + "s)";
        }
    }
    return out;
}

} // namespace

std::string build_script_prompt(std::string_view topic, const Json& clips) {
    const std::string_view trimmed = js_trim(topic);
    const std::string tema = trimmed.empty() ? std::string("[ESCRIBE AQUÍ EL TEMA DEL VIDEO]") : std::string(trimmed);
    const std::string clip_list = build_clip_list(clips);

    std::string out =
        R"(Necesito que escribas el guion para un video corto de YouTube Shorts, en formato vertical.

El texto se convertirá directamente en voz usando un modelo TTS en español, como Kokoro.

MUY IMPORTANTE: escribe pensando en CÓMO EL TTS VA A PRONUNCIAR LAS PALABRAS, no necesariamente en cómo se escriben originalmente.

Cuando aparezcan nombres, palabras o términos en inglés, NO los escribas en inglés si eso hace que el TTS los pronuncie mal.

Escríbelos de forma fonética, usando letras del español, para que un hablante hispanohablante los pronuncie lo más parecido posible al inglés.

Ejemplos:
McFly → Macflai
Marty → Marti
Rick → Ric
Morty → Morti
Justin → Yastin
Dan Harmon → Dan Jarmon
Brown → Braun

No traduzcas los nombres. Solo adapta su escritura para conseguir una pronunciación natural.

TONO Y VOZ:

Quiero que suene como una persona real que acaba de descubrir algo increíble y se lo está contando a un amigo.

Amigable: 9/10
Natural: 9/10
Expresivo: 8/10
Sorpresa: 7/10
Energía: 7/10
Autoridad: 5/10
Dramatismo: 3/10

NO quiero una voz de documental.
NO quiero una voz de enciclopedia.
NO quiero un narrador formal.
NO quiero frases que parezcan escritas por una IA.

Debe sonar espontáneo, curioso, cercano y ligeramente sorprendido.

Ejemplo del tono correcto:
"Oye, ¿sabías que esto empezó casi como una broma?"

Ejemplo incorrecto:
"Esta producción tuvo sus orígenes en un proyecto audiovisual desarrollado por sus creadores."

REGLA DE ORO:

Escribe como HABLA una persona, no como escribe un artículo.

Usa frases cortas.
Una idea por frase.
Evita palabras innecesariamente complicadas.
Usa expresiones naturales.
Haz que cada frase sea fácil de pronunciar.

FORMATO PARA EL TTS:

Cada frase debe estar en su propia línea.

Usa puntuación limpia.
Usa comas y puntos para controlar naturalmente el ritmo.
Usa signos de interrogación y exclamación cuando correspondan.

No abuses de los puntos suspensivos.

NO uses:
Emojis.
Hashtags.
Markdown.
Asteriscos.
Viñetas.
Acotaciones como "pausa", "tono emocionado" o similares DENTRO del texto del guion.

Escribe los números con palabras.

Evita trabalenguas.
Evita palabras extranjeras innecesarias.
Cuando una palabra extranjera sea necesaria, escríbela fonéticamente para que el TTS en español la pronuncie correctamente.

ESTRUCTURA:

La primera frase debe ser un gancho fuerte.

Después desarrolla la historia rápidamente, revelando información interesante poco a poco.

Incluye curiosidad, sorpresa o pequeños giros dentro de la narración.

El cierre debe dejar al espectador con ganas de saber qué pasó después.

DURACIÓN:

Aproximadamente cuarenta segundos.
Entre noventa y ciento diez palabras.

TEMA:

)";
    out += tema;
    out += "\n\n";
    if (!clip_list.empty()) {
        out += R"(MATERIAL DISPONIBLE (CLIPS QUE TENGO):

Estos son los clips que ya tengo grabados/descargados, con su descripción y su duración aproximada. Debes construir el montaje USANDO SOLO estos clips, referenciándolos por su identificador (por ejemplo [Clip 3]).

)";
        out += clip_list;
        out += R"(

MONTAJE (MUY IMPORTANTE):

Además del guion hablado, quiero que actúes como editor y me digas QUÉ ESCENA PONER en cada frase o en cada tramo de segundos del narrador.

Reglas del montaje:

Asigna a cada frase del guion uno o varios clips de la lista de arriba, referenciados por su identificador.
Cuando un clip dure más que la frase, indica qué fracción o momento del clip mostrar (por ejemplo "primeros 2 segundos" o "la parte final").
No inventes escenas que no estén en la lista. Si falta material para una frase, dilo claramente con "FALTA CLIP: ..." describiendo qué haría falta grabar.
Sería bueno que propongas pequeñas PAUSAS del narrador en momentos clave, para dejar que una fracción de la escena se vea y se aprecie mejor antes de seguir hablando. Indica la duración aproximada de cada pausa (por ejemplo "pausa de un segundo").

)";
    }
    out += R"(Antes de entregar el guion, revisa mentalmente cómo sonaría cada frase pronunciada por un TTS en español.

Si un nombre en inglés probablemente será pronunciado mal por el TTS, reemplázalo por una escritura fonética en español.

FORMATO DE LA RESPUESTA:

Primero, bajo el título GUION, escribe únicamente el guion final: cada frase en su propia línea, sin acotaciones ni anotaciones de escena.
)";
    if (!clip_list.empty()) {
        out += "Después, bajo el título MONTAJE, escribe la lista de frases numeradas y, para cada una, qué clip mostrar (por su identificador), qué fracción del clip usar y las pausas sugeridas.";
    }
    return out;
}

} // namespace gc::core
