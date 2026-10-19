# Repository Guidelines

## Project Overview
`m3` ("Mind Map in Markdown") is a C++17 mind-map library with a public C API and an optional Qt Widgets editor/demo. Native JSON is the persistence format; Markdown, outline JSON and Qt HTML exports are projections, not full-fidelity round-trip formats.

## Architecture & Data Flow
- `m3core` / `m3::core`: `src/api.cpp` wraps the semantic model, JSON/Markdown codecs and layout engine behind the C ABI. The model owns an ordered forest (main tree plus orphan trees) and cross-links; it has no Qt dependency.
- `m3qt` / `m3::qt_editor`: `MindMapEditor` is a QWidget/PImpl facade. `MindMapController` owns the core handle, snapshots committed JSON, measures the full tree through `MindMapView`, requests core layout, then installs a `Presentation` in the graphics view.
- Persist node data, ordered children, cross-links and styles; keep folding, selection, layout direction, geometry, zoom and pan outside persisted JSON. Legacy complete-node `expanded` values are boolean-validated and discarded (native, simple-tree, MindElixir and full-node insertion); node patches reject that key atomically. Schema version remains 1 and all descendants are retained.
- File handling and toolbar presentation belong to the host/demo. The reusable editor contains no toolbar rows or layout picker; its canvas, properties card, context menus and shortcuts remain native. `examples/qt_demo_window.cpp` owns the demo rows. URL activation emits `nodeLinkActivated`; hosts decide whether to open it. Do not put file/network policy in the reusable editor.

## Orphan Trees

- `rootId` remains the protected main root. Native schemaVersion 1 optionally stores ordered
  `orphanIds`, omitted when empty; each ID starts an independent tree. Every node must belong to
  exactly one declared tree. Unlisted disconnected nodes, duplicate ownership and cycles remain
  invalid; `orphanIds` is a reserved native marker, never a simple-tree fallback field.
- Empty, non-null parent IDs in C insert/move (empty QString in Qt addNode/moveNode) target the
  orphan-root list. Indexes follow child-list rules, including after-removal reordering. Moving
  an orphan under a node reattaches its entire tree. Only the main root is immovable/undeletable.
- Canvas **Add Orphan Node** creates an independent root and begins topic editing. **Detach
  Subtree** makes an attached non-main node an orphan. **Move** and node-to-node dragging reattach
  orphan roots; empty-space drops do not detach. Enter on any tree root adds a child. Home still
  focuses the main root. Removing a selected orphan root leaves no selected ancestor.
- Keep main-root protection (`root`) separate from tree-root appearance (`treeRoot`) in Qt
  presentation/properties. Each tree has its own initial fold depth and branch-color inheritance;
  automatic branch colors apply to children of any tree root. Moving under a collapsed parent
  preserves that fold and hides the moved subtree, as with ordinary reparenting.
- Traversal/export order is main tree then orphan trees in stored order. Markdown/HTML include
  each at root level. Outline JSON retains its main nested node and adds optional `orphans`
  containing nested trees; Qt outline entries restart at level 1. All projections retain descendants.
- `m3_layout_tree` accepts roots in input order, lays out each tree independently, then stacks
  component bounds vertically with `vertical_gap` separation. The first root stays at the origin;
  root centers share X=0. Folding retains full-tree geometry; placement is not persisted.

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
The reusable editor ships Simplified Chinese (`m3_zh_CN`), Japanese (`m3_ja`) and Korean
(`m3_ko`, locale `ko_KR`) Qt catalogs in `src/qt/translations/`. Qt builds require matching-major `LinguistTools`;
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
Shared property controls call `QCoreApplication::translate` with the explicit
`m3::qt::NodePropertiesPanel` context; do not hide their literals behind a helper, which
prevents `lupdate` from extracting the font presets and automatic color labels.

