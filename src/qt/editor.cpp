#include "m3/qt/editor.h"
#include "mindmap_controller.h"
#include "mindmap_view.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <functional>
#include <QToolBar>
#include <QLabel>
#include <QVBoxLayout>
namespace m3::qt {
class MindMapEditor::Private {
public:
    MindMapEditor *host;
    MindMapView *view;
    MindMapController *controller;
    QLabel *error;
    QToolBar *toolbar;
    QComboBox *direction;
    QAction *addChild, *editSelection, *deleteSelection, *toggleExpanded, *move, *up, *down, *addLink;
    QList<QAction *> menuActions;
    QAction *action(const char *name, const QString &text, QKeySequence shortcut, std::function<void()> command) {
        auto *result = new QAction(text, host);
        result->setObjectName(QString::fromLatin1(name));
        result->setShortcut(shortcut);
        result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        host->addAction(result);
        toolbar->addAction(result);
        menuActions.append(result);
        QObject::connect(result, &QAction::triggered, host, std::move(command));
        return result;
    }
    static const NodeChoice *choice(const std::vector<NodeChoice> &nodes, const QString &id) {
        for (const auto &node : nodes) if (node.id == id) return &node;
        return nullptr;
    }
    static void populate(QComboBox *picker, const std::vector<NodeChoice> &nodes, const QSet<QString> &excluded = {}) {
        for (const auto &node : nodes)
            if (!excluded.contains(node.id)) picker->addItem(node.topic + QStringLiteral(" [") + node.id + QLatin1Char(']'), node.id);
    }
    void updateActions() {
        const auto nodes = controller->choices();
        const auto *node = choice(nodes, controller->selectedNodeId());
        const bool hasLink = controller->selectedLinkId().isEmpty() == false;
        const bool movable = node && !node->parent.isEmpty();
        addChild->setEnabled(node); addLink->setEnabled(node);
        editSelection->setEnabled(node || hasLink);
        deleteSelection->setEnabled(movable || hasLink);
        move->setEnabled(movable);
        toggleExpanded->setEnabled(node && !node->children.isEmpty());
        toggleExpanded->setText(node && !node->expanded ? tr("Expand") : tr("Collapse"));
        const auto *parent = node ? choice(nodes, node->parent) : nullptr;
        const auto index = parent ? parent->children.indexOf(node->id) : -1;
        up->setEnabled(parent && index > 0);
        down->setEnabled(parent && index >= 0 && index + 1 < parent->children.size());
        QSignalBlocker blocker(direction);
        direction->setCurrentIndex(direction->findData(int(controller->layoutDirection())));
    }
    void topicDialog(bool insert) {
        const QString id = controller->selectedNodeId();
        const auto nodes = controller->choices();
        const auto *node = choice(nodes, id);
        if (!node) return;
        bool accepted = false;
        const auto topic = QInputDialog::getMultiLineText(host, insert ? tr("Add child") : tr("Rename node"),
            tr("Topic"), insert ? QString() : node->topic, &accepted);
        if (accepted) {
            if (insert) controller->addNode(id, topic, -1);
            else controller->renameNode(id, topic);
        }
    }
    void linkDialog(bool insert) {
        const auto nodes = controller->choices();
        if (nodes.empty()) return;
        const QString id = controller->selectedLinkId();
        const auto link = insert ? LinkPresentation{} : controller->linkChoice(id);
        if (!insert && link.id.isEmpty()) return;
        QDialog dialog(host);
        dialog.setWindowTitle(insert ? tr("Add link") : tr("Edit link"));
        auto *form = new QFormLayout(&dialog);
        auto *source = new QComboBox(&dialog), *target = new QComboBox(&dialog);
        source->setObjectName(QStringLiteral("sourceNode")); target->setObjectName(QStringLiteral("targetNode"));
        populate(source, nodes); populate(target, nodes);
        source->setCurrentIndex(source->findData(insert ? controller->selectedNodeId() : link.source));
        target->setCurrentIndex(target->findData(insert ? controller->selectedNodeId() : link.target));
        auto *topic = new QLineEdit(link.topic, &dialog);
        topic->setObjectName(QStringLiteral("linkTopic"));
        auto *directed = new QCheckBox(tr("Directed"), &dialog);
        directed->setObjectName(QStringLiteral("directed")); directed->setChecked(insert || link.directed);
        form->addRow(tr("Source"), source); form->addRow(tr("Target"), target);
        form->addRow(tr("Topic"), topic); form->addRow(directed);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            if (insert) controller->addLink(source->currentData().toString(), target->currentData().toString(), directed->isChecked(), topic->text());
            else controller->updateLink(id, source->currentData().toString(), target->currentData().toString(), directed->isChecked(), topic->text());
        }
    }
    void moveDialog() {
        const auto nodes = controller->choices();
        const QString id = controller->selectedNodeId();
        const auto *node = choice(nodes, id);
        if (!node || node->parent.isEmpty()) return;
        QSet<QString> excluded{id};
        // Choices arrive in child preorder: each excluded parent precedes its descendants.
        for (const auto &entry : nodes) if (excluded.contains(entry.parent)) excluded.insert(entry.id);
        QDialog dialog(host);
        dialog.setWindowTitle(tr("Move node"));
        auto *form = new QFormLayout(&dialog);
        auto *parent = new QComboBox(&dialog);
        parent->setObjectName(QStringLiteral("newParent"));
        populate(parent, nodes, excluded);
        auto *index = new QSpinBox(&dialog);
        index->setObjectName(QStringLiteral("insertIndex"));
        auto range = [&] {
            const auto *destination = choice(nodes, parent->currentData().toString());
            const int maximum = destination ? int(destination->children.size()) - (destination->id == node->parent ? 1 : 0) : 0;
            index->setRange(0, maximum);
            index->setValue(maximum);
        };
        QObject::connect(parent, &QComboBox::currentIndexChanged, &dialog, range);
        parent->setCurrentIndex(parent->findData(node->parent));
        range();
        const auto *oldParent = choice(nodes, node->parent);
        if (oldParent) index->setValue(int(oldParent->children.indexOf(id)));
        form->addRow(tr("New parent"), parent); form->addRow(tr("Insertion index (zero-based)"), index);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) controller->moveNode(id, parent->currentData().toString(), index->value());
    }
    void reorder(int delta) {
        const auto nodes = controller->choices();
        const auto *node = choice(nodes, controller->selectedNodeId());
        const auto *parent = node ? choice(nodes, node->parent) : nullptr;
        if (parent) controller->moveNode(node->id, parent->id, int(parent->children.indexOf(node->id)) + delta);
    }
    explicit Private(MindMapEditor *editor) : host(editor) {
        auto *layout = new QVBoxLayout(editor);
        view = new MindMapView(editor);
        error = new QLabel(editor);
        error->setTextFormat(Qt::PlainText); error->setWordWrap(true); error->hide();
        controller = new MindMapController(*view, editor);
        toolbar = new QToolBar(editor);
        layout->addWidget(toolbar);
        addChild = action("addChild", tr("Add child"), QKeySequence(Qt::Key_Insert), [this] { topicDialog(true); });
        editSelection = action("editSelection", tr("Rename/Edit"), QKeySequence(Qt::Key_F2), [this] {
            if (controller->selectedLinkId().isEmpty()) topicDialog(false); else linkDialog(false);
        });
        deleteSelection = action("deleteSelection", tr("Delete"), QKeySequence(Qt::Key_Delete), [this] {
            const auto link = controller->selectedLinkId(), node = controller->selectedNodeId();
            if (!link.isEmpty()) controller->removeLink(link);
            else if (!node.isEmpty() && QMessageBox::question(host, tr("Delete subtree"),
                tr("Delete this node and all its descendants?"), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes)
                controller->removeNode(node);
        });
        toggleExpanded = action("toggleExpanded", tr("Collapse"), QKeySequence(Qt::Key_Space), [this] {
            const auto nodes = controller->choices();
            const auto *node = choice(nodes, controller->selectedNodeId());
            if (node) controller->setExpanded(node->id, !node->expanded);
        });
        move = action("moveNode", tr("Move..."), QKeySequence(Qt::CTRL | Qt::Key_M), [this] { moveDialog(); });
        up = action("moveUp", tr("Move up"), QKeySequence(Qt::CTRL | Qt::Key_Up), [this] { reorder(-1); });
        down = action("moveDown", tr("Move down"), QKeySequence(Qt::CTRL | Qt::Key_Down), [this] { reorder(1); });
        addLink = action("addLink", tr("Add link"), QKeySequence(Qt::CTRL | Qt::Key_L), [this] { linkDialog(true); });
        toolbar = new QToolBar(editor);
        layout->addWidget(toolbar);
        action("zoomIn", tr("Zoom +"), {}, [this] { view->zoom(1.2); });
        action("zoomOut", tr("Zoom -"), {}, [this] { view->zoom(1 / 1.2); });
        action("resetZoom", tr("100%"), {}, [this] { view->resetZoom(); });
        action("fit", tr("Fit"), {}, [this] { view->fitContents(); });
        direction = new QComboBox(toolbar);
        direction->addItem(tr("Balanced"), int(LayoutDirection::Balanced));
        direction->addItem(tr("Right"), int(LayoutDirection::Right));
        direction->addItem(tr("Left"), int(LayoutDirection::Left));
        direction->setAccessibleName(tr("Layout direction"));
        toolbar->addWidget(direction);
        QObject::connect(direction, &QComboBox::currentIndexChanged, editor, [this] {
            controller->setLayoutDirection(static_cast<LayoutDirection>(direction->currentData().toInt()));
            updateActions();
        });
        layout->addWidget(view, 1); layout->addWidget(error);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(view, &QWidget::customContextMenuRequested, editor, [this](const QPoint &point) {
            QMenu menu(host); menu.addActions(menuActions); menu.exec(view->mapToGlobal(point));
        });
        QObject::connect(controller, &MindMapController::documentChanged, editor, [this, editor] { updateActions(); emit editor->documentChanged(); });
        QObject::connect(controller, &MindMapController::selectionChanged, editor, [this, editor](const QString &node, const QString &link) {
            updateActions(); emit editor->selectionChanged(node, link);
        });
        QObject::connect(controller, &MindMapController::errorOccurred, editor, [this, editor](const QString &message) {
            error->setText(message); error->show(); emit editor->errorOccurred(message);
        });
        QObject::connect(controller, &MindMapController::commandSucceeded, editor, [this] { error->clear(); error->hide(); updateActions(); });
        QObject::connect(view, &MindMapView::nodePicked, controller, &MindMapController::selectNode);
        QObject::connect(view, &MindMapView::linkPicked, controller, &MindMapController::selectLink);
        QObject::connect(view, &MindMapView::emptyPicked, controller, &MindMapController::clearSelection);
        QObject::connect(view, &MindMapView::expansionRequested, controller, &MindMapController::setExpanded);
        QObject::connect(view, &MindMapView::appearanceChanged, controller, &MindMapController::refreshAppearance);
        QObject::connect(view, &MindMapView::editRequested, editSelection, &QAction::trigger);
        controller->newDocument(QStringLiteral("Central topic"));
        updateActions();
    }
};
MindMapEditor::MindMapEditor(QWidget *parent) : QWidget(parent), d(std::make_unique<Private>(this)) {}
MindMapEditor::~MindMapEditor() = default;
bool MindMapEditor::newDocument(const QString &topic) { return d->controller->newDocument(topic); }
bool MindMapEditor::loadJson(const QByteArray &json) { return d->controller->loadJson(json); }
QByteArray MindMapEditor::toJson() const { return d->controller->toJson(); }
QString MindMapEditor::lastError() const { return d->controller->lastError(); }
QString MindMapEditor::addNode(const QString &parent, const QString &topic, int index) { return d->controller->addNode(parent, topic, index); }
bool MindMapEditor::renameNode(const QString &id, const QString &topic) { return d->controller->renameNode(id, topic); }
bool MindMapEditor::removeNode(const QString &id) { return d->controller->removeNode(id); }
bool MindMapEditor::moveNode(const QString &id, const QString &parent, int index) { return d->controller->moveNode(id, parent, index); }
bool MindMapEditor::setExpanded(const QString &id, bool expanded) { return d->controller->setExpanded(id, expanded); }
QString MindMapEditor::addLink(const QString &source, const QString &target, bool directed, const QString &topic) { return d->controller->addLink(source, target, directed, topic); }
bool MindMapEditor::updateLink(const QString &id, const QString &source, const QString &target, bool directed, const QString &topic) { return d->controller->updateLink(id, source, target, directed, topic); }
bool MindMapEditor::removeLink(const QString &id) { return d->controller->removeLink(id); }
bool MindMapEditor::selectNode(const QString &id) { return d->controller->selectNode(id); }
bool MindMapEditor::selectLink(const QString &id) { return d->controller->selectLink(id); }
void MindMapEditor::clearSelection() { d->controller->clearSelection(); }
QString MindMapEditor::selectedNodeId() const { return d->controller->selectedNodeId(); }
QString MindMapEditor::selectedLinkId() const { return d->controller->selectedLinkId(); }
bool MindMapEditor::setLayoutDirection(LayoutDirection direction) { return d->controller->setLayoutDirection(direction); }
MindMapEditor::LayoutDirection MindMapEditor::layoutDirection() const { return d->controller->layoutDirection(); }
void MindMapEditor::fitToContents() { d->view->fitContents(); }
}
