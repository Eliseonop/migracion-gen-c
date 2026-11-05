// Los tests corren sin nadie delante. En Windows, un abort(), un assert de la CRT de depuración o un
// qFatal de Qt (que en Debug pasa por _CrtDbgReport) abrían una ventana de «Microsoft Visual C++
// Runtime Library» que esperaba a que alguien pulsara un botón; así salen por stderr y el test falla
// sin más.

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>
#endif

namespace {

[[maybe_unused]] const bool kNoCrashDialogs = [] noexcept {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX); // ni la de «dejó de funcionar»
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
#endif
    return true;
}();

} // namespace

#endif
