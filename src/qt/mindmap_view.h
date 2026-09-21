#ifndef M3_QT_MINDMAP_VIEW_H
#define M3_QT_MINDMAP_VIEW_H
#include "presentation.h"
#include <QGraphicsView>
#include <QHash>
#include <QKeySequence>
#include <QPointer>

class QGraphicsTextItem;
class QPlainTextEdit;

namespace m3::qt {
class MindMapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MindMapView(QWidget *parent = nullptr);
    ~MindMapView() override;
    void beginTopicEdit(const QString &id, const QList<QKeySequence> &acceptShortcuts);
    void finishTopicEdit(bool commit, bool restoreFocus = false);
    void prepare(NodePresentation &node) const;
    void install(Presentation presentation, bool fit);
    void showError(const QString &message);
    void setSelection(const QString &node, const QString &link);
    void ensureNodeVisible(const QString &id);
    void centerNode(const QString &id);
    void fitContents();
    void zoom(qreal factor);
    void resetZoom();
signals:
    void topicEditRequested(const QString &id, const QString &topic);
    void topicEditingChanged(bool editing);
    void nodePicked(const QString &id);
    void nodeLinkActivated(const QString &nodeId, const QString &url);
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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    Qt::MouseButton panning = Qt::NoButton;
    bool pendingFit = false;
    QPointF panPosition;
    QHash<QString, QString> nodeParents;
    QString pressedLinkNodeId, pressedLinkUrl;
    QPoint linkPressPosition;
    QString draggedNodeId, dropTargetId;
    QPoint dragPressPosition;
    bool nodeDragging = false;
    QPointer<QPlainTextEdit> topicEditor;
    QPointer<QGraphicsTextItem> topicLabel;
    QString editedId, originalTopic;
    Qt::FocusPolicy viewFocusPolicy = Qt::NoFocus, viewportFocusPolicy = Qt::NoFocus;
    void updateTopicEditorGeometry();
    QGraphicsItem *targetAt(const QPoint &position) const;
    void updatePanCursor(const QPoint &position);
    QString dropTargetAt(const QPoint &position) const;
    void setDropTarget(const QString &id);
    void clearNodeLinkPress();
    bool handleNodeLinkEvent(QEvent *event);
    void clearNodeDrag();
    bool handleNodeDragEvent(QEvent *event);
    bool pick(QMouseEvent *event, bool activate);
    void preserveCenter(const QPointF &center);
};
}
#endif
