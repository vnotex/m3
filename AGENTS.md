# Repository Guidelines

## Project Overview
`m3` ("Mindmap in Markdown") is a C++17 mind-map library with a public C API and an optional Qt Widgets editor/demo. Native JSON is the persistence format; Markdown, outline JSON and Qt HTML exports are projections, not full-fidelity round-trip formats.

## Architecture & Data Flow
- `m3core` / `m3::core`: `src/api.cpp` wraps the semantic model, JSON/Markdown codecs and layout engine behind the C ABI. The model owns an ordered rooted tree plus cross-links; it has no Qt dependency.
- `m3qt` / `m3::qt_editor`: `MindMapEditor` is a QWidget/PImpl facade. `MindMapController` owns the core handle, snapshots committed JSON, measures visible nodes through `MindMapView`, requests core layout, then installs a `Presentation` in the graphics view.
- Persist node data, ordered children, cross-links, styles and `expanded`; keep selection, layout direction, geometry, zoom and pan outside persisted JSON. Collapsing a node hides descendants without deleting them.
- File handling belongs to the host/demo. URL activation emits `nodeLinkActivated`; hosts decide whether to open it. Do not put file/network policy in the reusable editor.

## Key Directories
- `include/m3/`: installed C contracts; `include/m3/qt/` is the optional Qt C++ surface.
- `src/`: Qt-free model/codecs/layout; `src/qt/`: controller/view, properties UI, HTML export and embedded resources.
- `tests/`: case-driven executables and allocation-failure tests; `tests/consumer/` and `tests/qt_consumer/` validate installed packages.
- `examples/`: demo-owned open/save/export policy. `cmake/` and `.github/workflows/`: package exports and CI recipes. `third_party/`: vendored dependencies/data and required licenses.

## Development Commands
Run from the repository root; examples use PowerShell-compatible syntax.

Core configure, build and test:
```powershell
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Debug -DM3_BUILD_SHARED=ON -DM3_BUILD_TESTS=ON
cmake --build build/core --config Debug
ctest --test-dir build/core -C Debug --output-on-failure
```

Qt editor/demo on Windows with the matching Qt MSVC kit discoverable through `CMAKE_PREFIX_PATH` or `Qt6_DIR`:
```powershell
cmake -S . -B build/qt -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug -DM3_BUILD_SHARED=ON -DM3_BUILD_QT=ON -DM3_BUILD_QT_DEMO=ON -DM3_BUILD_TESTS=ON
cmake --build build/qt --config Debug
ctest --test-dir build/qt -C Debug --output-on-failure
cmake --install build/qt --config Debug --prefix build/install-qt
./build/install-qt/bin/m3_qt_demo.exe
```
Installation deploys Qt runtime dependencies on Windows. The demo accepts one optional JSON file argument; without one it opens a sample map. Do not set `QT_QPA_PLATFORM=offscreen` globally when launching the interactive demo.

Use a separate build directory for a different generator/toolchain. `--config`/CTest `-C` select multi-config builds; `CMAKE_BUILD_TYPE` selects single-config builds. There is no configured lint/format command; existing compiler flags are `/W4 /utf-8` on MSVC and `-Wall -Wextra -Wpedantic` elsewhere.