VNote packages the checked-in `.qm` files, like vtextedit, rather than m3's build outputs.
After editing the `.ts` files, run the Qt 5.15 `lrelease` tool from the m3 root for each
catalog and commit both source and binary files (Qt 6 can read these catalogs):
```powershell
lrelease src/qt/translations/m3_zh_CN.ts -qm src/qt/translations/m3_zh_CN.qm
lrelease src/qt/translations/m3_ja.ts -qm src/qt/translations/m3_ja.qm
lrelease src/qt/translations/m3_ko.ts -qm src/qt/translations/m3_ko.qm
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

### Node clipboard

Canvas Copy publishes `application/x-m3-nodes+json` (a valid native forest) plus the existing plain
text flavor. Include hidden descendants, collapse selected ancestor/descendant overlaps into one
copied root, order roots by document preorder, and keep only cross-links with both endpoints copied.
Text-input and link Copy must stay text-only, without committing an active text draft.

Native node Paste is handled before the host MIME hooks. It appends copied roots under one selected
node with fresh node/link IDs, validates the complete merged candidate, and uses `commitChange`
for one history publication. Do not implement it via repeated leaf insertion or `loadJson` (which
resets history). Reveal/select the first new root only after success and while the controller and
document revision still match. Read-only destinations refuse Paste; Copy remains available.
Malformed native data must report failure without mutating state or falling back to text/host MIME.

This is semantic node copying, not resource acquisition: URL/image records remain literal and use
the destination resource-base policy. Hosts still own asset files and nonnative MIME insertion.
Coverage: `m3_qt_clipboard` plus VNote's `nodeClipboardRoundTrip` integration case.

### Node-properties placement

Each editor owns one node-properties panel. `EditorConfig::separateNodePropertiesPanel`
is a construction-time host policy, default `false`: the embedded card starts collapsed and
retains its toggle preference across selection and document changes. With `true`,
`nodePropertiesPanel()` returns that same widget, initially hidden; hosts may reparent it into
a dock and control its geometry/visibility. No canvas toggle or collapse shortcut is shown,
and `collapseNodeProperties()` does nothing. Non-node selections clear and disable fields;
editor enabled/read-only policy still gates edits outside the editor subtree.

The editor destroys a surviving externally parented panel before its model/view. Hosts may
destroy the panel first; the accessor then returns null rather than creating a replacement.
Never create a second properties widget or share the panel between editors. VNote defaults
to separated mode and exposes the preference in Settings, effective on newly opened maps.

When `documentChanged` is emitted inside a properties `QLineEdit::setText()` / `textChanged`
operation, hosts must use `deleteLater()` to close the editor or panel: Qt still uses the line
control after the signal returns. Direct editor commands continue to support synchronous host
teardown; do not defer the property commit itself.

### Document lifecycle and host policy
- **New/Open/Close:** the constructor already creates a selected root. Use `newDocument(topic)` or `loadJson(bytes)` to replace it; failed imports preserve the current document, selection, camera and folds. In a full host, prompt Save/Discard/Cancel before replacing a dirty document or closing. Track the filename separately and update it only on successful load/save. Reuse `DemoWindow::mayReplace`, `openFile`, `saveFile`, and `closeEvent` in `examples/qt_demo_window.cpp` as the reference, not as library internals.
- **Blank input:** `loadJson()` treats zero bytes or only JSON whitespace (space, tab, CR, LF) as `newDocument()`: a selected `root` / `Central topic`, centered at 100%, with fresh history and no pending draft. Host loads remain allowed in read-only mode. Nonblank input retains the existing import rules (including optional-field trees); invalid input is never replaced with a default map. The Qt-free C JSON parser remains strict about blank input.
- **Initial camera:** constructor/New/Open reset to 100% and center the selected root, not the scene bounds. Root placement and explicitly requested hidden fits are coalesced across show/resize after the host attaches and lays out the widget; hosts must attach before returning to the event loop. Zoom/reset, panning and explicit centering cancel pending placement; fitting supersedes it. Later resizes preserve scale rather than continuously fitting. The empty-canvas menu orders Focus Main Node, Fit Width, Fit Page, 100%, Zoom In, Zoom Out.
- **Fit modes:** `fitToContents()` / command and shortcut field `fit` mean **Fit Page**, retaining the existing whole-scene geometry. `fitToWidth()` / `fitWidth` scale uniformly to the full scene width and permit vertical overflow; there is no command alias. Both remain one-shot camera operations, keep document/selection/inline drafts intact, and ignore empty or invalid scene bounds. Hidden requests retain Width versus Page through presentation refresh and deferred show/resize. `zoomMode()` distinguishes `ZoomMode::Custom`, `Width` and `Page`; `isZoomFit()` and the bool in `zoomChanged(qreal, bool)` mean either fit mode. Notifications include same-scale mode changes; hosts must query the enum rather than persist that bool. Explicit manual zoom, Ctrl+wheel and reset clear fit mode. These choices are view/session state, never native JSON or undo history.
- **Local folding:** `EditorConfig::initialVisibleLevels` is copied at construction and seeds folds only on successful New/Open, including blank input. The default is 3 visible levels: root=1, children=2, grandchildren=3; nonpositive values mean unlimited. Nodes at and below the frontier start collapsed, including hidden nodes and leaves. `setExpanded()` and outline/find reveals share one editor-local state; they never change JSON, emit semantic/history signals or consume undo/redo. Reveals stay open until explicitly collapsed or replaced. Edits and undo/redo keep folds for surviving IDs, prune deleted IDs and leave newly created/reintroduced IDs expanded; opening depth is never reapplied during edits or refreshes. Read-only editors allow folding through the API, Space/action and mouse affordance while semantic editing remains disabled. Node-context **Expand All** / **Collapse All** include the selected node and every descendant, preserving other branches and orphan trees; leaves disable both actions. Batch these folds through one controller preparation/install, not repeated per-node commands. The public two-argument `setExpanded()` remains single-node.
- **Folding geometry:** Qt measures the full tree in declared child order and uses `m3_layout_tree`; collapse/expand only filters visible nodes, edges and cross-links. Hidden nodes retain layout space in every direction (including Outline), so folding does not rebalance sides, compact siblings or center the camera. Selecting the ancestor of a newly hidden selection may still reveal it. Content/style/image changes can remeasure geometry; hidden image requests remain deferred until visible. The C `m3_mindmap_layout_json` API also lays out the complete tree, requires a valid measurement for every model node and emits every valid cross-link, independently of legacy expansion values.
- **Persistence and drafts:** Save/autosave must check `commitActiveEdit(true)` before `toJson()` so the snapshot includes the draft while the same input, caret, focus and local undo remain active. Repeated unchanged captures are no-ops; later typing becomes pending again, and Escape discards only changes since the last accepted snapshot. `commitActiveEdit()` without the flag still ends editing. Snapshots never commit implicitly; `toMarkdown()`/`toHtml()` are export projections, not editable backups. Exporting must not clear the dirty flag. Folding, selection, layout direction, zoom and pan remain host/view state.
- **Errors and changes:** check each command's `bool`/ID result and each snapshot for empty output; inspect `lastError()` on failure. Connect `errorOccurred` for interactive/presentation errors. `documentChanged` means a semantic edit committed, including New/Open, not a folding, selection or camera change. A later drawing failure does not roll back that edit; never clear dirty state in the error handler.
- **Commands and selection:** use `addNode`, `renameNode`, `moveNode`, `setExpanded`, `addLink`, etc. rather than editing controller/model internals. Hosts can present the existing editor-owned `commandAction(name)` in their own toolbar/menu; the public header lists stable names. Do not delete/reparent these borrowed actions or change their shortcut/enablement policy. Follow `layoutDirectionChanged` to synchronize host layout controls, blocking combo signals on both updates and failure resets. Keep returned IDs opaque and case-sensitive; do not assume an imported root is named `root`. Use `selectedNodeIds()` for groups: both arguments of `selectionChanged` and `selectedNodeId()` can be empty for a multi-selection. Disable single-node actions unless exactly one node is selected.
- **Link creation:** create links in the UI by dragging the selected node's arrow handle.
  There is no Add Link command, shortcut or creation dialog. Keep the programmatic `addLink()`
  API and the `editLink` properties dialog for existing links.
- **Configuration and resource bases:** `EditorConfig` is copied at construction. Replace/clear shortcut lists to rebind/disable keys without removing commands. Set `resourceBasePath` before creating the widget; an empty value captures the working directory. The base is fixed for the editor's lifetime: Open, Save As and `reloadImages()` do not rebase relative image/link references. Choose a stable workspace base or recreate the editor for a different document base; saving elsewhere does not rewrite stored paths.
- **Live editor preferences:** `EditorConfig::style.fontFamily` is a comma-separated fallback list; the first installed family wins, ignoring case and foundry suffixes. Empty/unavailable families inherit the host. `fontPointSize` is 0 (inherit) or 1–256 **points**, independently of family; per-node `style.fontSize` remains **pixels**. `setStyle()` remeasures the existing scene without document edits, fitting, or ending node/link inline drafts; explicit defaults survive host font/palette changes. Properties controls retain interface typography.
- **Automatic section numbers:** `EditorConfig::autoSectionNumbering` defaults to `true`. Main and orphan roots stay unnumbered; each tree's children restart at 1 and descendants follow full ordered child paths, including folded nodes. `sectionNumberPattern` accepts `1.1`, `1.1.`, or `1.1)` (default/fallback `1.1.`), followed by one space. `setSectionNumbering(enabled, pattern)` updates both live, without semantic signals/history changes or ending drafts. `NodePresentation::topic` and `NodeItem::topic` retain authored text; only measured/rendered labels and visual HTML receive `sectionNumber`. Draft seeds, save-while-editing checkpoints and scene-replacement comparisons must use authored topics. JSON, Markdown, outline, properties and HTML articles stay undecorated; authored numeric prefixes are never stripped.
- **Live behavior:** `setUndoLimit()` bounds retained commands (0 or negative means unlimited); lowering it prunes oldest undo and then farthest redo snapshots without removing the current snapshot or touching drafts. Increasing it cannot restore discarded history. `setConfirmSubtreeDeletion()` affects UI deletion only. `setAutoRandomBranchColor()` affects future direct children of any tree root, never existing colors. Shortcuts and relative-URL resolution remain construction-time policy.
- **URL activation and hooks:** connect `nodeLinkActivated(nodeId, url)` to the host's validated opening policy. Relative references arrive as absolute file URLs; do not blindly open arbitrary schemes or paths from imported documents. Override protected `canInsertFromMimeData(source, origin)` and `insertFromMimeData(nodeId, source, origin)` for host-owned paste/drop acquisition, or `onAddUrl`/`onAddImage` for app-owned pickers. Eligibility is pure: no IO, decoding, dialogs or edits. MIME is synchronously borrowed; snapshot it before modal UI. The default accepts only one absolute local-file drop and stores its encoded URL without committing an unrelated draft. Canvas paste commits its captured target's draft and rejects changed document/selection bindings; inline/properties text paste stays native. `readOnlyChanged` reports actual policy transitions after action updates.
- **Atomic children:** `addNodes(parentId, nodesJson, index)` inserts a nonempty array of complete native property records in one transaction/history step, preserving selection. Require string topics; caller-supplied `id`, `children` and `expanded` are forbidden. Generated IDs and children retain input order; invalid entries abort the whole candidate. Main-branch colors use the same unused-color palette policy as `addNode`, including colors assigned earlier in the batch.
- **Images:** the example intentionally supplies no image I/O, so image nodes keep placeholders. Connect `imageRequested(url, requestId)` before loading (or call `reloadImages()` after connecting). The host fetches/decodes and calls `provideImage(url, requestId, pixels)` on the GUI thread with the original URL/ID; null `QImage` means unavailable. Stale/duplicate responses are ignored. Use QObject contexts/`QPointer` for asynchronous lifetime safety and enforce scheme, path, size and network limits in the host. `examples/qt_demo_window.cpp` illustrates one loader; Qt Network is demo/host policy, not an editor dependency.

## HTML/SVG export boundary

`MindMapController::toHtml()` prepares committed visible content and asks `MindMapView` to fill
an `HtmlMap`. An export-only `prepare()` collector also retains cached article-image pixels for
all nodes, including folded descendants. It performs no I/O or requests, and ordinary preparation
allocates no article-image records. The detached `buildScene()` collector records the same node
rectangles, routed curves, leader lines, arrowheads and label positions used by the editor. Do not
introduce another router or read live selection/drag/camera geometry. Ordinary scene construction
has no HTML-record allocation. Public `toHtml()` returns a copied, natural-size HTML document;
there is no export mode, viewer toolbar, sizing script or nested scroll container.

`html_export.cpp` emits selectable HTML topic/icon/tag/link-label text at Qt-measured positions,
including paragraph direction, and inline SVG paths/polygons behind nodes (`pointer-events:none`).
Only supplied image pixels are PNG-encoded; visual and article images share one encoding cache.
There is no whole-map image, canvas, foreignObject or raster-size/downsampling fallback. Invalid
or non-finite map bounds fail. Escape text, attributes and CSS strings. Node-image URLs are never
fetched by the exported document. Its sole hosted presentation asset is the requested m3 PNG
favicon; generating the HTML still performs no network I/O.

Articles are an iterative preorder projection. Main and orphan roots restart at depth 1. Depths
1–6 are H1–H6 articles directly under `#articles`, without a forest-wrapping list. A depth-6
article's depth-7 children start a sibling UL; deeper child lists remain under their parent's LI.
List titles use `p.m3-article-title`, not invalid deeper heading tags. Close each list before the
next heading or root. Use real outside markers with one gutter, not cumulative nested indentation.
Set heading sizes explicitly to avoid older Chromium's section-ancestor H1 demotion and keep deep
headings readable. Never insert an orphan category title or filter authored topic text.

