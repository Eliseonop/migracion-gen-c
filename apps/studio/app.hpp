#pragma once

namespace gc::studio {

// Arranca el editor y devuelve el código de salida de la app.
int run(int argc, char* argv[]);

// Antes de crear el motor de QML. En Windows carga las librerías de los plugins opcionales de
// QtQuick.Controls y registra sus módulos, para que el motor no cargue las DLL de esos plugins: Smart App
// Control puede bloquearlas (son las de vcpkg, sin firmar) y entonces no carga ningún control.
void preload_qml_modules();

} // namespace gc::studio
