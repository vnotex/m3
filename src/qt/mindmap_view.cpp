#include "mindmap_view.h"
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QScrollBar>
#include <QResizeEvent>
#include <limits>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <map>

namespace m3::qt {
namespace {
class NodeItem final : public QGraphicsItem {
public:
    QString id;
    bool expanded, hasChildren;
    QRectF rect;
    QPalette colors;
    NodeItem(NodePresentation node, const QPalette &palette)
        : id(node.id), expanded(node.expanded), hasChildren(node.hasChildren),
          rect(QPointF(), node.rectangle.size()), colors(palette) {
        setPos(node.rectangle.topLeft());
        setZValue(2);
        setFlag(ItemIsSelectable);
        auto *label = new QGraphicsTextItem(this);
        // setDocument borrows; QObject parenting makes the text item sole owner.
        node.text->setParent(label);
        label->setDocument(node.text.release());
        label->setFont(label->document()->defaultFont());
        label->setDefaultTextColor(colors.color(QPalette::Text));
        label->setTextInteractionFlags(Qt::NoTextInteraction);
        label->setPos(12, 8);
    }
    QRectF affordance() const { return QRectF(rect.right() - 22, rect.center().y() - 8, 18, 16); }
    QRectF boundingRect() const override { return rect.adjusted(-2, -2, 2, 2); }
    QPainterPath shape() const override { QPainterPath path; path.addRoundedRect(rect, 8, 8); return path; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        p->setPen(QPen(colors.color(isSelected() ? QPalette::Highlight : QPalette::Mid), isSelected() ? 3 : 1));
        p->setBrush(colors.color(QPalette::Button));
        p->drawRoundedRect(rect, 8, 8);
        if (hasChildren) {
            const QRectF box = affordance();
            p->setPen(QPen(colors.color(QPalette::ButtonText), 1));
            p->setBrush(colors.color(QPalette::Base));
            p->drawRoundedRect(box, 3, 3);
            p->drawLine(box.center() - QPointF(4, 0), box.center() + QPointF(4, 0));
            if (!expanded) p->drawLine(box.center() - QPointF(0, 4), box.center() + QPointF(0, 4));
        }
    }
};
class LinkItem final : public QGraphicsItem {
public:
    QString id;
    QPainterPath curve, hit;
    QPolygonF arrow;
    QPalette colors;
    LinkItem(const LinkPresentation &link, QPainterPath path, const QFont &font, const QPalette &palette)
        : id(link.id), curve(std::move(path)), colors(palette) {
        setZValue(1);
        setFlag(ItemIsSelectable);
        if (link.directed) {
            const QPointF end = curve.pointAtPercent(1);
            QPointF tangent = end - curve.pointAtPercent(0.999);
            const qreal length = std::hypot(tangent.x(), tangent.y());
            if (length > 0) {
                tangent /= length;
                const QPointF normal(-tangent.y(), tangent.x());
                arrow << end << end - tangent * 9 + normal * 3 << end - tangent * 9 - normal * 3;
            }
        }
        QPainterPathStroker stroker;
        stroker.setWidth(8);
        hit = stroker.createStroke(curve);
        if (!arrow.isEmpty()) { QPainterPath outline; outline.addPolygon(arrow); hit = hit.united(outline); }
        if (!link.topic.isEmpty()) {
            auto *label = new QGraphicsTextItem(this);
            label->setFont(font);
            label->document()->setDocumentMargin(0);
            label->setPlainText(link.topic);
            label->setDefaultTextColor(colors.color(QPalette::Text));
            label->setTextInteractionFlags(Qt::NoTextInteraction);
            const QSizeF size = label->boundingRect().size();
            label->setPos(curve.pointAtPercent(0.5) - QPointF(size.width() / 2, size.height()));
            QPainterPath textShape;
            textShape.addRect(label->mapRectToParent(label->boundingRect()));
            hit = hit.united(textShape);
        }
    }
    QRectF boundingRect() const override { return hit.boundingRect().adjusted(-2, -2, 2, 2); }
    QPainterPath shape() const override { return hit; }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override {
        const QColor color = colors.color(isSelected() ? QPalette::Highlight : QPalette::Dark);
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(color, isSelected() ? 2 : 1, Qt::DashLine));
        p->drawPath(curve);
        if (!arrow.isEmpty()) {
            p->setPen(Qt::NoPen); p->setBrush(color); p->drawPolygon(arrow);
        }
    }
};
QPointF boundary(const QRectF &rect, const QPointF &towards) {
    const QPointF delta = towards - rect.center();
    const qreal x = delta.x() == 0 ? std::numeric_limits<qreal>::infinity() : rect.width() / (2 * std::abs(delta.x()));
    const qreal y = delta.y() == 0 ? std::numeric_limits<qreal>::infinity() : rect.height() / (2 * std::abs(delta.y()));
    return rect.center() + delta * std::min(x, y);
}
}
MindMapView::MindMapView(QWidget *parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    // Stable viewport dimensions prevent fit/scrollbar resize feedback.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setResizeAnchor(NoAnchor);
    setTransformationAnchor(NoAnchor);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundBrush(palette().brush(QPalette::Base));
}
void MindMapView::prepare(NodePresentation &node) const {
    node.text = std::make_unique<QTextDocument>();
    node.text->setDocumentMargin(0);
    QFont textFont = font();
    textFont.setBold(node.root);
    node.text->setDefaultFont(textFont);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    node.text->setDefaultTextOption(option);
    node.text->setPlainText(node.topic);
    node.text->setTextWidth(-1);
    node.text->setTextWidth(qMin(qreal(240), qMax(qreal(1), node.text->idealWidth())));
    const QSizeF textSize = node.text->size();
    node.rectangle = QRectF(0, 0, qMax(qreal(72), textSize.width() + 24 + (node.hasChildren ? 18 : 0)),
                            qMax(qreal(36), textSize.height() + 16));
}
void MindMapView::install(Presentation presentation, bool fit) {
    auto replacement = std::make_unique<QGraphicsScene>();
    QHash<QString, QRectF> rectangles;
    for (const auto &node : presentation.nodes) rectangles.insert(node.id, node.rectangle);
    for (const auto &edge : presentation.treeEdges) {
        const QRectF parent = rectangles.value(edge.source), child = rectangles.value(edge.target);
        const bool right = child.center().x() > parent.center().x();
        const QPointF from(right ? parent.right() : parent.left(), parent.center().y());
        const QPointF to(right ? child.left() : child.right(), child.center().y());
        const qreal middle = (from.x() + to.x()) / 2;
        QPainterPath path(from);
        path.cubicTo(QPointF(middle, from.y()), QPointF(middle, to.y()), to);
        auto *item = replacement->addPath(path, QPen(palette().color(QPalette::Mid), 1));
        item->setAcceptedMouseButtons(Qt::NoButton);
    }
    using Pair = std::pair<QString, QString>;
    std::map<Pair, std::vector<const LinkPresentation *>> groups;
    for (const auto &link : presentation.links)
        groups[{std::min(link.source, link.target), std::max(link.source, link.target)}].push_back(&link);
    for (const auto &group : groups) {
        const auto &links = group.second;
        const QRectF canonicalFrom = rectangles.value(group.first.first), canonicalTo = rectangles.value(group.first.second);
        QPointF normal = canonicalTo.center() - canonicalFrom.center();
        const qreal length = std::hypot(normal.x(), normal.y());
        if (length > 0) normal = QPointF(-normal.y(), normal.x()) / length;
        for (size_t i = 0; i < links.size(); ++i) {
            const auto &link = *links[i];
            const QRectF source = rectangles.value(link.source), target = rectangles.value(link.target);
            QPainterPath path;
            if (link.source == link.target) {
                const qreal radius = 36 + 18 * i;
                const QPointF from(source.right(), source.center().y()), to(source.center().x(), source.top());
                path.moveTo(from);
                const QPointF corner(source.right() + radius, source.top() - radius);
                path.cubicTo(corner, corner, to);
            } else {
                const QPointF from = boundary(source, target.center()), to = boundary(target, source.center());
                const qreal offset = 24 * (qreal(i) - (qreal(links.size()) - 1) / 2 + 1);
                path.moveTo(from);
                path.quadTo((from + to) / 2 + normal * offset, to);
            }
            replacement->addItem(new LinkItem(link, std::move(path), font(), palette()));
        }
    }
    for (auto &node : presentation.nodes) replacement->addItem(new NodeItem(std::move(node), palette()));
    replacement->setSceneRect(replacement->itemsBoundingRect().adjusted(-32, -32, 32, 32));
    const QPointF center = mapToScene(viewport()->rect().center());
    auto *old = scene();
    replacement->setParent(this);
    setScene(replacement.release());
    delete old;
    preserveCenter(center);
    pendingFit = pendingFit || fit;
    if (pendingFit && isVisible()) fitContents();
}
void MindMapView::showError(const QString &message) {
    auto *replacement = new QGraphicsScene(this);
    auto *text = replacement->addText(tr("Drawing unavailable\n%1").arg(message), font());
    text->setDefaultTextColor(palette().color(QPalette::Text));
    auto *old = scene();
    setScene(replacement);
    delete old;
    fitContents();
}
void MindMapView::setSelection(const QString &node, const QString &link) {
    for (auto *item : scene()->items()) {
        if (auto *n = dynamic_cast<NodeItem *>(item)) n->setSelected(n->id == node);
        else if (auto *l = dynamic_cast<LinkItem *>(item)) l->setSelected(l->id == link);
    }
}
void MindMapView::fitContents() {
    if (!isVisible() || viewport()->width() <= 0 || viewport()->height() <= 0) { pendingFit = true; return; }
    pendingFit = false;
    setSceneRect(scene()->sceneRect());
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
}
void MindMapView::zoom(qreal factor) {
    pendingFit = false;
    const QPointF center = mapToScene(viewport()->rect().center());
    const qreal current = transform().m11();
    const qreal target = std::clamp(current * factor, qreal(0.1), qreal(4));
    scale(target / current, target / current);
    preserveCenter(center);
}
void MindMapView::resetZoom() {
    pendingFit = false;
    const QPointF center = mapToScene(viewport()->rect().center());
    resetTransform(); preserveCenter(center);
}
void MindMapView::pick(QMouseEvent *event, bool activate) {
    for (auto *item = itemAt(event->position().toPoint()); item; item = item->parentItem()) {
        if (auto *node = dynamic_cast<NodeItem *>(item)) {
            const QString id = node->id;
            const bool expanded = node->expanded;
            const bool toggle = node->hasChildren && node->affordance().contains(node->mapFromScene(mapToScene(event->position().toPoint())));
            if (toggle && !activate) emit expansionRequested(id, !expanded);
            else { emit nodePicked(id); if (activate) emit editRequested(); }
            return;
        }
        if (auto *link = dynamic_cast<LinkItem *>(item)) {
            const QString id = link->id;
            emit linkPicked(id);
            if (activate) emit editRequested();
            return;
        }
    }
    emit emptyPicked();
}
void MindMapView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        panning = true; panPosition = event->position().toPoint(); setCursor(Qt::ClosedHandCursor); event->accept();
    } else if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        setFocus(); pick(event, false); event->accept();
    } else QGraphicsView::mousePressEvent(event);
}
void MindMapView::mouseMoveEvent(QMouseEvent *event) {
    if (panning) {
        const QPoint current = event->position().toPoint(), delta = current - panPosition;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        panPosition = current; event->accept();
    } else QGraphicsView::mouseMoveEvent(event);
}
void MindMapView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) { panning = false; unsetCursor(); event->accept(); }
    else QGraphicsView::mouseReleaseEvent(event);
}
void MindMapView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) { pick(event, true); event->accept(); }
    else QGraphicsView::mouseDoubleClickEvent(event);
}
void MindMapView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        const QPoint position = event->position().toPoint();
        const QPointF before = mapToScene(position);
        zoom(std::pow(1.2, event->angleDelta().y() / 120.0));
        const QPointF after = mapToScene(position);
        preserveCenter(mapToScene(viewport()->rect().center()) + before - after);
        event->accept();
    } else QGraphicsView::wheelEvent(event);
}
void MindMapView::preserveCenter(const QPointF &center) {
    // QGraphicsView otherwise recenters a small scene and clamps near its edges.
    const QSizeF visible = mapToScene(viewport()->rect()).boundingRect().size();
    const QRectF frame(center - QPointF(visible.width() / 2, visible.height() / 2), visible);
    setSceneRect(scene()->sceneRect().united(frame.adjusted(-4, -4, 4, 4)));
    centerOn(center);
}
void MindMapView::resizeEvent(QResizeEvent *event) {
    const QPointF center = mapToScene(QRect(QPoint(), event->oldSize()).center());
    QGraphicsView::resizeEvent(event);
    if (pendingFit) fitContents();
    else if (event->oldSize().isValid()) preserveCenter(center);
}
void MindMapView::showEvent(QShowEvent *event) {
    QGraphicsView::showEvent(event);
    // Context-bound callback cannot outlive the view. Layout has settled by then.
    if (pendingFit) QTimer::singleShot(0, this, [this] { if (pendingFit) fitContents(); });
}
void MindMapView::changeEvent(QEvent *event) {
    QGraphicsView::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange) {
        setBackgroundBrush(palette().brush(QPalette::Base));
        emit appearanceChanged();
    }
}
}
