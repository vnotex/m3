#ifndef M3_QT_EDITOR_H
#define M3_QT_EDITOR_H
#include <QByteArray>
#include <QString>
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
// retried on the next refresh. documentChanged fires once per semantic success.
// IDs are case-sensitive. Index -1 appends; move indexes apply after removal.
// Only visible nodes/links can be selected, exclusively; hidden data is preserved.
// Selection, layout direction, zoom/pan and fit never change persisted JSON.
// Topics/labels are plain Unicode text. Embedded NULs are rejected.
namespace m3::qt {
class M3_QT_API MindMapEditor : public QWidget {
    Q_OBJECT
public:
    enum class LayoutDirection { Balanced, Right, Left };
    Q_ENUM(LayoutDirection)
    explicit MindMapEditor(QWidget *parent = nullptr);
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
signals:
    void documentChanged();
    void selectionChanged(const QString &nodeId, const QString &linkId);
    void errorOccurred(const QString &message);
private:
    class Private;
    std::unique_ptr<Private> d;
};
}
#endif