## Code Conventions & Common Patterns
- Match nearby compact C++ formatting: four spaces, same-line braces, header guards. C API functions use `m3_*`, core helpers use snake_case, Qt types use PascalCase and methods/signals camelCase; namespaces are `m3` and `m3::qt`.
- Use RAII (`std::unique_ptr`, Qt parent ownership). C inputs are borrowed NUL-terminated UTF-8; release output snapshots with `m3_string_free`, layout results with `m3_layout_result_free`, and handles with `m3_mindmap_destroy`. Output slots are nulled on failure.
- Reuse `m3::require`/`Failure` internally and `boundary` in `src/api.cpp` to translate exceptions to `M3Status` and thread-local `m3_last_error()`. No C++ exception may cross the C ABI. Validate/allocate before committing mutations; follow the existing prepare-then-swap pattern so failures leave documents unchanged.
- Preserve immutable case-sensitive IDs, child order and opaque style keys. Native records reject unknown fields and duplicate JSON members; import compatibility formats have separate rules. Preserve wire spellings such as `hyperLink`. Compare parsed JSON, not formatting/key order.
- Qt commands are synchronous and require the GUI thread with a host `QApplication`. Independent core handles may run concurrently; callers synchronize shared handles. Use context-bound deferred Qt callbacks, not an invented worker-thread pipeline.
- Configure shortcuts through `EditorConfig`; extend dropped-file URL conversion through synchronous `resolveDroppedFileUrl` (empty skips the update). Reuse controller signals and QObject ownership rather than introducing a dependency-injection framework.
- Semantic success emits `documentChanged`; a later rendering failure does not roll back the committed edit. Preserve the distinction between semantic failure and presentation failure, and keep uncommitted inline drafts out of snapshot exports.

## Important Files
- `include/m3/m3.h`, `include/m3/m3_layout.h`, `include/m3/qt/editor.h`: authoritative ownership, persistence, layout and widget behavior contracts; read these before changing public behavior.
- `src/qt/mindmap_controller.cpp`, `src/qt/presentation.h`: semantic-to-render boundary and selection/error lifecycle.
- `CMakeLists.txt`, `src/qt/CMakeLists.txt`, `cmake/m3Config.cmake.in`: targets, resources, installs and package components.
- `.github/workflows/ci.yml`: complete build/install/consumer validation recipes. `examples/qt_demo.cpp` is the application entry point; `examples/qt_demo_window.cpp` owns file actions. `README.md` contains only the project tagline.

## Runtime/Tooling Preferences
- Require CMake >=3.18 and a C++17 compiler; the `ctest --test-dir` examples need CMake/CTest >=3.20. C API consumers/tests use C99. This is a native CMake project, not a Node/Bun project; no JavaScript package manager is involved.
- `M3_BUILD_SHARED` defaults ON for the core; Qt editor is always shared. `M3_BUILD_TESTS` defaults ON only at top level. `M3_BUILD_QT` defaults OFF; `M3_BUILD_QT_DEMO` defaults to the Qt setting and requires Qt enabled.
- Qt needs >=6.5 Widgets, plus Qt Test for tests. CI uses Qt 6.8.3 with MSVC 2022 x64. Installed core consumers link `m3::core` without Qt discovery; editor consumers request `find_package(m3 CONFIG REQUIRED COMPONENTS qt_editor)` and link `m3::qt_editor`.
- Keep build/install artifacts under ignored `build/`. Preserve vendored nlohmann/json 3.11.3, Unicode emoji data and license notices; QSS and Unicode resources are embedded by Qt CMake. No asset regeneration script is provided.

## Testing & QA
- CTest drives standalone case-dispatched C/C++ executables, not GoogleTest/Catch2. Reuse `CHECK`, `ok`, `Map`, `Text` and fixtures in `tests/test_support.h`; register new case arguments in `tests/CMakeLists.txt` and the matching executable dispatch.
- Focused examples: `ctest --test-dir build/core -C Debug -R '^m3_model_' --output-on-failure` and `ctest --test-dir build/qt -C Debug -R '^m3_qt_' --output-on-failure`. Qt tests use Qt Test helpers and CTest sets `QT_QPA_PLATFORM=offscreen`; the matching platform plugin must be available. Demo cases require `M3_BUILD_QT_DEMO=ON`.
- Cover observable contracts: mutation atomicity, snapshot ownership, schema/Unicode errors, deep/collapsed layouts, and Qt signal/state transitions. `m3_allocations_outputs` / `m3_allocations_mutations` use a separate fault-injection core; never expose allocator controls in the installed library.
- CI covers Windows/Linux core and Windows Qt, Debug/Release and shared/static core. Packaging changes also require the installed consumers and core-without-Qt checks in CI; these are not CTest registrations. No coverage percentage is configured.
