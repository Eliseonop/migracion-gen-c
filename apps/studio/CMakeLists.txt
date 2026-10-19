# genclip-studio: el editor de escritorio Qt Quick (F5). gc_studio tiene los ViewModels (se prueban
# con Catch2) y el arranque; el ejecutable solo añade el QML, cuyo C++ generado no pasa
# gc_set_warnings.

qt_standard_project_setup()

add_library(gc_studio STATIC app.cpp editor_model.cpp home_model.cpp)
target_include_directories(gc_studio PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(gc_studio PUBLIC gc::core Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2)
gc_set_warnings(gc_studio)

qt_add_executable(genclip-studio main.cpp)
set(qml_files Main Theme Home SideItem ProjectCard FolderCard InlineEdit IconButton Editor Timeline Inspector)
list(TRANSFORM qml_files REPLACE "(.+)" "qml/\\1.qml")
foreach(file IN LISTS qml_files)
    cmake_path(GET file FILENAME name)
    set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name}) # junto al qmldir
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(genclip-studio URI GenClip.Studio VERSION 1.0 RESOURCE_PREFIX /qt/qml QML_FILES ${qml_files})
target_link_libraries(genclip-studio PRIVATE gc_studio)
set_target_properties(genclip-studio PROPERTIES WIN32_EXECUTABLE ON MACOSX_BUNDLE ON)
