#ifndef M3_QT_MINDMAP_CONTROLLER_H
#define M3_QT_MINDMAP_CONTROLLER_H
#include "m3/qt/editor.h"
#include "m3/m3.h"
#include "presentation.h"
#include <QSet>
#include <memory>

namespace m3::qt {
class MindMapView;
class MindMapController : public QObject {
    Q_OBJECT
public:
    explicit MindMapController(MindMapView &view, QObject *parent);
    bool newDocument(const QString &topic);
    bool loadJson(const QByteArray &json);
    QByteArray toJson();
    QString toMarkdown();
    QString toHtml();
    QString lastError() const { return error; }
    QString addNode(const QString &parent, const QString &topic, int index);
    bool renameNode(const QString &id, const QString &topic);
    bool commitTopicEdit(const QString &id, const QString &draft);
    bool updateNodeProperties(const QString &id, const QByteArray &patch);
    bool removeNode(const QString &id);
    bool moveNode(const QString &id, const QString &parent, int index);
    bool setExpanded(const QString &id, bool expanded);
    QString addLink(const QString &source, const QString &target, bool directed, const QString &topic);
    bool updateLink(const QString &id, const QString &source, const QString &target, bool directed, const QString &topic);
    bool removeLink(const QString &id);
    bool selectNode(const QString &id);
    bool selectLink(const QString &id);
    void clearSelection();
    QString selectedNodeId() const { return selectedNode; }
    QString selectedLinkId() const { return selectedLink; }
    bool setLayoutDirection(MindMapEditor::LayoutDirection direction);
    MindMapEditor::LayoutDirection layoutDirection() const { return direction; }
    std::vector<NodeChoice> choices();
    NodeProperties nodeProperties(const QString &id);
    LinkPresentation linkChoice(const QString &id);
    void refreshAppearance();
signals:
    void documentChanged();
    void selectionChanged(const QString &nodeId, const QString &linkId);
    void errorOccurred(const QString &message);
    void commandSucceeded();
private:
    using Map = std::unique_ptr<M3Mindmap, decltype(&m3_mindmap_destroy)>;
    Map model{nullptr, m3_mindmap_destroy};
    MindMapView &view;
    QString error, selectedNode, selectedLink;
    QSet<QString> visibleNodes, visibleLinks;
    MindMapEditor::LayoutDirection direction = MindMapEditor::LayoutDirection::Balanced;
    bool fail(const QString &message);
    bool status(M3Status result);
    bool strings(std::initializer_list<QString> values);
    void success();
    QByteArray snapshot(const M3Mindmap *map);
    Presentation prepare(const M3Mindmap *map, MindMapEditor::LayoutDirection requested);
    void install(Presentation presentation, bool fit);
    bool replace(Map candidate);
    bool changed(M3Status result, const QString &preferredNode = {}, const QString &preferredLink = {});
    void selection(const QString &node, const QString &link);
};
}
#endif
