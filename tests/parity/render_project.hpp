#pragma once

// Una escena de las fixtures de render (render/synthetic.json, render/text.json) como proyecto de
// gc_export, para pasarla por FrameRenderer (el camino de `genclip frame` y del preview).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <utility>

#include "gc/core/clip_media.hpp"
#include "gc/core/io.hpp"
#include "gc/core/json.hpp"
#include "gc/core/serialize.hpp"
#include "gc/export/project.hpp"

namespace gc::test {

// El material sale de render/files (los clips pasan a ser de colección, que se busca ahí; el render
// no mira `asset_scope`) y la caché de «Eliminar fondo», de render/files/bgcache (DATA_DIR/bgcache).
// Las fuentes (<raíz>/app/fonts) son una copia de resources/fonts en una carpeta temporal: las
// mismas que usan los tests con su TextFonts.
inline gc::exporter::LoadedProject render_fixture_project(gc::Json timeline) {
    static const std::filesystem::path root = [] {
        const auto r = std::filesystem::temp_directory_path() / "gc_test_render_root";
        std::filesystem::create_directories(r / "app" / "fonts");
        std::filesystem::copy(gc::core::path_from_utf8(GC_RESOURCES_DIR) / "fonts", r / "app" / "fonts",
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::skip_existing);
        return r;
    }();
    for (gc::Json& clip : timeline.at("clips")) {
        clip["asset_scope"] = "collection";
    }
    const gc::Json raw = {{"id", "render-fixture"},
                          {"name", "render"},
                          {"created_at", "2026-10-31T00:00:00"},
                          {"timeline", std::move(timeline)}};
    auto project = gc::core::project_from_json(raw);
    REQUIRE(project);
    REQUIRE(project->timeline);
    gc::exporter::LoadedProject lp;
    lp.root = root;
    const auto files = gc::core::path_from_utf8(GC_FIXTURES_DIR) / "render" / "files";
    lp.dirs = gc::core::MediaDirs{root / "clips", files, files};
    lp.settings = gc::Json::object();
    lp.project = *project;
    lp.timeline = *project->timeline;
    return lp;
}

} // namespace gc::test
