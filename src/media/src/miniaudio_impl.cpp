// La implementación de miniaudio (una sola cabecera), en su propia biblioteca sin los avisos del
// proyecto: es código de terceros. Solo la reproducción: ni grabar, ni decodificar archivos (eso
// lo hace FFmpeg), ni generadores.
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
