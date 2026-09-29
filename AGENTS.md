# Repository Guidelines

## Project Overview
`m3` ("Mindmap in Markdown") is a C++17 mind-map library with a public C API and an optional Qt Widgets editor/demo. Native JSON is the persistence format; Markdown, outline JSON and Qt HTML exports are projections, not full-fidelity round-trip formats.

## Architecture & Data Flow
- `m3core` / `m3::core`: `src/api.cpp` wraps the semantic model, JSON/Markdown codecs and layout engine behind the C ABI. The model owns an ordered rooted tree plus cross-links; it has no Qt dependency.
- `m3qt` / `m3::qt_editor`: `MindMapEditor` is a QWidget/PImpl facade. `MindMapController` owns the core handle, snapshots committed JSON, measures visible nodes through `MindMapView`, requests core layout, then installs a `Presentation` in the graphics view.
- Persist node data, ordered children, cross-links, styles and `expanded`; keep selection, layout direction, geometry, zoom and pan outside persisted JSON. Collapsing a node hides descendants without deleting them.
- File handling and toolbar presentation belong to the host/demo. The reusable editor contains no toolbar rows or layout picker; its canvas, properties card, context menus and shortcuts remain native. `examples/qt_demo_window.cpp` owns the demo rows. URL activation emits `nodeLinkActivated`; hosts decide whether to open it. Do not put file/network policy in the reusable editor.

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

## Embedding the Qt Editor in an Application

Use the installed public header `<m3/qt/editor.h>` and target `m3::qt_editor`; do not include `src/qt/` internals or try to share a core handle with the widget. Each editor owns its document. The host supplies `QApplication`, the GUI thread, widget ownership, and file/network policy.

### Build and deploy the host
Build/install m3 with `M3_BUILD_QT=ON`; the demo is optional (`M3_BUILD_QT_DEMO=OFF` for library-only builds). Add the m3 install prefix and a compatible Qt kit to the host's `CMAKE_PREFIX_PATH` (or set `m3_DIR` and `Qt6_DIR`). A standalone application's `CMakeLists.txt` can be:

```cmake
cmake_minimum_required(VERSION 3.18)
project(mindmap_app LANGUAGES CXX)
find_package(m3 CONFIG REQUIRED COMPONENTS qt_editor)
add_executable(mindmap_app main.cpp)
target_compile_features(mindmap_app PRIVATE cxx_std_17)
target_link_libraries(mindmap_app PRIVATE m3::qt_editor)
```

The component discovers Qt Widgets transitively; Qt Test is not a host dependency. Enable `AUTOMOC` on the host target if its own classes use `Q_OBJECT`; this example needs none. Match compiler/ABI, architecture, Qt kit and Debug/Release libraries. Linking does not deploy runtime dependencies: ship m3qt, m3core when shared, and the matching Qt libraries/platform plugins. See `tests/qt_consumer/CMakeLists.txt` for Windows m3 DLL copying; the demo's Qt deployment recipe is in `src/qt/CMakeLists.txt`. A host adding network access must find/link Qt Network itself.

### Translations
The reusable editor ships Simplified Chinese (`m3_zh_CN`) and Japanese (`m3_ja`) Qt
catalogs in `src/qt/translations/`. Qt builds require matching-major `LinguistTools`;
core-only builds and installed consumers do not. Normal builds generate `.qm` files
under the build tree; installation places them in `${CMAKE_INSTALL_DATADIR}/m3/translations`.
The host owns a `QTranslator`, loads `m3` with its chosen `QLocale` and `_` separator,
and installs it before constructing editors. Missing catalogs fall back to English;
live language switching is not provided. The demo, Qt-free core diagnostics and the
vendored Unicode emoji names/search corpus are not part of these catalogs.

Refresh source catalogs deliberately with `cmake --build <builddir> --target m3_update_translations`,
then translate every unfinished entry. That target includes the public include path so
`MindMapEditor::Private` extracts into the same context as runtime `tr()` calls.
Helper widgets without `Q_OBJECT` need `Q_DECLARE_TR_FUNCTIONS`; shared color labels
use the explicit `m3::qt::NodeColors` context. Keep translation source arguments literal.

VNote packages the checked-in `.qm` files, like vtextedit, rather than m3's build outputs.
After editing the `.ts` files, run the Qt 5.15 `lrelease` tool from the m3 root for each
catalog and commit both source and binary files (Qt 6 can read these catalogs):
```powershell
lrelease src/qt/translations/m3_zh_CN.ts -qm src/qt/translations/m3_zh_CN.qm
lrelease src/qt/translations/m3_ja.ts -qm src/qt/translations/m3_ja.qm
```
Ordinary builds and `m3_update_translations` do not refresh the checked-in `.qm` files.

