#ifndef M3_QT_EDITOR_H
#define M3_QT_EDITOR_H
#include <QByteArray>
#include <QString>
#include <QKeySequence>
#include <QList>
#include <QWidget>
#include <memory>

#ifdef M3_QT_BUILD_DLL
#define M3_QT_API Q_DECL_EXPORT
#else
#define M3_QT_API Q_DECL_IMPORT
#endif

// Qt C++ ABI: hosts supply QApplication on the GUI thread and a matching Qt kit.
// Enable M3_BUILD_QT, then find_package(m3 COMPONENTS qt_editor) and link m3::qt_editor.
// Component-free/core-only consumers remain independent of Qt. M3_BUILD_QT_DEMO
// additionally builds the file-handling example; this widget owns no file policy.
// The widget owns its model; snapshots and commands never expose a core handle.
// New editors contain the selected root "root" / "Central topic". New/load
// replace it atomically, select the new root, and fit once; failed imports retain
// the document and selection. Import accepts native JSON and Mind Elixir; export
// is a copied native JSON snapshot, including opaque metadata, or empty on error.
// Mutations return false/empty on semantic failure. After a successful mutation,
// a drawing failure is reported without claiming rollback: the error scene is
// retried on the next refresh. documentChanged fires once per semantic success;
// unchanged node-property edits do not emit it.
// IDs are case-sensitive. Index -1 appends; move indexes apply after removal.
// Only visible nodes/links can be selected, exclusively; hidden data is preserved.
// Selection, layout direction, zoom/pan and fit never change persisted JSON.
// Drag empty space with the left mouse button to pan; the middle button pans anywhere.
// Empty space shows an open hand cursor, closing during a pan. Empty clicks clear selection.
// Selecting a node or accepting an inline topic scrolls its full bounds into view
// without changing zoom; oversized nodes can only be partially shown by scrolling.
// Expanding a node centers it at the current zoom; large branches may still
// extend beyond the viewport.
// Topics/labels are plain Unicode text. Embedded NULs are rejected.
// A selected node has a floating properties card; links/empty selection hide it.
// Appearance, tags, icons, URL and note edits persist immediately without changing
// selection or zoom. Tags/icons accept comma-separated entries. Icons display as
// literal Unicode text above the topic, wrapping within the node; names are not
// mapped to an icon library. The Icons field opens a searchable, categorized
// Unicode 15.1 emoji picker on focus; a choice replaces the current entry or a
// selection within it, never adjacent entries. Ctrl+H/J/K/L moves the picker
// selection left/down/up/right without editing the input text.
// The automatic navigation tooltip is shown once per QApplication lifetime.
// Direct typing still saves immediately; glyph availability depends on host fonts.
// Empty icons reserve no space. Nonempty URLs
// show an indicator beside the topic. Activating it emits nodeLinkActivated with
// the node ID and unchanged URL; the host decides whether and how to open it.
// Collapsing the card leaves only its top-right toggle, without changing selection.
// The expanded/collapsed preference is local to this editor and survives selection
// and document changes; links/empty selection hide either form without resetting it.
// The renderer recognizes style.color/background, fontSize (1-256 pixels, numeric
// or "Npx"), fontWeight (normal/bold or CSS weight 100-900), and fontStyle
// (normal/italic, inheriting the editor font when unset). Other style data stays
// opaque and is preserved when a property is edited or appearance is reset.
namespace m3::qt {
// Widget policy, copied at construction; no Qt-specific configuration enters the core.
// Replace a shortcut list to rebind it, or clear it to disable its keyboard binding
// without removing the toolbar/menu command. Avoid assigning the same sequence to
// multiple map commands. Bindings are local to this widget; link/move dialogs and
// the layout picker and properties inputs keep their normal input keys.
// acceptTopic applies only during
// inline editing, while map shortcuts are suspended. UI creation inserts a blank
// node and starts inline editing. Escape cancels the draft (keeping a newly created
// blank node); clicking outside saves. Programmatic addNode does not start editing.
// Example: config.shortcuts.addChild = {QKeySequence(QStringLiteral("Ctrl+J"))};
//          MindMapEditor editor(config);
struct EditorConfig {
    struct Shortcuts {
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
    } shortcuts;
    // UI policy only: the removeNode() API never prompts.
    bool confirmSubtreeDeletion = true;
};
class M3_QT_API MindMapEditor : public QWidget {
    Q_OBJECT
public:
    enum class LayoutDirection { Balanced, Right, Left };
    Q_ENUM(LayoutDirection)
    explicit MindMapEditor(QWidget *parent = nullptr);
    explicit MindMapEditor(const EditorConfig &config, QWidget *parent = nullptr);
    ~MindMapEditor() override;
    bool newDocument(const QString &topic = QStringLiteral("Central topic"));
    bool loadJson(const QByteArray &json);
    QByteArray toJson() const;
    QString lastError() const;
    QString addNode(const QString &parentId, const QString &topic, int index = -1);
    bool renameNode(const QString &id, const QString &topic);
    bool removeNode(const QString &id);
    bool moveNode(const QString &id, const QString &parentId, int index = -1);
    bool setExpanded(const QString &id, bool expanded);
    QString addLink(const QString &sourceId, const QString &targetId, bool directed, const QString &topic = {});
    bool updateLink(const QString &id, const QString &sourceId, const QString &targetId, bool directed, const QString &topic);
    bool removeLink(const QString &id);
    bool selectNode(const QString &id);
    bool selectLink(const QString &id);
    void clearSelection();
    QString selectedNodeId() const;
    QString selectedLinkId() const;
    bool setLayoutDirection(LayoutDirection direction);
    LayoutDirection layoutDirection() const;
    void fitToContents();
    // Commit any inline draft and select the document root. For a shown editor,
    // also center it and focus the canvas without changing zoom. Navigation alone
    // does not change the document. Returns false if the root cannot be selected.
    bool focusRoot();
signals:
    void documentChanged();
    void selectionChanged(const QString &nodeId, const QString &linkId);
    void nodeLinkActivated(const QString &nodeId, const QString &url);
    void errorOccurred(const QString &message);
private:
    class Private;
    std::unique_ptr<Private> d;
};
}
#endif
