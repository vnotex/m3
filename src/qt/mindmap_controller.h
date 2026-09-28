#ifndef M3_QT_MINDMAP_CONTROLLER_H
#define M3_QT_MINDMAP_CONTROLLER_H
#include "m3/qt/editor.h"
#include "m3/m3.h"
#include "presentation.h"
#include <QSet>
#include <QHash>
#include <QStringList>
#include <memory>
#include <optional>
#include <vector>

namespace m3::qt {
class MindMapView;
class MindMapController : public QObject {
    Q_OBJECT
public:
    explicit MindMapController(MindMapView &view, const EditorConfig &config, QObject *parent);
    QString resourceBasePath() const { return resourceBase; }
    void setResourceBasePath(const QString &path);
    void setReadOnly(bool value);
    bool isReadOnly() const { return readOnly; }
    QString resolveResourceUrl(const QString &value) const;
    void provideImage(const QString &url, quint64 requestId, const QImage &image);
    void reloadImages();
    bool newDocument(const QString &topic);
    bool loadJson(const QByteArray &json);
    bool canUndo() const { return !readOnly && historyCursor > 0; }
    bool canRedo() const { return !readOnly && historyCursor + 1 < history.size(); }
    bool undo();
    bool redo();
    bool isRestoringHistory() const { return restoringHistory; }
    QByteArray toJson();
    QByteArray nodeJson(const QString &id);
    QString selectedText();
    QString toMarkdown();
    QString toHtml();
    QString lastError() const { return error; }
    QString addNode(const QString &parent, const QString &topic, int index);
    bool renameNode(const QString &id, const QString &topic);
    bool commitTopicEdit(const QString &id, const QString &draft);
    bool updateNodeProperties(const QString &id, const QByteArray &patch);
    bool removeNode(const QString &id);
    bool removeSelectedNodes();
    bool moveNode(const QString &id, const QString &parent, int index);
    bool setExpanded(const QString &id, bool expanded);
    QString addLink(const QString &source, const QString &target, bool directed, const QString &topic);
    bool updateLink(const QString &id, const QString &source, const QString &target, bool directed, const QString &topic);
    bool setLinkDirection(const QString &id, LinkDirection direction);
    bool reconnectLink(const QString &id, bool source, const QString &original, const QString &node);
    bool commitLinkTopicEdit(const QString &id, const QString &topic);
    bool removeLink(const QString &id);
    QVector<OutlineEntry> outline();
    bool revealNode(const QString &id);
    FindResult findText(const QString &text, Qt::CaseSensitivity sensitivity, bool backward, bool incremental);
    void clearFind();
    bool selectNode(const QString &id);
    bool toggleNodeSelection(const QString &id);
    bool selectLink(const QString &id);
    void clearSelection();
    QString selectedNodeId() const { return selectedNodes.size() == 1 ? selectedNodes.front() : QString(); }
    QStringList selectedNodeIds() const { return selectedNodes; }
    QString selectedLinkId() const { return selectedLink; }
    bool setLayoutDirection(MindMapEditor::LayoutDirection direction);
    MindMapEditor::LayoutDirection layoutDirection() const { return direction; }
    std::vector<NodeChoice> choices();
    NodeProperties nodeProperties(const QString &id);
    LinkPresentation linkChoice(const QString &id);
    void refreshAppearance();
signals:
    void documentChanged();
    void layoutDirectionChanged(m3::qt::MindMapEditor::LayoutDirection direction);
    void undoAvailable(bool available);
    void redoAvailable(bool available);
    void selectionChanged(const QString &nodeId, const QString &linkId);
    void errorOccurred(const QString &message);
    void commandSucceeded();
    void imageRequested(const QString &url, quint64 requestId);
private:
    using Map = std::unique_ptr<M3Mindmap, decltype(&m3_mindmap_destroy)>;
    Map model{nullptr, m3_mindmap_destroy};
    struct HistoryState {
        QByteArray json;
        QStringList nodes;
        QString link;
    };
    QByteArray currentSnapshot;
    std::vector<HistoryState> history;
    size_t historyCursor = 0;
    const size_t undoLimit;
    quint64 documentRevision = 0;
    bool undoWasAvailable = false, redoWasAvailable = false, restoringHistory = false;
    void notifyHistoryAvailability();
    bool restoreHistory(size_t cursor);
    MindMapView &view;
    QString error, selectedLink;
    QStringList selectedNodes;
    QString resourceBase;
    const bool resolveRelativeUrls, autoRandomBranchColor;
    bool readOnly = false;
    struct ImageResource {
        quint64 requestId = 0;
        QImage pixels;
        bool completed = false;
    };
    QHash<QString, ImageResource> imageResources;
    quint64 nextImageRequestId = 1, imageGeneration = 0;
    bool imageRequestsScheduled = false, imageRefreshScheduled = false;
    void scheduleImageRequests();
    void scheduleImageRefresh();
    QSet<QString> visibleNodes, visibleLinks;
    QSet<QString> temporaryExpanded;
    struct FindTarget { QString id; bool link = false; };
    QString findQuery;
    Qt::CaseSensitivity findSensitivity = Qt::CaseSensitive;
    QVector<FindTarget> findMatches;
    int currentFindMatch = -1;
    bool findCacheValid = false;
    bool revealTarget(FindTarget target);
    MindMapEditor::LayoutDirection direction = MindMapEditor::LayoutDirection::Balanced;
    bool fail(const QString &message);
    bool writable();
    bool status(M3Status result);
    bool strings(std::initializer_list<QString> values);
    void success();
    QByteArray snapshot(const M3Mindmap *map);
    Presentation prepare(const M3Mindmap *map, const QByteArray &json,
                         MindMapEditor::LayoutDirection requested, QSet<QString> &expansions,
                         bool useImageCache = true);
    void install(Presentation presentation, bool fit);
    bool replace(Map candidate);
    template<typename Operation>
    bool transact(Operation &&operation,
                  const QString &preferredNode = {}, const QString &preferredLink = {},
                  const QString &clearedExpansion = {});
    bool finishChange(std::optional<Presentation> prepared = {}, bool fit = false, bool restoring = false,
                      const QString &preferredNode = {}, const QString &preferredLink = {});
    void selection(QStringList nodes, QString link, bool ensureVisible = true);
    void restoreSelection();
};
}
#endif
