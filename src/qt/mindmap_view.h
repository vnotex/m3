#ifndef M3_QT_MINDMAP_VIEW_H
#define M3_QT_MINDMAP_VIEW_H
#include "presentation.h"
#include <QGraphicsView>
#include <QHash>
#include <QImage>
#include <QKeySequence>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <functional>

class QGraphicsTextItem;
class QPlainTextEdit;

namespace m3::qt {
class MindMapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MindMapView(QWidget *parent = nullptr);
    ~MindMapView() override;
    void beginTopicEdit(const QString &id, const QList<QKeySequence> &acceptShortcuts);
    void beginLinkTopicEdit(const QString &id, const QList<QKeySequence> &acceptShortcuts);
    using TopicCommitHandler = std::function<bool(const QString &, const QString &, const QString &, bool)>;
    void setTopicCommitHandler(TopicCommitHandler handler);
    bool finishTopicEdit(bool commit, bool restoreFocus = false, bool keepEditing = false);
    bool hasPendingEdit() const { return pendingEdit; }
    void setReadOnly(bool value);
    void prepare(NodePresentation &node) const;
    void install(Presentation presentation, bool fit);
    QImage renderImage(Presentation presentation) const;
    void showError(const QString &message);
    void setSelection(const QStringList &nodes, const QString &link);
    void ensureNodeVisible(const QString &id, const QRect &occlusion = {});
    void ensureLinkVisible(const QString &id);
    void centerNode(const QString &id);
    void fitContents();
    void zoom(qreal factor);
    void resetZoom();
    void scrollSteps(int horizontal, int vertical);
signals:
    void pendingEditChanged(bool pending);
    void linkCreationRequested(const QString &source, const QString &target);
    void linkEndpointChangeRequested(const QString &id, bool source, const QString &original, const QString &node);
    void topicEditingChanged(bool editing);
    void nodePicked(const QString &id);
    void nodeSelectionToggled(const QString &id);
    void nodeLinkActivated(const QString &nodeId, const QString &url);
    void imageResizeRequested(const QString &nodeId, const QString &url, const QSizeF &originalSize, const QSizeF &size);
    void fileDropped(const QString &nodeId, const QString &filePath);
    void nodeMoveRequested(const QString &id, const QString &parent, int index);
    void linkPicked(const QString &id);
    void emptyPicked();
    void editRequested();
    void expansionRequested(const QString &id, bool expanded);
    void appearanceChanged();
protected:
    bool event(QEvent *event) override;
    bool viewportEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    bool readOnly = false;
    Qt::MouseButton panning = Qt::NoButton;
    QSet<QString> selectedNodes;
    bool suppressNodeDoubleClick = false;
    bool pendingFit = false;
    QPointF panPosition;
    QHash<QString, QString> nodeParents;
    QGraphicsItem *linkCreationHandle = nullptr;
    QGraphicsItem *linkEndpointHandles[2] = {};
    QString reconnectedLinkId, originalEndpointId;
    QPoint reconnectPressPosition;
    QPointF reconnectOriginalPosition, reconnectFixedPosition;
    bool reconnectSource = false, reconnectDragging = false, suppressReconnectMenu = false;
    QString linkSourceId;
    QPoint linkCreationPressPosition;
    QPointF linkPreviewStart, linkPreviewEnd;
    bool linkCreating = false, suppressLinkContextMenu = false;
    QString pressedLinkNodeId, pressedLinkUrl;
    QPoint linkPressPosition;
    QString draggedNodeId, dropTargetId;
    QPoint dragPressPosition;
    bool nodeDragging = false;
    QString resizedNodeId, resizedImageUrl;
    QSizeF originalImageSize, resizedImageSize;
    QRectF initialImageRectangle;
    QPoint imagePressPosition;
    QPointF imagePressScenePosition;
    QGraphicsItem *resizedImageItem = nullptr;
    bool imageResizeDragging = false;
    enum class TopicKind { Node, Link };
    TopicKind editedKind = TopicKind::Node;
    bool topicLabelVisible = false;
    bool pendingEdit = false, finishingTopicEdit = false;
    TopicCommitHandler topicCommitHandler;
    void updatePendingEdit();
    QPointer<QPlainTextEdit> topicEditor;
    QList<QKeySequence> topicAcceptShortcuts;
    QPointer<QGraphicsTextItem> topicLabel;
    QString editedId, originalTopic, originalTopicDraft;
    Qt::FocusPolicy viewFocusPolicy = Qt::NoFocus, viewportFocusPolicy = Qt::NoFocus;
    static QGraphicsTextItem *findTopicLabel(QGraphicsScene *scene, const QString &id, TopicKind kind);
    void beginTopicEdit(TopicKind kind, const QString &id, const QList<QKeySequence> &acceptShortcuts);
    void updateTopicEditorGeometry();
    void clearLinkCreation();
    bool handleLinkCreationEvent(QEvent *event);
    void updateLinkCreation(const QPoint &position);
    QString linkTargetAt(const QPoint &position, const QString &excluded) const;
    void clearLinkReconnect();
    void clearLinkEndpointHandles();
    bool handleLinkReconnectEvent(QEvent *event);
    void updateLinkReconnect(const QPoint &position);
    QGraphicsItem *targetAt(const QPoint &position) const;
    void updatePanCursor(const QPoint &position);
    QString dropTargetAt(const QPoint &position) const;
    void setDropTarget(const QString &id);
    void clearImageResize();
    bool handleImageResizeEvent(QEvent *event);
    bool updateImageResize(const QPoint &position);
    void clearNodeLinkPress();
    bool handleNodeLinkEvent(QEvent *event);
    void clearNodeDrag();
    bool handleNodeDragEvent(QEvent *event);
    bool pick(QMouseEvent *event, bool activate);
    void preserveCenter(const QPointF &center);
};
}
#endif