### Minimal host with startup Open and Save As
This complete `main.cpp` accepts one optional JSON path and provides Save As. It uses the same `QFile`/`QSaveFile` policy as the demo, checks failures, and clears the dirty flag only after a successful load or committed save. In an existing app, parent the editor to a page/dock and add it to that container's layout instead of creating another `QApplication`.
For localized UI, also follow [Translations](#translations) before constructing the editor.

```cpp
#include <m3/qt/editor.h>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QStatusBar>

using m3::qt::MindMapEditor;

bool saveDocument(MindMapEditor &editor, QMainWindow &window, const QString &path) {
    const auto fail = [&window](const QString &message) {
        window.statusBar()->showMessage(message);
        return false;
    };
    if (!editor.commitActiveEdit(true)) return fail(editor.lastError());
    const QByteArray bytes = editor.toJson();
    if (bytes.isEmpty()) return fail(editor.lastError());
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return fail(output.errorString());
    if (output.write(bytes) != bytes.size()) {
        output.cancelWriting();
        return fail(output.errorString());
    }
    if (!output.commit()) return fail(output.errorString());
    window.setWindowModified(false);
    return true;
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("Mind map[*]"));
    const auto fail = [&window](const QString &message) {
        QMessageBox::critical(&window, QStringLiteral("Open failed"), message);
        return 1;
    };
    const QStringList args = app.arguments();
    const QString path = args.size() > 1 ? args.at(1) : QString();
    m3::qt::EditorConfig config;
    config.resourceBasePath = path.isEmpty() ? QDir::currentPath() : QFileInfo(path).absolutePath();
    config.shortcuts.addChild = {QKeySequence(QStringLiteral("Ctrl+J"))};
    auto *editor = new MindMapEditor(config, &window);
    window.setCentralWidget(editor);
    QObject::connect(editor, &MindMapEditor::documentChanged, &window,
                     [&window] { window.setWindowModified(true); });
    QObject::connect(editor, &MindMapEditor::errorOccurred, &window,
                     [&window](const QString &message) { window.statusBar()->showMessage(message); });
    if (!path.isEmpty()) {
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
        const QByteArray bytes = input.readAll();
        if (input.error() != QFileDevice::NoError) return fail(input.errorString());
        if (!editor->loadJson(bytes)) return fail(editor->lastError());
    }
    window.setWindowModified(false); // A successful load emits documentChanged first.
    auto *saveAs = window.menuBar()->addAction(QStringLiteral("Save As..."));
    saveAs->setShortcut(QKeySequence::SaveAs);
    QObject::connect(saveAs, &QAction::triggered, &window, [editor, &window] {
        const QString target = QFileDialog::getSaveFileName(
            &window, QStringLiteral("Save mind map"), {}, QStringLiteral("JSON files (*.json)"));
        if (!target.isEmpty()) saveDocument(*editor, window, target);
    });
    window.resize(1100, 750);
    window.show();
    return app.exec();
}
```

### Document lifecycle and host policy
- **New/Open/Close:** the constructor already creates a selected root. Use `newDocument(topic)` or `loadJson(bytes)` to replace it; failed imports preserve the current document and selection. In a full host, prompt Save/Discard/Cancel before replacing a dirty document or closing. Track the filename separately and update it only on successful load/save. Reuse `DemoWindow::mayReplace`, `openFile`, `saveFile`, and `closeEvent` in `examples/qt_demo_window.cpp` as the reference, not as library internals.
- **Persistence and drafts:** Save/autosave must check `commitActiveEdit(true)` before `toJson()` so the snapshot includes the draft while the same input, caret, focus and local undo remain active. Repeated unchanged captures are no-ops; later typing becomes pending again, and Escape discards only changes since the last accepted snapshot. `commitActiveEdit()` without the flag still ends editing. Snapshots never commit implicitly; `toMarkdown()`/`toHtml()` are export projections, not editable backups. Exporting must not clear the dirty flag. Selection, layout direction, zoom and pan remain host/view state.
- **Errors and changes:** check each command's `bool`/ID result and each snapshot for empty output; inspect `lastError()` on failure. Connect `errorOccurred` for interactive/presentation errors. `documentChanged` means a semantic edit committed, including New/Open, not merely a selection or camera change. A later drawing failure does not roll back that edit; never clear dirty state in the error handler.
- **Commands and selection:** use `addNode`, `renameNode`, `moveNode`, `setExpanded`, `addLink`, etc. rather than editing controller/model internals. Hosts can present the existing editor-owned `commandAction(name)` in their own toolbar/menu; the public header lists stable names. Do not delete/reparent these borrowed actions or change their shortcut/enablement policy. Follow `layoutDirectionChanged` to synchronize host layout controls, blocking combo signals on both updates and failure resets. Keep returned IDs opaque and case-sensitive; do not assume an imported root is named `root`. Use `selectedNodeIds()` for groups: both arguments of `selectionChanged` and `selectedNodeId()` can be empty for a multi-selection. Disable single-node actions unless exactly one node is selected.
- **Configuration and resource bases:** `EditorConfig` is copied at construction. Replace/clear shortcut lists to rebind/disable keys without removing commands. Set `resourceBasePath` before creating the widget; an empty value captures the working directory. The base is fixed for the editor's lifetime: Open, Save As and `reloadImages()` do not rebase relative image/link references. Choose a stable workspace base or recreate the editor for a different document base; saving elsewhere does not rewrite stored paths.
- **URL activation and hooks:** connect `nodeLinkActivated(nodeId, url)` to the host's validated opening policy. Relative references arrive as absolute file URLs; do not blindly open arbitrary schemes or paths from imported documents. Override the synchronous `resolveDroppedFileUrl` to customize dropped-file storage (empty skips), or `onAddUrl`/`onAddImage` for app-owned pickers. These are subclass hooks, not signals or asynchronous callbacks.
- **Images:** the example intentionally supplies no image I/O, so image nodes keep placeholders. Connect `imageRequested(url, requestId)` before loading (or call `reloadImages()` after connecting). The host fetches/decodes and calls `provideImage(url, requestId, pixels)` on the GUI thread with the original URL/ID; null `QImage` means unavailable. Stale/duplicate responses are ignored. Use QObject contexts/`QPointer` for asynchronous lifetime safety and enforce scheme, path, size and network limits in the host. `examples/qt_demo_window.cpp` illustrates one loader; Qt Network is demo/host policy, not an editor dependency.

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
- `.github/workflows/ci.yml`: complete build/install/consumer validation recipes. `examples/qt_demo.cpp` is the application entry point; `examples/qt_demo_window.cpp` owns file actions. `README.md` provides the project overview, screenshot, build instructions and library integration guidance.

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