Each article's direct rows are `.m3-article-title`, icons, tags, URL, cross-references, note, figure,
in that order; skip absent fields. Icons and references do not live in the heading. Safe
http/https/mailto links contain an aria-hidden link indicator and `.m3-article-url-text`; unsafe
URLs stay inert text without a link indicator. References preserve direction, self/parallel edges
and authored-label tooltips. Notes are escaped plain-text paragraphs; figures use supplied pixels
or explicit placeholders. IDs remain `m3-node-<UTF8 hex>` for visual-topic/reference navigation.

Host-visible geometry contract: `#visual-map` directly contains relative, in-flow `#mindmap` with
positive natural CSS-pixel `data-width`/`data-height` and matching dimensions. Node/SVG coordinates
remain map-local, translated by detached bounds. Visual IDs use `m3-map-node-<UTF8 hex>`. No main or
article width caps, centering, transforms, fitting or JavaScript; ordinary document scrolling exposes
large maps. Print hosts reserve the scaled rectangle on `#visual-map`, make its map child absolute,
and apply CSS layout zoom only to that child. Never zoom ancestors or let natural-width overflow
shrink the whole PDF. VNote preserves map-first/article-next pagination, list markers, and 12pt body.
Print chrome hiding stays `main > h1`, not article headings; `.m3-article-title` keeps with next.

