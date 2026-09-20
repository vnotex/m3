#include "mindmap_view.h"
#include <QAction>
#include <QApplication>
#include <QFocusEvent>
#include <QGraphicsItem>
#include <QKeyEvent>
#include <QMenu>
#include <QPlainTextEdit>
#include <QShortcut>
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
    QGraphicsTextItem *label;
    NodeItem(NodePresentation node, const QPalette &palette)
        : id(node.id), expanded(node.expanded), hasChildren(node.hasChildren),
          rect(QPointF(), node.rectangle.size()), colors(palette) {
        setPos(node.rectangle.topLeft());
        setZValue(2);
        setFlag(ItemIsSelectable);
        label = new QGraphicsTextItem(this);
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
            const QPointF center = affordance().center();
            p->setPen(QPen(colors.color(QPalette::ButtonText), 1));
            p->setBrush(colors.color(QPalette::Base));
            p->drawEllipse(center, 6, 6);
            p->drawLine(center - QPointF(3, 0), center + QPointF(3, 0));
            if (!expanded) p->drawLine(center - QPointF(0, 3), center + QPointF(0, 3));
        }
    }
};
QRectF placeLabel(const QRectF &initial, const std::vector<QRectF> &occupied) {
    auto collision = [&](const QRectF &candidate) {
        return std::find_if(occupied.begin(), occupied.end(), [&](const QRectF &obstacle) {
            return candidate.adjusted(-4, -4, 4, 4).intersects(obstacle);
        });
    };
    if (collision(initial) == occupied.end()) return initial;
    QRectF best;
    qreal bestDistance = std::numeric_limits<qreal>::infinity();
    // Try each cardinal direction. Movement is monotonic, so each encountered
    // obstacle is passed permanently; choose the closest unobstructed position.
    for (int direction = 0; direction < 4; ++direction) {
        QRectF candidate = initial;
        for (auto obstacle = collision(candidate); obstacle != occupied.end(); obstacle = collision(candidate)) {
            // Clear the boundary despite rounding through moveRight/Bottom and adjusted().
            const qreal gap = 4 + 8 * std::numeric_limits<qreal>::epsilon() *
                std::max({qreal(1), std::abs(obstacle->left()), std::abs(obstacle->right()),
                          std::abs(obstacle->top()), std::abs(obstacle->bottom()), candidate.width(), candidate.height()});
            switch (direction) {
            case 0: candidate.moveBottom(obstacle->top() - gap); break;
            case 1: candidate.moveTop(obstacle->bottom() + gap); break;
            case 2: candidate.moveRight(obstacle->left() - gap); break;
            case 3: candidate.moveLeft(obstacle->right() + gap); break;
            }
        }
        const QPointF displacement = candidate.topLeft() - initial.topLeft();
        const qreal distance = QPointF::dotProduct(displacement, displacement);
        if (distance < bestDistance) { bestDistance = distance; best = candidate; }
    }
    return best;
}
class LinkItem final : public QGraphicsItem {
public:
    QString id;
    QPainterPath curve, hit, leader;
    QPolygonF arrow;
    QPalette colors;
    LinkItem(const LinkPresentation &link, QPainterPath path, const QFont &font, const QPalette &palette,
             std::vector<QRectF> &occupied)
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
            const QPointF anchor = curve.pointAtPercent(0.5);
            const QRectF preferred(anchor - QPointF(size.width() / 2, size.height()), size);
            const QRectF placed = placeLabel(preferred, occupied);
            label->setPos(placed.topLeft());
            occupied.push_back(placed);
            if (placed != preferred) {
                // Retain the route's midpoint anchor without putting text under
                // nodes or other labels. The leader shares the link's hit area.
                leader.moveTo(anchor);
                leader.lineTo(QPointF(std::clamp(anchor.x(), placed.left(), placed.right()),
                                      std::clamp(anchor.y(), placed.top(), placed.bottom())));
                hit = hit.united(stroker.createStroke(leader));
            }
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
        if (!leader.isEmpty()) {
            p->setPen(QPen(color, 1, Qt::DotLine));
            p->drawPath(leader);
        }
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
MindMapView::~MindMapView() {
    qApp->removeEventFilter(this);
    if (topicEditor) {
        disconnect(topicEditor, nullptr, this, nullptr);
        for (auto *shortcut : topicEditor->findChildren<QShortcut *>())
            disconnect(shortcut, nullptr, this, nullptr);
    }
    topicEditor.clear();
    topicLabel.clear();
}
void MindMapView::beginTopicEdit(const QString &id, const QList<QKeySequence> &acceptShortcuts) {
    if (id.isEmpty()) return;
    if (topicEditor && editedId == id) { topicEditor->setFocus(); return; }
    auto findNode = [&]() -> NodeItem * {
        for (auto *item : scene()->items())
            if (auto *node = dynamic_cast<NodeItem *>(item); node && node->id == id) return node;
        return nullptr;
    };
    if (!findNode()) return;
    finishTopicEdit(true);
    // Committing can synchronously replace every scene item.
    auto *node = findNode();
    if (!node) return;
    ensureVisible(node);
    editedId = id;
    topicLabel = node->label;
    originalTopic = topicLabel->toPlainText();
    auto *input = new QPlainTextEdit(viewport());
    topicEditor = input;
    input->setObjectName(QStringLiteral("topicEditor"));
    input->setAccessibleName(tr("Topic"));
    input->setFont(topicLabel->font());
    input->setPalette(palette());
    input->setPlainText(originalTopic);
    input->setTabChangesFocus(false);
    input->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    input->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(input, &QWidget::customContextMenuRequested, this, [input](const QPoint &point) {
        // The standard menu must belong to the input for outside-event routing.
        QPointer<QMenu> menu = input->createStandardContextMenu();
        menu->setParent(input, menu->windowFlags());
        menu->exec(input->viewport()->mapToGlobal(point));
        if (menu) menu->deleteLater();
    });
    topicLabel->hide();
    updateTopicEditorGeometry();
    viewFocusPolicy = focusPolicy();
    viewportFocusPolicy = viewport()->focusPolicy();
    setFocusPolicy(Qt::NoFocus);
    viewport()->setFocusPolicy(Qt::NoFocus);
    auto *accept = new QShortcut(input);
    accept->setKeys(acceptShortcuts);
    accept->setContext(Qt::WidgetWithChildrenShortcut);
    connect(accept, &QShortcut::activated, this, [this] { finishTopicEdit(true, true); });
    input->show();
    input->setFocus(Qt::OtherFocusReason);
    input->selectAll();
    emit topicEditingChanged(true);
    qApp->installEventFilter(this);
}
void MindMapView::finishTopicEdit(bool commit, bool restoreFocus) {
    if (!topicEditor) return;
    auto *input = topicEditor.data();
    const QString id = editedId, original = originalTopic, draft = input->toPlainText();
    topicEditor.clear();
    editedId.clear();
    originalTopic.clear();
    qApp->removeEventFilter(this);
    disconnect(input, nullptr, this, nullptr);
    for (auto *shortcut : input->findChildren<QShortcut *>()) {
        shortcut->setEnabled(false);
        disconnect(shortcut, nullptr, this, nullptr);
    }
    setFocusPolicy(viewFocusPolicy);
    viewport()->setFocusPolicy(viewportFocusPolicy);
    if (topicLabel) topicLabel->show();
    topicLabel.clear();
    input->hide();
    input->deleteLater();
    emit topicEditingChanged(false);
    if (commit && draft != original) emit topicEditRequested(id, draft);
    if (commit) ensureNodeVisible(id);
    if (restoreFocus) setFocus(Qt::OtherFocusReason);
}
void MindMapView::updateTopicEditorGeometry() {
    if (!topicEditor || !topicLabel) return;
    const QRect label = mapFromScene(topicLabel->sceneBoundingRect()).boundingRect();
    const int margin = topicEditor->frameWidth() + int(std::ceil(topicEditor->document()->documentMargin()));
    const int line = topicEditor->fontMetrics().lineSpacing();
    const int width = std::min(viewport()->width(), std::max(120, label.width() + 2 * margin));
    const int height = std::min(viewport()->height(), std::clamp(label.height() + 2 * margin,
                                                               3 * line + 2 * margin, 8 * line + 2 * margin));
    const int x = std::clamp(label.left() - margin, 0, viewport()->width() - width);
    const int y = std::clamp(label.top() - margin, 0, viewport()->height() - height);
    topicEditor->setGeometry(x, y, width, height);
}
bool MindMapView::eventFilter(QObject *watched, QEvent *event) {
    if (!topicEditor) return false;
    bool owned = false, inWindow = false;
    for (auto *object = watched; object; object = object->parent()) {
        if (object == topicEditor) { owned = true; break; }
        if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->window() == window())
            inWindow = true;
    }
    if (owned) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape) { key->accept(); return true; }
            const auto *accept = topicEditor->findChild<QShortcut *>();
            auto modifiers = key->modifiers();
            modifiers.setFlag(Qt::KeypadModifier, false);
            const QKeyCombination nonKeypad(modifiers, Qt::Key(key->key()));
            for (const auto &sequence : accept->keys()) {
                // Qt also matches keypad events against bindings without KeypadModifier.
                if (!sequence.isEmpty() && (sequence[0] == key->keyCombination() || sequence[0] == nonKeypad)) {
                    // Leave sequence recognition to Qt, not QPlainTextEdit.
                    key->ignore();
                    return true;
                }
            }
        } else if (event->type() == QEvent::KeyPress && watched == topicEditor &&
                   static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            finishTopicEdit(false, true);
            return true;
        } else if (event->type() == QEvent::FocusOut && watched == topicEditor) {
            if (static_cast<QFocusEvent *>(event)->reason() == Qt::PopupFocusReason) {
                auto *popup = QApplication::activePopupWidget();
                // Native IME popups need not have a QWidget; text menus are input-owned.
                if (!popup) return false;
                for (QObject *owner = popup; owner; owner = owner->parent())
                    if (owner == topicEditor) return false;
            }
            finishTopicEdit(true);
            return false;
        }
        return false;
    }
    if (!inWindow && event->type() == QEvent::Shortcut) {
        // QAction shortcuts belong to associated widgets, even without a QObject parent.
        if (auto *action = qobject_cast<QAction *>(watched))
            for (auto *associated : action->associatedObjects())
                for (auto *object = associated; object && !inWindow; object = object->parent())
                    if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->window() == window())
                        inWindow = true;
    }
    if (!inWindow) return false;
    if (((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) &&
         watched != viewport()) || event->type() == QEvent::Shortcut ||
        (event->type() == QEvent::Close && watched == window())) {
        finishTopicEdit(true);
        // The receiver's original action must still execute after the rename.
        return false;
    }
    return false;
}
void MindMapView::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    updateTopicEditorGeometry();
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
    std::vector<QRectF> occupied;
    occupied.reserve(presentation.nodes.size() + presentation.links.size());
    for (const auto &node : presentation.nodes) {
        rectangles.insert(node.id, node.rectangle);
        occupied.push_back(node.rectangle.adjusted(-2, -2, 2, 2));
    }
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
            replacement->addItem(new LinkItem(link, std::move(path), font(), palette(), occupied));
        }
    }
    QGraphicsTextItem *replacementLabel = nullptr;
    for (auto &node : presentation.nodes) {
        auto *item = new NodeItem(std::move(node), palette());
        replacement->addItem(item);
        if (topicEditor && item->id == editedId) replacementLabel = item->label;
    }
    replacement->setSceneRect(replacement->itemsBoundingRect().adjusted(-32, -32, 32, 32));
    const QPointF center = mapToScene(viewport()->rect().center());
    if (topicEditor) {
        if (fit || !replacementLabel || replacementLabel->toPlainText() != originalTopic) {
            finishTopicEdit(false);
        } else {
            topicLabel.clear();
            topicLabel = replacementLabel;
            topicLabel->hide();
            topicEditor->setFont(topicLabel->font());
            topicEditor->setPalette(palette());
        }
    }
    auto *old = scene();
    replacement->setParent(this);
    setScene(replacement.release());
    delete old;
    preserveCenter(center);
    pendingFit = pendingFit || fit;
    if (pendingFit && isVisible()) fitContents();
    updateTopicEditorGeometry();
}
void MindMapView::showError(const QString &message) {
    finishTopicEdit(false);
    auto *replacement = new QGraphicsScene(this);
    auto *text = replacement->addText(tr("Drawing unavailable\n%1").arg(message), font());
    text->setDefaultTextColor(palette().color(QPalette::Text));
    auto *old = scene();
    setScene(replacement);
    delete old;
    fitContents();
}
void MindMapView::setSelection(const QString &node, const QString &link) {
    if (topicEditor && (node != editedId || !link.isEmpty())) finishTopicEdit(false);
    for (auto *item : scene()->items()) {
        if (auto *n = dynamic_cast<NodeItem *>(item)) n->setSelected(n->id == node);
        else if (auto *l = dynamic_cast<LinkItem *>(item)) l->setSelected(l->id == link);
    }
}
void MindMapView::ensureNodeVisible(const QString &id) {
    if (id.isEmpty() || !isVisible()) return;
    for (auto *item : scene()->items()) {
        if (auto *node = dynamic_cast<NodeItem *>(item); node && node->id == id) {
            ensureVisible(node, 0, 0);
            return;
        }
    }
}
void MindMapView::fitContents() {
    if (!isVisible() || viewport()->width() <= 0 || viewport()->height() <= 0) { pendingFit = true; return; }
    pendingFit = false;
    setSceneRect(scene()->sceneRect());
    fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
    updateTopicEditorGeometry();
}
void MindMapView::zoom(qreal factor) {
    pendingFit = false;
    const QPointF center = mapToScene(viewport()->rect().center());
    const qreal current = transform().m11();
    const qreal target = std::clamp(current * factor, qreal(0.1), qreal(4));
    scale(target / current, target / current);
    preserveCenter(center);
    updateTopicEditorGeometry();
}
void MindMapView::resetZoom() {
    pendingFit = false;
    const QPointF center = mapToScene(viewport()->rect().center());
    resetTransform(); preserveCenter(center);
    updateTopicEditorGeometry();
}
void MindMapView::pick(QMouseEvent *event, bool activate) {
    enum class Target { Empty, Node, Link, Expansion };
    Target target = Target::Empty;
    QString id;
    bool expanded = false;
    for (auto *item = itemAt(event->position().toPoint()); item; item = item->parentItem()) {
        if (auto *node = dynamic_cast<NodeItem *>(item)) {
            id = node->id;
            expanded = !node->expanded;
            const bool toggle = node->hasChildren && node->affordance().contains(node->mapFromScene(mapToScene(event->position().toPoint())));
            target = toggle && !activate ? Target::Expansion : Target::Node;
            break;
        }
        if (auto *link = dynamic_cast<LinkItem *>(item)) {
            id = link->id;
            target = Target::Link;
            break;
        }
    }
    // No scene pointers or old-coordinate hit tests survive the synchronous rename.
    finishTopicEdit(true);
    setFocus(Qt::MouseFocusReason);
    switch (target) {
    case Target::Node: emit nodePicked(id); if (activate) emit editRequested(); break;
    case Target::Link: emit linkPicked(id); if (activate) emit editRequested(); break;
    case Target::Expansion: emit expansionRequested(id, expanded); break;
    case Target::Empty: emit emptyPicked(); break;
    }
}
void MindMapView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        panning = true; panPosition = event->position().toPoint(); setCursor(Qt::ClosedHandCursor); event->accept();
    } else if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton) {
        pick(event, false); event->accept();
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
        updateTopicEditorGeometry();
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
    updateTopicEditorGeometry();
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
