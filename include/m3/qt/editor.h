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
// replace it atomically, select the new root, and fit once; failed imports retain
// the document and selection. Import accepts native JSON, optional-field nested
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
// Right-clicking a group member retains the group and offers only Delete.
// Multiple nodes support only Delete: no properties, editing or mutation gestures.
// Viewport/help and selection controls remain available. Delete removes selected
// subtrees once, including overlaps, in one document change and confirmation.
// A selection containing the root cannot be deleted. After group deletion, select
// the visible parent of the first selected subtree not covered by another selection.
// Selection, layout direction, zoom/pan and fit never change persisted JSON.
// Shift+vertical wheel scrolls horizontally; with normal left-to-right controls,
// down/negative deltas move the viewport right and up/positive deltas move it left.
// Ctrl+wheel (including Ctrl+Shift) zooms at the pointer. Unmodified and native
// horizontal wheel behavior is unchanged.
// Drag empty space with the left mouse button to pan; the middle button pans anywhere.
// Empty space shows an open hand cursor, closing during a pan. Empty clicks clear selection.
// Node context menus separate creation from editing/reordering and omit viewport actions.
// With no selection the menu contains only Focus Main Node, Fit, Zoom In, Zoom Out and 100%.
// For a single node, Collapse/Expand, Move and Add Link are command/shortcut actions.
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
// Presenting expanded node properties (including for a different selected node)
// also scrolls the node clear of the panel, preserving zoom and keyboard focus.
// If it cannot fit, scrolling exposes the largest portion in a clear viewport region.
// An already-presented panel does not constrain subsequent pan or property edits.
// Startup fit and explicit fit/centering commands retain their camera placement.
// Expanding a node centers it at the current zoom; large branches may still
// extend beyond the viewport.
// Topics/labels are plain Unicode text. Embedded NULs are rejected.
// A single selected node has a floating properties card; other selections hide it.
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
// Dropping one local file onto a canvas node sets or replaces its URL through
// resolveDroppedFileUrl, without changing selection.
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
// Widget policy, copied at construction; no Qt-specific configuration enters the core.
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
// link topics inline. Link topics are literal text, without node hashtag parsing.
// These letters remain ordinary input while typing, including in the inline draft.
// P toggles the selected node's properties card, keeping focus on the canvas.
// With canvas focus, ? opens shortcut help for any selection. Escape or an outside
// click dismisses it; Escape returns focus to the canvas without clearing selection.
// acceptTopic applies only during
// inline editing, while map shortcuts are suspended. UI node creation inserts a
// blank node and starts inline editing. UI link creation (arrow drag or Add Link
// dialog) selects the new visible link and starts inline editing; the dialog also
// defaults to undirected. Escape cancels only the draft, keeping the created node
// or link; clicking outside saves. Programmatic addNode/addLink do not start editing.
// Example: config.shortcuts.addChild = {QKeySequence(QStringLiteral("Ctrl+J"))};
//          MindMapEditor editor(config);
struct EditorConfig {
    struct Shortcuts {
        QList<QKeySequence> undo{QKeySequence(QKeySequence::Undo)};
        QList<QKeySequence> redo{QKeySequence(QKeySequence::Redo)};
        QList<QKeySequence> addChild{QKeySequence(Qt::Key_Tab), QKeySequence(Qt::Key_Insert)};
        // The root has no sibling: Enter/Shift+Enter append a child there.
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
        QList<QKeySequence> addLink{QKeySequence(Qt::CTRL | Qt::Key_L)};
        // Logical tree navigation, independent of layout direction. A collapsed
        // node has no selectable child; Space expands it before navigating.
        QList<QKeySequence> selectParent{QKeySequence(Qt::Key_Left)};
        QList<QKeySequence> selectChild{QKeySequence(Qt::Key_Right)};
        QList<QKeySequence> previousSibling{QKeySequence(Qt::Key_Up)};
        QList<QKeySequence> nextSibling{QKeySequence(Qt::Key_Down)};
        // Focus main node: select the root, center it, and focus the canvas.
        QList<QKeySequence> selectRoot{QKeySequence(Qt::Key_Home)};
        QList<QKeySequence> clearSelection{QKeySequence(Qt::Key_Escape)};
        QList<QKeySequence> zoomIn{QKeySequence(QKeySequence::ZoomIn), QKeySequence(Qt::CTRL | Qt::Key_Equal)};
        QList<QKeySequence> zoomOut{QKeySequence(QKeySequence::ZoomOut)};
        QList<QKeySequence> resetZoom{QKeySequence(Qt::CTRL | Qt::Key_0)};
        QList<QKeySequence> fit;
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
    // UI policy only: the removeNode() API never prompts.
    bool confirmSubtreeDeletion = true;
    // Filesystem directory for relative image/link references; empty captures cwd.
    // Relative bases become absolute at construction, without canonicalizing or
    // requiring existence. Mutable through setResourceBasePath; never persisted.
    QString resourceBasePath;
    // False passes image request and activation URLs unchanged to the host,
    // including relative/protected resource identifiers. No library I/O occurs.
    bool resolveRelativeUrls = true;
    // New direct children of the root (UI or addNode) get a random palette color.
    // Prefer colors unused by current main branches, comparing their effective
    // QColor values (local override or inherited root color); nested overrides
    // do not reserve colors. Once all 23 are used, allow any palette color.
    // Enabled by default. Creation only: loading/moving nodes never recolors them.
    // The setting is copied, not persisted; each assigned branchColor is persisted.
    bool autoRandomBranchColor = true;
};
class M3_QT_API MindMapEditor : public QWidget {
    Q_OBJECT
public:
    // Outline shows depth-first indented rows with square tree connectors.
    enum class LayoutDirection { Balanced, Right, Left, Outline };
    Q_ENUM(LayoutDirection)
    explicit MindMapEditor(QWidget *parent = nullptr);
    explicit MindMapEditor(const EditorConfig &config, QWidget *parent = nullptr);
    ~MindMapEditor() override;
    // Borrowed command action by stable, case-sensitive name; null for empty/missing.
    // The editor owns lifetime, shortcuts and enablement. Hosts may present actions
    // in toolbars/menus, but must not delete, reparent or override those policies.
    // Names: undo, redo, addChild, addSibling, addSiblingBefore, editSelection,
    // deleteSelection, toggleExpanded, moveNode, moveUp, moveDown, addLink, editLink,
    // zoomIn, zoomOut, resetZoom, fit, selectRoot, selectParent, selectChild,
    // previousSibling, nextSibling, copy, clearSelection, toggleBold, toggleItalic,
    // resetStyle, textColor, fillColor, editTags, editIcons, editNote, toggleProperties,
    // editTopic, showHelp. Triggering retains the same checked inline-commit behavior.
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
    // Collapse the properties card and focus the canvas, including read-only or no selection.
    // Idempotent; keeps document, selection and zoom. The user may expand it again.
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
    bool newDocument(const QString &topic = QStringLiteral("Central topic"));
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
    // Selection, copying, navigation and camera controls remain available.
    // Host-controlled newDocument/loadJson are permitted in read-only mode.
    void setReadOnly(bool readOnly);
    bool isReadOnly() const;
    bool canUndo() const;
    bool canRedo() const;
    // Accept a changed inline draft first, then traverse one document command.
    // A rejected draft/restoration leaves the cursor unchanged. Read-only or no
    // available command returns false. Text-input shortcuts keep local history.
    // Selection is restored; camera/resources are not history. Save retains it;
    // successful newDocument/loadJson starts a fresh baseline.
    bool undo();
    bool redo();
    QByteArray toJson() const;
    // Copied Markdown text of the committed model, empty on error. No selection,
    // layout, or document changes; an active inline draft remains uncommitted.
    QString toMarkdown() const;
    // Copied standalone HTML: top-left switch between the current-layout visible
    // map (shown initially) and full articles. Committed data only; no editor state
    // changes. Empty on error; the snapshot survives edits and widget destruction.
    QString toHtml() const;
    QString lastError() const;
    QString addNode(const QString &parentId, const QString &topic, int index = -1);
    bool renameNode(const QString &id, const QString &topic);
    // Copied complete committed node record, empty on error. IDs are immutable.
    QByteArray nodeJson(const QString &id) const;
    // Validated native node patch. A style object merges members, null members
    // remove keys; unrelated/opaque style data survives. Unchanged patches emit
    // no documentChanged. Read-only or invalid patches return false.
    bool updateNode(const QString &id, const QByteArray &patch);
    bool removeNode(const QString &id);
    bool moveNode(const QString &id, const QString &parentId, int index = -1);
    bool setExpanded(const QString &id, bool expanded);
    QString addLink(const QString &sourceId, const QString &targetId, bool directed, const QString &topic = {});
    bool updateLink(const QString &id, const QString &sourceId, const QString &targetId, bool directed, const QString &topic);
    bool removeLink(const QString &id);
    // Complete node preorder, including collapsed descendants; root level is 1.
    // Empty output reports an error for a live document.
    QVector<OutlineEntry> outline() const;
    // Select and scroll at the current zoom, temporarily revealing collapsed
    // ancestors without changing persisted expansion, document history or JSON.
    // Explicit expansion commands consume that node's temporary override, even
    // for persisted no-ops. Edits retain existing IDs; New/Open clears overrides.
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
    // Clear Find state without changing selection, camera or temporary reveals.
    void clearFind();
    bool selectNode(const QString &id);
    bool selectLink(const QString &id);
    void clearSelection();
    // The singular getter is empty for zero or multiple selected nodes.
    QString selectedNodeId() const;
    // Full node selection in click order; empty when a link is selected.
    QStringList selectedNodeIds() const;
    QString selectedLinkId() const;
    // Selection in the focused owned text input, otherwise selected node topics
    // in selection order joined by LF, or the selected link topic.
    QString selectedText() const;
    bool setLayoutDirection(LayoutDirection direction);
    LayoutDirection layoutDirection() const;
    void fitToContents();
    // View-only controls; retain document, selection and inline draft.
    // Current canvas scale; fit may produce values outside the step-zoom range.
    qreal zoomFactor() const;
    // True after initial/explicit fit, including while fitting awaits a visible viewport.
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
    // View-only scale or Fit/manual-mode changes, including initial/deferred fit.
    // Mode transitions emit even at the same scale; pan never emits.
    void zoomChanged(qreal factor, bool fit);
    // Committed semantic changes only, never draft typing or camera/selection.
    void documentChanged();
    // View-only: emitted after a successful actual layout change, including API calls.
    void layoutDirectionChanged(m3::qt::MindMapEditor::LayoutDirection direction);
    // Availability includes read-only policy; emitted only on boolean transitions.
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    // Only transitions between a draft differing from its original and no
    // changed draft. Cancel/accept emits false; it does not imply a saved map.
    void pendingEditChanged(bool pending);
    // Fires for every membership change. For multiple nodes both arguments are
    // empty; query selectedNodeIds() to distinguish a group from no selection.
    void selectionChanged(const QString &nodeId, const QString &linkId);
    void nodeLinkActivated(const QString &nodeId, const QString &url);
    void imageRequested(const QString &url, quint64 requestId);
    void errorOccurred(const QString &message);
protected:
    // Called once per eligible canvas drop with the decoded absolute local path,
    // never on hover, URL-field edits, loading, or activation. The default returns
    // a fully encoded file URL. Subclasses may use QDir::relativeFilePath with
    // their own base directory. The result is stored unchanged as hyperLink:
    // no trimming, re-encoding, or scheme/existence validation. Empty skips the
    // mutation, preserving any existing URL. This is a synchronous conversion
    // hook, not an asynchronous operation or document-mutation callback.
    virtual QString resolveDroppedFileUrl(const QString &filePath) const;
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