Coverage in `m3_qt_html` / `m3_qt_html_edges` compares parsed DOM/SVG with actual scene geometry and
paint primitives, including large maps, images, safe content, all layout directions and self/parallel/
bidirectional links. Image-resize and demo export cases retain nonmutation/snapshot ownership checks.
Windows offscreen tests use the installed Windows font directory so line measurements are real.

## Painter rendering boundary

`MindMapEditor::renderMap(QPainter &, const QRectF &)` paints the committed visible map into a
centered, aspect-fitted rectangle through the same detached `buildScene()` used by HTML export.
It allocates no HTML records and does not rasterize the map. The current layout, folds, palette,
cached image pixels and placeholders remain authoritative; there is no resource I/O or implicit
draft commit. Camera, selection, handles, properties panels and editor history are not rendered or
mutated. Painter state is restored. Reject inactive painters and non-finite/empty target or map
bounds; zero/negative origins are valid. The host owns QPrinter, page margins/ranges and any draft
acceptance. Keep Qt PrintSupport out of m3. VNote exercises the public API through its real-window
`test_mindmapviewwindow_print` regression target.

## Code Conventions & Common Patterns
- Match nearby compact C++ formatting: four spaces, same-line braces, header guards. C API functions use `m3_*`, core helpers use snake_case, Qt types use PascalCase and methods/signals camelCase; namespaces are `m3` and `m3::qt`.
- Use RAII (`std::unique_ptr`, Qt parent ownership). C inputs are borrowed NUL-terminated UTF-8; release output snapshots with `m3_string_free`, layout results with `m3_layout_result_free`, and handles with `m3_mindmap_destroy`. Output slots are nulled on failure.
- Reuse `m3::require`/`Failure` internally and `boundary` in `src/api.cpp` to translate exceptions to `M3Status` and thread-local `m3_last_error()`. No C++ exception may cross the C ABI. Validate/allocate before committing mutations; follow the existing prepare-then-swap pattern so failures leave documents unchanged.
- Preserve immutable case-sensitive IDs, child order and opaque style keys. Native records reject unknown fields and duplicate JSON members; import compatibility formats have separate rules. Preserve wire spellings such as `hyperLink`. Compare parsed JSON, not formatting/key order.
- Qt commands are synchronous and require the GUI thread with a host `QApplication`. Independent core handles may run concurrently; callers synchronize shared handles. Use context-bound deferred Qt callbacks, not an invented worker-thread pipeline.
- Configure shortcuts through `EditorConfig`; extend resource acquisition through the protected synchronous MIME hooks. Paste/drop targets are captured before host callbacks; never retarget them from a later selection. Reuse controller signals and QObject ownership rather than introducing a dependency-injection framework.
- Semantic success emits `documentChanged`; a later rendering failure does not roll back the committed edit. Preserve the distinction between semantic failure and presentation failure, and keep uncommitted inline drafts out of snapshot exports.
- Emoji picker backgrounds and captions use the host's `CE_ItemViewItem` rendering, not
  palette-only fills: host QSS may make `Highlight` transparent and `AlternateBase` equal to
  the normal background. Paint the current selection as active while keyboard focus stays
  in the input. Keep its `Text`-colored outline: native styles may share selection/hover fills.
- Link-creation and expansion controls share palette roles: `ButtonText` foreground on
  `Base` at rest, `HighlightedText` on `Highlight` while hovered. Keep link creation
  neutral at rest rather than using the accent color for its glyph.

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
- Visual affordance regressions share gesture helpers such as `expansionPoint()` for target
  coordinates and render the node's scene bounds. Do not duplicate control offsets or sizes
  inside color checks; retain palette and hover/fold/leave/hide behavior checks.
- CI covers Windows/Linux core and Windows Qt, Debug/Release and shared/static core. Packaging changes also require the installed consumers and core-without-Qt checks in CI; these are not CTest registrations. No coverage percentage is configured.
