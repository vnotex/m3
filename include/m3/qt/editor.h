#ifndef M3_QT_EDITOR_H
#define M3_QT_EDITOR_H
#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QKeySequence>
#include <QList>
#include <QVector>
#include <QWidget>
#include <memory>

class QAction;
class QMenu;
class QMimeData;
class QPainter;
class QRectF;

#ifdef M3_QT_STATIC
#define M3_QT_API
#elif defined(M3_QT_BUILD_DLL)
#define M3_QT_API Q_DECL_EXPORT
#else
#define M3_QT_API Q_DECL_IMPORT
#endif

// Qt C++ ABI: hosts supply QApplication on the GUI thread and a matching Qt kit.
// Enable M3_BUILD_QT, then find_package(m3 COMPONENTS qt_editor) and link m3::qt_editor.
// Component-free/core-only consumers remain independent of Qt. M3_BUILD_QT_DEMO
// additionally builds the file-handling example; this widget owns no file policy.
// The widget owns its model; snapshots and commands never expose a core handle.
// It contains the canvas, properties card and diagnostics, but no toolbar rows or
// layout picker. Hosts (including the demo) own toolbar presentation.
// New editors contain the selected root "root" / "Central topic". New/load
// replace it atomically, select and center the new root at 100%; failed imports retain
// the document, selection, camera and folds. Successful New/Open initializes local
// folding from EditorConfig::initialVisibleLevels, ignoring legacy expansion values.
// Import accepts native JSON, optional-field nested
// trees, and Mind Elixir. toJson is a copied native JSON snapshot, including
// opaque metadata, or empty on error. toMarkdown is a separate copied text
// projection of the committed model, empty on error; it never changes the
// selection, layout, or document and does not commit an inline draft.
// Mutations return false/empty on semantic failure. After a successful mutation,
// a drawing failure is reported without claiming rollback: the error scene is
// retried on the next refresh. documentChanged fires once per semantic success;
// unchanged node-property edits do not emit it.
// IDs are case-sensitive. Index -1 appends; move indexes apply after removal.
// Only visible nodes/links can be selected; hidden data is preserved. Links are
// exclusive. Shift/Ctrl-click toggles nodes in a group; plain click selects one.
// Right-clicking a group member retains the group and offers Delete and Copy.
// Multiple nodes support only Delete as a mutation: no properties or editing gestures.
// Viewport/help and selection controls remain available. Delete removes selected
// subtrees once, including overlaps, in one document change and confirmation.
// A selection containing the main root cannot be deleted. After group deletion, select
// the visible parent of the first uncovered subtree; deleting an orphan root leaves
// no selected ancestor.
// Selection, folding, layout direction, zoom/pan and fit never change persisted JSON.
// Shift+vertical wheel scrolls horizontally; with normal left-to-right controls,
// down/negative deltas move the viewport right and up/positive deltas move it left.
// Ctrl+wheel (including Ctrl+Shift) zooms at the pointer. Unmodified and native
// horizontal wheel behavior is unchanged.
// Drag empty space with the left mouse button to pan; the middle button pans anywhere.
// Empty space shows an open hand cursor, closing during a pan. Empty clicks clear selection.
// Node context menus separate creation from editing/reordering and omit viewport actions.
// Expand All/Collapse All fold that node and all its descendants, including in read-only maps.
// Other branches and orphan trees retain their folds; these actions are disabled on leaves.
// With no selection the menu offers Focus Main Node, Fit Width, Fit Page, 100%, Zoom In and Zoom Out,
// followed by Add Orphan Node and Shortcuts Help (the existing keyboard shortcuts overlay).
// Move and Detach Subtree act on non-main nodes; detach is available only for an attached
// subtree. Collapse/Expand is also available as a command.
// Drag a non-main node, including an orphan root, onto another node to reattach it.
// Dropping on empty space cancels; detachment requires Detach Subtree.
// A single selected node shows a small arrow outside its top-right corner. Drag it onto
// another visible node to create an undirected cross-link without moving either node.
// Escape, an interrupted gesture, or an invalid drop cancels without changing the document.
// A selected link shows two fixed-size endpoint squares. Drag either square onto
// a visible node to reconnect only that endpoint, including self-links. Motion is
// a preview; release commits once. Escape, interrupted gestures and invalid or
// unchanged drops leave the document intact. Handles are not persisted or exported.
// Link menus contain Rename/Edit, a separator, exclusive ---, <---, --->, <--->
// direction choices, a separator, and Delete. Arrows are relative to stored
// source/target order, not screen position. Link properties is the editLink command.
// directed=false draws no arrows; otherwise style.arrowDirection "backward" or
// "both" selects source-only or both arrows. Missing/other values draw target-only.
// These Qt style keys survive native JSON; unrelated style data stays opaque.
// updateLink and the link-properties Directed checkbox retain this style preference.
// Selecting a node or accepting an inline topic scrolls its full bounds into view
// without changing zoom; oversized nodes can only be partially shown by scrolling.
// Presenting expanded embedded node properties (including for a different selected
// node) also scrolls the node clear of the panel, preserving zoom and keyboard focus.
// If it cannot fit, scrolling exposes the largest portion in a clear viewport region.
// An already-presented panel does not constrain subsequent pan or property edits.
// Startup centering and explicit fit/centering commands retain their camera placement.
// Folding is editor-local view state, not a semantic edit or undo command.
// It hides/shows descendants without rebalancing branches or compacting rows;
// hidden nodes retain layout space, so visible node positions and the camera stay put.
// Collapsing a selected descendant still selects/reveals its ancestor. Explicit
// focus/fit commands remain available; expanded branches may extend beyond the viewport.
// Topics/labels are plain Unicode text. Embedded NULs are rejected.
// A single selected node has a properties panel. By default it is an embedded card,
// initially collapsed; other selections hide the card. With separateNodePropertiesPanel,
// the host presents the same panel externally and controls its geometry and visibility.
// The external body stays expanded, clearing/disabling its fields for other selections.
// Appearance, tags, icons, URL, Image URL and note edits persist immediately without changing
// selection or zoom. Blank Image URL hides the image while retaining its dimensions.
// Images appear beneath tags, preserving decoded aspect ratio within stored bounds;
// unspecified dimensions use natural size, capped at 240 without upscaling. Display
// is capped at 4096 scene units without rewriting imported metadata. Pending/failed
// resources reserve a placeholder. Selected decoded images have a proportional
// resize handle: motion previews only; release persists one width/height edit.
// Escape or interrupted gestures cancel without a semantic change. Loading images
// never changes the document; hosts supply pixels through the GUI-thread protocol.
// Tags/icons accept comma-separated entries. Icons display as
// literal Unicode text above the topic, wrapping within the node; names are not
// mapped to an icon library. The Icons field opens a searchable, categorized
// Unicode 15.1 emoji picker on focus; a choice replaces the current entry or a
// selection within it, never adjacent entries. Ctrl+H/J/K/L moves the picker
// selection left/down/up/right without editing the input text.
// The automatic navigation tooltip is shown once per QApplication lifetime.
// Direct typing still saves immediately; glyph availability depends on host fonts.
// Empty icons reserve no space. Nonempty URLs
// show an indicator beside the topic. Activating it emits nodeLinkActivated with
// the node ID and target: relative paths become absolute file URLs unless
// resolveRelativeUrls is false. The host decides whether and how to open it.
// Canvas node Copy publishes a native forest (application/x-m3-nodes+json) plus text.
// It includes hidden descendants, deduplicates overlapping selections in document order,
// and retains links only when both endpoints were copied. Paste appends child subtrees
// under one selected node, regenerates node/link IDs, and selects/reveals the first new root
// in one undoable edit. Text-input/link Copy stays text-only; read-only editors allow Copy.
// Resource references remain literal and use the destination editor's resource base;
// resource files are not copied. Invalid native data fails without a text/host fallback.
// Other canvas clipboard/drop data dispatch synchronous MIME hooks without changing selection.
// Standalone editors accept only single absolute local-file drops as hyperlinks for that data.
// Collapsing the card leaves only its top-right toggle, without changing selection.
// The expanded/collapsed preference is local to this editor and survives selection
// and document changes; links/groups/empty selection hide either form without resetting it.
// The renderer recognizes style.color/background, fontSize (1-256 pixels, numeric
// or "Npx"), fontWeight (normal/bold or CSS weight 100-900), and fontStyle
// (normal/italic, inheriting the editor font when unset), and branchColor (a QColor
// string). Branch color controls a node border and its incoming tree edge, inherited
// by descendants until their own valid override; cross-links are unchanged. Invalid
// or absent colors inherit the nearest colored ancestor, or the normal appearance.
// The node-only "Branch Color" context submenu offers Auto and the properties
// palette. Auto removes only that node's override, not descendant overrides; its
// checkmark reflects local intent, not inherited color. Selection/drop feedback
// retains thicker/dashed borders without replacing an explicit branch color.
// Reset appearance clears only font/text/fill, preserving branchColor and opaque
// style data. Imported invalid values remain unchanged until explicitly edited.
namespace m3::qt {
struct OutlineEntry { QString id; QString topic; int level = 1; };
struct FindResult { int totalMatches = 0; int currentMatch = -1; };
// Widget policy, initialized at construction; style and behavior have live setters.
// No Qt-specific configuration enters the core.
// Replace a shortcut list to rebind it, or clear it to disable its keyboard binding
// without removing the command action. Avoid assigning the same sequence to
// multiple map commands. Bindings are local to this widget; link/move dialogs and
// the layout picker and properties inputs keep their normal input keys.
// With a selected node and canvas focus, B/I/R toggle bold/italic or reset appearance
// without expanding the properties card or moving focus. C/F expand and reveal the
// Text/Fill color mode; T/O/N expand and reveal Tags/Icons/Note, focusing that control.
// While properties are expanded, unhandled digits reaching the editor select a
// swatch in the current Text/Fill mode: row (1-4), then column (1-6); 11 selects Auto.
// Child inputs and active shortcuts take priority. Changing focus or interrupting
// the code cancels a partial entry. Digits in text fields remain ordinary input.
// E begins inline topic editing for nodes only; F2 and double-click edit node or
// link topics inline. Node double-click editing accepts the topic label or blank
// space inside the node, including empty topics. Images, tags, icons and the
// expand/collapse button remain excluded; blank canvas space does not start editing.
// Plain left double-clicks on decoded images emit nodeImageActivated, including in
// read-only mode. The host owns the viewer; placeholders and resize handles do not activate.
// Expand/collapse toggles on each left-button press, including rapid repeated clicks.
// Link topics are literal text, without node hashtag parsing.
// These letters remain ordinary input while typing, including in the inline draft.
// P toggles the selected node's embedded properties card, keeping focus on the canvas.
// Separate panels have no collapse toggle or P/Escape collapse binding; property
// commands never reopen a host-hidden panel or scroll the canvas clear of it.
// With canvas focus, ? opens shortcut help for any selection. Escape or an outside
// click dismisses it; Escape returns focus to the canvas without clearing selection.
// acceptTopic applies only during
// inline editing, while map shortcuts are suspended. UI node creation inserts a
// blank node and starts inline editing. Dragging the create-link arrow creates an
// undirected link, selects it and starts inline editing. There is no link-creation
// shortcut or dialog. Escape cancels only the draft, keeping the created node
// or link; clicking outside saves. Programmatic addNode/addLink do not start editing.
// Example: config.shortcuts.addChild = {QKeySequence(QStringLiteral("Ctrl+Shift+J"))};
//          MindMapEditor editor(config);
struct EditorConfig {
    struct Style {
        // Comma-separated fallback list; empty or unavailable inherits the host.
        QString fontFamily;
        // 0 inherits the host; otherwise 1..256 points. Node sizes remain pixels.
        int fontPointSize = 0;
    } style;
    struct Shortcuts {
        QList<QKeySequence> undo{QKeySequence(QKeySequence::Undo)};
        QList<QKeySequence> redo{QKeySequence(QKeySequence::Redo)};
        QList<QKeySequence> addChild{QKeySequence(Qt::Key_Tab), QKeySequence(Qt::Key_Insert)};
        // Tree roots have no sibling: Enter/Shift+Enter append a child there.
        QList<QKeySequence> addSibling{QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)};
        QList<QKeySequence> addSiblingBefore{QKeySequence(Qt::SHIFT | Qt::Key_Return), QKeySequence(Qt::SHIFT | Qt::Key_Enter)};
        QList<QKeySequence> editSelection{QKeySequence(Qt::Key_F2)};
        // Inline Enter or Ctrl+Enter accepts; Shift+Enter inserts a newline.
        QList<QKeySequence> acceptTopic{QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter),
                                       QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)};
        QList<QKeySequence> deleteSelection{QKeySequence(Qt::Key_Delete)};
        QList<QKeySequence> toggleExpanded{QKeySequence(Qt::Key_Space)};
        QList<QKeySequence> moveNode{QKeySequence(Qt::CTRL | Qt::Key_M)};
        QList<QKeySequence> moveUp{QKeySequence(Qt::CTRL | Qt::Key_Up)};
        QList<QKeySequence> moveDown{QKeySequence(Qt::CTRL | Qt::Key_Down)};
        // Logical tree navigation, independent of layout direction. A collapsed
        // node has no selectable child; Space expands it before navigating.
        // With canvas focus, Ctrl+H/J/K/L also navigate left/down/up/right.
        QList<QKeySequence> selectParent{QKeySequence(Qt::Key_Left), QKeySequence(Qt::CTRL | Qt::Key_H)};
        QList<QKeySequence> selectChild{QKeySequence(Qt::Key_Right), QKeySequence(Qt::CTRL | Qt::Key_L)};
        QList<QKeySequence> previousSibling{QKeySequence(Qt::Key_Up), QKeySequence(Qt::CTRL | Qt::Key_K)};
        QList<QKeySequence> nextSibling{QKeySequence(Qt::Key_Down), QKeySequence(Qt::CTRL | Qt::Key_J)};
        // Focus main node: select the root, center it, and focus the canvas.
        QList<QKeySequence> selectRoot{QKeySequence(Qt::Key_Home)};
        QList<QKeySequence> clearSelection{QKeySequence(Qt::Key_Escape)};
        QList<QKeySequence> zoomIn{QKeySequence(QKeySequence::ZoomIn), QKeySequence(Qt::CTRL | Qt::Key_Equal)};
        QList<QKeySequence> zoomOut{QKeySequence(QKeySequence::ZoomOut)};
        QList<QKeySequence> resetZoom{QKeySequence(Qt::CTRL | Qt::Key_0)};
        QList<QKeySequence> fit; // Fit Page.
        QList<QKeySequence> fitWidth;
        QList<QKeySequence> toggleBold{QKeySequence(Qt::Key_B)};
        QList<QKeySequence> toggleItalic{QKeySequence(Qt::Key_I)};
        QList<QKeySequence> resetStyle{QKeySequence(Qt::Key_R)};
        QList<QKeySequence> textColor{QKeySequence(Qt::Key_C)};
        QList<QKeySequence> fillColor{QKeySequence(Qt::Key_F)};
        QList<QKeySequence> editTags{QKeySequence(Qt::Key_T)};
        QList<QKeySequence> editIcons{QKeySequence(Qt::Key_O)};
        QList<QKeySequence> editNote{QKeySequence(Qt::Key_N)};
        QList<QKeySequence> editTopic{QKeySequence(Qt::Key_E)};
        // Question-mark events may retain Shift on layouts that use it for ?.
        QList<QKeySequence> showHelp{QKeySequence(Qt::Key_Question), QKeySequence(Qt::SHIFT | Qt::Key_Question)};
        QList<QKeySequence> toggleProperties{QKeySequence(Qt::Key_P)};
    } shortcuts;
    // Maximum completed document commands; 0 is unlimited. Full native snapshots
    // retain embedded images/opaque data, so this bounds commands, not bytes.
    int undoLimit = 100;
    // Every tree root is level 1; positive values limit initially visible levels, nonpositive
    // means unlimited. Copied at construction; applied on successful New/Open only.
    int initialVisibleLevels = 3;
    // UI policy only: the removeNode() API never prompts.
    bool confirmSubtreeDeletion = true;
    // Filesystem directory for relative image/link references; empty captures cwd.
    // Relative bases become absolute at construction, without canonicalizing or
    // requiring existence. Mutable through setResourceBasePath; never persisted.
    QString resourceBasePath;
    // False passes image request and activation URLs unchanged to the host,
    // including relative/protected resource identifiers. No library I/O occurs.
    bool resolveRelativeUrls = true;
    // New direct children of any tree root (UI or addNode) get a random palette color.
    // Prefer colors unused by that tree's direct branches, comparing their effective
    // QColor values (local override or inherited root color); nested overrides
    // do not reserve colors. Once all 22 non-white colors are used, allow repeats.
    // White remains available manually; existing white branches are preserved.
    // Enabled by default. Creation only: loading/moving nodes never recolors them.
    // The setting is copied, not persisted; each assigned branchColor is persisted.
    bool autoRandomBranchColor = true;
    // Display-only hierarchy numbers; every tree root remains unnumbered.
    bool autoSectionNumbering = true;
    // Supported patterns: "1.1", "1.1.", "1.1)"; other values use "1.1.".
    QString sectionNumberPattern = QStringLiteral("1.1.");
    // Construction-time placement of the editor's sole node-properties panel.
    // False embeds an initially collapsed card; true leaves presentation to the host.
    // Not persisted in the document and not changeable on an existing editor.
    bool separateNodePropertiesPanel = false;
};
class M3_QT_API MindMapEditor : public QWidget {
    Q_OBJECT
public:
    // Outline shows depth-first indented rows with square tree connectors.
    enum class LayoutDirection { Balanced, Right, Left, Outline };
    Q_ENUM(LayoutDirection)
    enum class ZoomMode { Custom, Width, Page };
    Q_ENUM(ZoomMode)
    enum class MimeInsertionSource { Clipboard, Drop };
    explicit MindMapEditor(QWidget *parent = nullptr);
    explicit MindMapEditor(const EditorConfig &config, QWidget *parent = nullptr);
    ~MindMapEditor() override;
    // GUI-thread appearance/policy changes; no document edit or draft commit.
    void setStyle(const EditorConfig::Style &style);
    // Applies both numbering preferences in one refresh; unchanged normalized values are a no-op.
    void setSectionNumbering(bool enabled, const QString &pattern);
    // Lowering a positive limit discards excess undo/redo states; 0 is unlimited.
    void setUndoLimit(int limit);
    void setConfirmSubtreeDeletion(bool enabled);
    // Affects future direct children of any tree root only, never existing colors.
    void setAutoRandomBranchColor(bool enabled);
    // Borrowed command action by stable, case-sensitive name; null for empty/missing.
    // The editor owns lifetime, shortcuts and enablement. Hosts may present actions
    // in toolbars/menus, but must not delete, reparent or override those policies.
    // Names: undo, redo, addChild, addOrphan, addSibling, addSiblingBefore, editSelection,
    // deleteSelection, toggleExpanded, moveNode, detachNode, moveUp, moveDown, editLink,
    // zoomIn, zoomOut, resetZoom, fit (Fit Page), fitWidth, selectRoot, selectParent, selectChild,
    // previousSibling, nextSibling, copy, paste, clearSelection, toggleBold, toggleItalic,
    // resetStyle, textColor, fillColor, editTags, editIcons, editNote, toggleProperties,
    // editTopic, showHelp. Triggering retains the same checked inline-commit behavior.
    // addOrphan creates an independent tree and starts topic editing; detachNode
    // detaches an attached non-main subtree. Neither has a default shortcut.
    // toggleBold/toggleItalic are checkable and reflect the selected node's parsed
    // appearance (including defaults), not QAction's optimistic activation state.
    // Toolbar formatting: fontSize is a QWidgetAction creating a size combo for each
    // presentation; textColorPopup, fillColorPopup and iconsPopup carry QMenus.
    // The editor owns these actions, menus and widgets. Hosts add the actions to a
    // toolbar and may supply icons and InstantPopup button mode; do not take ownership.
    // They format one writable selected node, commit an inline draft before applying
    // a size or opening a menu, and have no shortcuts. Opening/synchronizing is not
    // a semantic edit and never changes the properties card's expanded state.
    // Color choices close their menu; Icons saves each edit and stays open. Controls
    // close when their document/selection/policy or presenting window is invalidated.
    QAction *commandAction(const QString &name) const;
    // The sole panel, only in separated mode; otherwise null. Repeated calls return
    // the same widget, initially hidden and parented to this editor, never a factory.
    // Hosts may reparent/show/hide it. The editor deletes any surviving panel before
    // tearing down its model, even while externally parented. Independent deletion
    // by the host is supported: subsequent calls return null, with no replacement.
    QWidget *nodePropertiesPanel() const;
    // Collapse the embedded card and focus the canvas, including read-only or no selection.
    // Idempotent; keeps document, selection and zoom. No effect in separated mode.
    void collapseNodeProperties();
    QString resourceBasePath() const;
    // Normalize as at construction and invalidate/re-request image resources.
    // Document, selection, inline draft and camera remain unchanged.
    void setResourceBasePath(const QString &path);
    // GUI-thread host image protocol. Connect imageRequested before loading, or
    // call reloadImages after attaching. The URL follows resolveRelativeUrls;
    // supply that URL and request ID unchanged. Null pixels mean unavailable.
    // Only the first matching response is accepted; stale/unknown IDs are ignored.
    // Requests are deferred, editor-local, and monotonically numbered across loads.
    // Hosts own decoding/I/O; retain asynchronous editors with QObject context or
    // QPointer. Without a response, the widget keeps a placeholder and does no I/O.
    void provideImage(const QString &url, quint64 requestId, const QImage &image);
    // Clears cached success/failure/pending responses without changing the base,
    // document, selection, zoom or inline draft; requests visible images again.
    void reloadImages();
    // Successful New/Open resets local folding to initialVisibleLevels.
    bool newDocument(const QString &topic = QStringLiteral("Central topic"));
    // Empty or JSON-whitespace-only bytes create the default root, as newDocument().
    // Other input follows the normal import rules; failure preserves the current map.
    // Like any successful load, blank input resets history and centers the selected root at 100%.
    bool loadJson(const QByteArray &json);
    // Accept an inline node/link draft without changing selection or camera.
    // By default editing ends. keepEditing retains the same input, document,
    // caret, focus and local undo; Escape then abandons only later changes.
    // Save/autosave should call commitActiveEdit(true), then toJson().
    // Returns true for no changed draft or successful acceptance. Rejection
    // leaves text, caret, focus and the previous draft baseline intact.
    // Snapshots never commit implicitly.
    bool commitActiveEdit(bool keepEditing = false);
    bool hasPendingEdit() const;
    // Cancels drafts/gestures and gates all semantic commands, not the widget.
    // Selection, copying, folding, navigation and camera controls remain available.
    // Host-controlled newDocument/loadJson are permitted in read-only mode.
    void setReadOnly(bool readOnly);
    bool isReadOnly() const;
    bool canUndo() const;
    bool canRedo() const;
    // Accept a changed inline draft first, then traverse one document command.
    // A rejected draft/restoration leaves the cursor unchanged. Read-only or no
    // available command returns false. Text-input shortcuts keep local history.
    // Selection is restored; camera/resources/folding are not history. Surviving IDs
    // keep local folds; removed IDs are pruned and reintroduced IDs start expanded.
    // Save retains history;
    // successful newDocument/loadJson starts a fresh baseline.
    bool undo();
    bool redo();
    QByteArray toJson() const;
    // Copied Markdown text of the committed forest, each tree root at level 1.
    // Empty on error. No selection,
    // layout, or document changes; an active inline draft remains uncommitted.
    QString toMarkdown() const;
    // Copied standalone HTML: selectable node text and inline SVG connectors for
    // the current-layout visible map, followed by readable articles for all trees.
    // The full map stays at natural size in document flow, without a nested viewer.
    // Articles use tree-relative H1-H6 titles, then lists for deeper nodes, with
    // separate icon, tag, URL and cross-reference rows before notes and images.
    // Supplied node images are embedded, including cached images of hidden nodes;
    // this performs no resource I/O and does not rasterize the diagram. The main root
    // supplies the title. Committed data only; no editor state changes. Empty on error;
    // the snapshot survives edits and widget destruction.
    QString toHtml() const;
    // Paint the committed visible map centered and aspect-fitted inside target.
    // Vector output with the editor palette, cached images and current folds/layout;
    // independent of camera/selection, without editor controls or selection handles.
    // No draft commit, resource I/O or editor-state changes. The painter must be active
    // and target must have finite coordinates and positive dimensions. False on error.
    bool renderMap(QPainter &painter, const QRectF &target) const;
    QString lastError() const;
    // Empty parentId creates an orphan tree; otherwise inserts a child. -1 appends.
    QString addNode(const QString &parentId, const QString &topic, int index = -1);
    // Atomically insert complete node-property objects, keeping the current selection.
    // nodesJson is a nonempty array; each entry requires a string topic and forbids
    // id, children and expanded. Generated IDs follow input order; failure returns
    // an empty list without mutation. One successful batch consumes one undo step.
    QStringList addNodes(const QString &parentId, const QByteArray &nodesJson, int index = -1);
    bool renameNode(const QString &id, const QString &topic);
    // Copied complete committed node record, empty on error. IDs are immutable.
    QByteArray nodeJson(const QString &id) const;
    // Validated native node patch. A style object merges members, null members
    // remove keys; unrelated/opaque style data survives. Unchanged patches emit
    // no documentChanged. Read-only or invalid patches return false. Expansion
    // patches are rejected; use setExpanded() for editor-local folding.
    bool updateNode(const QString &id, const QByteArray &patch);
    // Removes a subtree, including orphan roots. The main root is protected.
    bool removeNode(const QString &id);
    // Empty parentId detaches/reorders an orphan; a node ID attaches it there.
    // Index is after removal from the old list; -1 appends. Main root cannot move.
    bool moveNode(const QString &id, const QString &parentId, int index = -1);
    // View-only, including read-only editors: no JSON/history/semantic signals.
    // Idempotent; missing IDs fail without changing folds or selection.
    bool setExpanded(const QString &id, bool expanded);
    QString addLink(const QString &sourceId, const QString &targetId, bool directed, const QString &topic = {});
    bool updateLink(const QString &id, const QString &sourceId, const QString &targetId, bool directed, const QString &topic);
    bool removeLink(const QString &id);
    // Complete preorder: main tree, then ordered orphan trees, including collapsed
    // descendants. Each tree root is level 1; the main root remains the title.
    // Empty output reports an error for a live document.
    QVector<OutlineEntry> outline() const;
    // Select and scroll at the current zoom, opening collapsed ancestors without
    // changing document history or JSON. Revealed ancestors stay open until folded
    // explicitly or New/Open resets them; edits retain folds for surviving IDs.
    // Available read-only; invalid IDs leave the selection unchanged.
    bool revealNode(const QString &id);
    // One match per node/link topic: node preorder, then links in ID order.
    // Repeated calls wrap; a changed query starts first/last by direction.
    // Incremental calls retain the previous matching target or choose the first.
    // currentMatch is zero-based, or -1 with no current result. Edits invalidate
    // cached matches; successful New/Open also clears the previous search.
    // Empty input clears Find; no match leaves selection/camera unchanged.
    FindResult findText(const QString &text, Qt::CaseSensitivity sensitivity,
                        bool backward = false, bool incremental = false);
    // Clear Find state without changing selection, camera or local folding.
    void clearFind();
    bool selectNode(const QString &id);
    bool selectLink(const QString &id);
    void clearSelection();
    // The singular getter is empty for zero or multiple selected nodes.
    QString selectedNodeId() const;
    // Full node selection in click order; empty when a link is selected.
    QStringList selectedNodeIds() const;
    QString selectedLinkId() const;
    // Selection in the focused owned text input (including an external properties
    // panel), otherwise selected node topics in selection order joined by LF,
    // or the selected link topic.
    QString selectedText() const;
    bool setLayoutDirection(LayoutDirection direction);
    LayoutDirection layoutDirection() const;
    // One-shot camera fitting; hidden requests wait for a visible viewport.
    // Fit Page preserves the whole-map geometry; Fit Width permits vertical overflow.
    // Empty or invalid scene bounds leave the camera and mode unchanged.
    void fitToContents();
    void fitToWidth();
    // View-only controls; retain document, selection and inline draft.
    // Current canvas scale; fit may produce values outside the step-zoom range.
    qreal zoomFactor() const;
    ZoomMode zoomMode() const;
    // True for Width or Page, including while fitting awaits a visible viewport.
    bool isZoomFit() const;
    void zoom(qreal factor);
    void resetZoom();
    // Positive steps scroll right/down, negative steps left/up, using native
    // scrollbar single-step actions.
    void scrollSteps(int horizontal, int vertical);
    // Commit any inline draft and select the document root. For a shown editor,
    // also center it and focus the canvas without changing zoom. Navigation alone
    // does not change the document. Returns false if the draft is rejected or
    // the root cannot be selected.
    bool focusRoot();
signals:
    // View-only scale or zoomMode() changes, including new/load reset and deferred fit.
    // fit means Width or Page; query zoomMode() to distinguish them.
    // Mode transitions emit even at the same scale; pan never emits.
    void zoomChanged(qreal factor, bool fit);
    // Committed semantic changes only, never draft typing or camera/selection/folding.
    void documentChanged();
    // View-only: emitted after a successful actual layout change, including API calls.
    void layoutDirectionChanged(m3::qt::MindMapEditor::LayoutDirection direction);
    // Availability includes read-only policy; emitted only on boolean transitions.
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    // Actual policy transitions only, after action/policy updates.
    void readOnlyChanged(bool readOnly);
    // Only transitions between a draft differing from its original and no
    // changed draft. Cancel/accept emits false; it does not imply a saved map.
    void pendingEditChanged(bool pending);
    // Fires for every membership change. For multiple nodes both arguments are
    // empty; query selectedNodeIds() to distinguish a group from no selection.
    void selectionChanged(const QString &nodeId, const QString &linkId);
    void nodeLinkActivated(const QString &nodeId, const QString &url);
    void nodeImageActivated(const QString &nodeId, const QImage &image);
    void imageRequested(const QString &url, quint64 requestId);
    void errorOccurred(const QString &message);
    // Extend the no-selection menu after any inline draft is committed.
    // Connect directly on the GUI thread; the populated menu is borrowed only
    // during this call. Hosts own their actions and file/export policy.
    void backgroundContextMenuRequested(QMenu *menu);
protected:
    // Native node clipboard data is handled internally before these resource hooks.
    // Pure, cheap eligibility query: no IO, decoding, dialogs or document changes.
    // The default accepts one absolute local-file URL for Drop, never Clipboard.
    virtual bool canInsertFromMimeData(const QMimeData *source, MimeInsertionSource origin) const;
    // Borrowed MIME valid only during this synchronous call. Snapshot needed data
    // before modal UI. Clipboard commits its captured target's draft first; Drop
    // does not commit an unrelated draft. Hosts own acquisition and node updates.
    // The default stores the fully encoded dropped file URL as hyperLink.
    virtual void insertFromMimeData(const QString &nodeId, const QMimeData *source,
                                    MimeInsertionSource origin);
    // Synchronous context-menu hooks, called after any inline draft is committed.
    // nodeId is the selected node when the menu opens. Defaults do nothing;
    // subclasses own URL/image editing and any resulting document changes.
    virtual void onAddUrl(const QString &nodeId);
    virtual void onAddImage(const QString &nodeId);
private:
    class Private;
    std::unique_ptr<Private> d;
};
}
#endif
