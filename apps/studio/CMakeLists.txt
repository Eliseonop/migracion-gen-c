# genclip-studio: el editor de escritorio Qt Quick (F5). gc_studio tiene los ViewModels y el arranque
# (con gc_set_warnings); gc_studio_qml, el módulo QML GenClip.Studio, cuyo C++ generado no pasa
# gc_set_warnings. Los tests enlazan los dos para manejar la interfaz sin pantalla.

qt_standard_project_setup()
# El puente REST con el backend de video-yt (backend_jobs.hpp), hasta F6 y F7: QtNetwork, sin TLS (es local).
find_package(Qt6 REQUIRED COMPONENTS Network)

add_library(gc_studio STATIC app.cpp backend_jobs.cpp beat_detector.cpp clip_list_model.cpp crop_editor.cpp editor_model.cpp
                             export_job.cpp frame_grabber.cpp frame_image.cpp frame_preview.cpp
                             home_model.cpp mask_overlay.cpp platform_chrome.cpp player.cpp preview_source.cpp proxy_maker.cpp
                             rounded_image.cpp
                             settings_model.cpp thumbnails.cpp)
target_include_directories(gc_studio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gc_studio PUBLIC gc::core Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::QuickControls2)
gc_set_warnings(gc_studio)
# La vista previa compone con gc_export, que solo existe con FFmpeg y Skia (presets msvc-vcpkg-*):
# sin él, el studio se compila igual y la vista previa lo dice.
if(TARGET gc_export)
    target_link_libraries(gc_studio PUBLIC gc::export)
    target_compile_definitions(gc_studio PUBLIC GC_STUDIO_PREVIEW)
endif()
# El catálogo de efectos y animaciones (resources/fx) va dentro del ejecutable.
qt_add_resources(gc_studio "gc_resources" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/resources"
                 FILES "${PROJECT_SOURCE_DIR}/resources/fx/catalog.json")

set(qml_files Main Theme Home SideItem ProjectCard FolderCard InlineEdit IconButton Editor Timeline Inspector
              MaterialPanel Settings CropModal CurveEditor CollectionsManager SmallButton SfxModal ShapeCard MaterialTile ClipMenu
              Toast)
list(TRANSFORM qml_files REPLACE "(.+)" "qml/\\1.qml")
foreach(file IN LISTS qml_files)
    cmake_path(GET file FILENAME name)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name}) # junto al qmldir
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_library(gc_studio_qml STATIC)
qt_add_qml_module(gc_studio_qml URI GenClip.Studio VERSION 1.0 RESOURCE_PREFIX /qt/qml NO_PLUGIN
                  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/GenClip/Studio QML_FILES ${qml_files})
target_link_libraries(gc_studio_qml PUBLIC Qt6::Quick Qt6::QuickControls2)

qt_add_executable(genclip-studio main.cpp)
target_link_libraries(genclip-studio PRIVATE gc_studio gc_studio_qml)
set_target_properties(genclip-studio PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
include(GcQtRuntime)
gc_qt_runtime(genclip-studio) # con el Qt de vcpkg, que arranque desde build/
