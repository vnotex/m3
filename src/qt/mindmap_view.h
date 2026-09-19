#ifndef M3_QT_MINDMAP_VIEW_H
#define M3_QT_MINDMAP_VIEW_H
#include "presentation.h"
#include <QGraphicsView>

namespace m3::qt {
class MindMapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MindMapView(QWidget *parent = nullptr);
    void prepare(NodePresentation &node) const;
    void install(Presentation presentation, bool fit);
    void showError(const QString &message);
    void setSelection(const QString &node, const QString &link);
    void fitContents();
    void zoom(qreal factor);
    void resetZoom();
signals:
    void nodePicked(const QString &id);
    void linkPicked(const QString &id);
    void emptyPicked();
    void editRequested();
    void expansionRequested(const QString &id, bool expanded);
    void appearanceChanged();
protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    bool panning = false, pendingFit = false;
    QPoint panPosition;
    void pick(QMouseEvent *event, bool activate);
    void preserveCenter(const QPointF &center);
};
}
#endif
