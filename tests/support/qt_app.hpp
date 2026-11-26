#pragma once

// La app de Qt de los tests del studio: un QGuiApplication sin pantalla (QT_QPA_PLATFORM=offscreen) para todo el
// proceso, que crea el primero que la pide y no se destruye (hacerlo al salir, detrás de los estáticos de Qt, rompe).
// La define test_studio_ui.cpp. ctest corre cada test en su proceso: el que use la red o el bucle de eventos de Qt la
// pide antes (sin ella, QNetworkAccessManager aborta).

#include <QGuiApplication>

namespace gc::test {

QGuiApplication& qt_app();

} // namespace gc::test
