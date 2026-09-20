#include "m3/qt/editor.h"
#include "mindmap_controller.h"
#include "mindmap_view.h"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <functional>
#include <QToolBar>
#include <QLabel>
#include <QVBoxLayout>
namespace m3::qt {
class MindMapEditor::Private {
public:
    MindMapEditor *host;
    const EditorConfig config;
    MindMapView *view;
    MindMapController *controller;
    QLabel *error;
    QToolBar *toolbar;
    QComboBox *direction;
    QAction *addChild, *addSibling, *addSiblingBefore, *editSelection, *deleteSelection, *toggleExpanded, *move, *up, *down, *addLink;
    QAction *rootSelection, *clearSelectionAction;
    QList<QAction *> nodeNavigation;
    enum class TopicOperation { Child, SiblingAfter, SiblingBefore };
    enum class Navigation { Parent, Child, PreviousSibling, NextSibling };
    QList<QAction *> menuActions;
    QAction *action(const char *name, const QString &text, const QList<QKeySequence> &shortcuts, std::function<void()> command, bool showInToolbar = true) {
        auto *result = new QAction(text, host);
        result->setObjectName(QString::fromLatin1(name));
        result->setShortcuts(shortcuts);
        result->setShortcutVisibleInContextMenu(true);
        QStringList keys;
        for (const auto &shortcut : shortcuts) keys.append(shortcut.toString(QKeySequence::NativeText));
        result->setToolTip(keys.isEmpty() ? text : text + QStringLiteral(" (%1)").arg(keys.join(QStringLiteral(", "))));
        result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(result);
        if (showInToolbar) {
            toolbar->addAction(result);
            menuActions.append(result);
        }
        QObject::connect(view, &MindMapView::topicEditingChanged, result, [result, shortcuts](bool editing) {
            result->setShortcuts(editing ? QList<QKeySequence>{} : shortcuts);
        });
        QObject::connect(result, &QAction::triggered, host, [this, command = std::move(command)] {
            view->finishTopicEdit(true);
            command();
        });
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
        addSibling->setEnabled(node); addSiblingBefore->setEnabled(node);
        for (auto *action : nodeNavigation) action->setEnabled(node);
        rootSelection->setEnabled(!nodes.empty());
        clearSelectionAction->setEnabled(node || hasLink);
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
    void createNode(TopicOperation operation) {
        const QString id = controller->selectedNodeId();
        const auto nodes = controller->choices();
        const auto *node = choice(nodes, id);
        if (!node) return;
        QString parentId = id;
        int index = -1;
        if ((operation == TopicOperation::SiblingAfter || operation == TopicOperation::SiblingBefore) && !node->parent.isEmpty()) {
            const auto *parent = choice(nodes, node->parent);
            if (!parent) return;
            parentId = parent->id;
            index = int(parent->children.indexOf(id)) + (operation == TopicOperation::SiblingAfter ? 1 : 0);
        }
        const auto *parent = choice(nodes, parentId);
        if (!parent || (!parent->expanded && !controller->setExpanded(parentId, true))) return;
        const QString created = controller->addNode(parentId, QString(), index);
        if (!created.isEmpty()) view->beginTopicEdit(created, config.shortcuts.acceptTopic);
    }
    void navigate(Navigation command) {
        const auto nodes = controller->choices();
        if (nodes.empty()) return;
        const auto *node = choice(nodes, controller->selectedNodeId());
        if (!node) return;
        QString target;
        if (command == Navigation::Parent) target = node->parent;
        else if (command == Navigation::Child) {
            if (node->expanded && !node->children.isEmpty()) target = node->children.front();
        } else {
            const auto *parent = choice(nodes, node->parent);
            if (!parent) return;
            const auto index = parent->children.indexOf(node->id) + (command == Navigation::PreviousSibling ? -1 : 1);
            if (index >= 0 && index < parent->children.size()) target = parent->children[index];
        }
        if (!target.isEmpty()) controller->selectNode(target);
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
    Private(MindMapEditor *editor, const EditorConfig &settings) : host(editor), config(settings) {
        auto *layout = new QVBoxLayout(editor);
        view = new MindMapView(editor);
        error = new QLabel(editor);
        error->setTextFormat(Qt::PlainText); error->setWordWrap(true); error->hide();
        controller = new MindMapController(*view, editor);
        toolbar = new QToolBar(editor);
        layout->addWidget(toolbar);
        addChild = action("addChild", tr("Add child"), config.shortcuts.addChild, [this] { createNode(TopicOperation::Child); });
        addSibling = action("addSibling", tr("Add sibling"), config.shortcuts.addSibling, [this] { createNode(TopicOperation::SiblingAfter); });
        addSiblingBefore = action("addSiblingBefore", tr("Add sibling before"), config.shortcuts.addSiblingBefore, [this] { createNode(TopicOperation::SiblingBefore); });
        editSelection = action("editSelection", tr("Rename/Edit"), config.shortcuts.editSelection, [this] {
            if (controller->selectedLinkId().isEmpty())
                view->beginTopicEdit(controller->selectedNodeId(), config.shortcuts.acceptTopic);
            else linkDialog(false);
        });
        deleteSelection = action("deleteSelection", tr("Delete"), config.shortcuts.deleteSelection, [this] {
            const auto link = controller->selectedLinkId(), node = controller->selectedNodeId();
            if (!link.isEmpty()) controller->removeLink(link);
            else if (!node.isEmpty() && (!config.confirmSubtreeDeletion || QMessageBox::question(host, tr("Delete subtree"),
                tr("Delete this node and all its descendants?"), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes))
                controller->removeNode(node);
        });
        toggleExpanded = action("toggleExpanded", tr("Expand/Collapse"), config.shortcuts.toggleExpanded, [this] {
            const auto nodes = controller->choices();
            const auto *node = choice(nodes, controller->selectedNodeId());
            if (node) controller->setExpanded(node->id, !node->expanded);
        });
        move = action("moveNode", tr("Move..."), config.shortcuts.moveNode, [this] { moveDialog(); });
        up = action("moveUp", tr("Move up"), config.shortcuts.moveUp, [this] { reorder(-1); });
        down = action("moveDown", tr("Move down"), config.shortcuts.moveDown, [this] { reorder(1); });
        addLink = action("addLink", tr("Add link"), config.shortcuts.addLink, [this] { linkDialog(true); });
        toolbar = new QToolBar(editor);
        layout->addWidget(toolbar);
        action("zoomIn", tr("Zoom +"), config.shortcuts.zoomIn, [this] { view->zoom(1.2); });
        action("zoomOut", tr("Zoom -"), config.shortcuts.zoomOut, [this] { view->zoom(1 / 1.2); });
        action("resetZoom", tr("100%"), config.shortcuts.resetZoom, [this] { view->resetZoom(); });
        action("fit", tr("Fit"), config.shortcuts.fit, [this] { view->fitContents(); });
        rootSelection = action("selectRoot", tr("Focus main node"), config.shortcuts.selectRoot, [this] { host->focusRoot(); });
        direction = new QComboBox(toolbar);
        direction->addItem(tr("Balanced"), int(LayoutDirection::Balanced));
        direction->addItem(tr("Right"), int(LayoutDirection::Right));
        direction->addItem(tr("Left"), int(LayoutDirection::Left));
        direction->setAccessibleName(tr("Layout direction"));
        toolbar->addWidget(direction);
        QObject::connect(direction, &QComboBox::currentIndexChanged, editor, [this] {
            const auto requested = static_cast<LayoutDirection>(direction->currentData().toInt());
            view->finishTopicEdit(true);
            controller->setLayoutDirection(requested);
            updateActions();
        });
        nodeNavigation = {
            action("selectParent", tr("Select parent"), config.shortcuts.selectParent, [this] { navigate(Navigation::Parent); }, false),
            action("selectChild", tr("Select first child"), config.shortcuts.selectChild, [this] { navigate(Navigation::Child); }, false),
            action("previousSibling", tr("Select previous sibling"), config.shortcuts.previousSibling, [this] { navigate(Navigation::PreviousSibling); }, false),
            action("nextSibling", tr("Select next sibling"), config.shortcuts.nextSibling, [this] { navigate(Navigation::NextSibling); }, false)
        };
        clearSelectionAction = action("clearSelection", tr("Clear selection"), config.shortcuts.clearSelection, [this] { controller->clearSelection(); }, false);
        layout->addWidget(view, 1); layout->addWidget(error);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(view, &QWidget::customContextMenuRequested, editor, [this](const QPoint &point) {
            view->finishTopicEdit(true);
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
        QObject::connect(view, &MindMapView::nodeMoveRequested, controller, &MindMapController::moveNode);
        QObject::connect(view, &MindMapView::linkPicked, controller, &MindMapController::selectLink);
        QObject::connect(view, &MindMapView::emptyPicked, controller, &MindMapController::clearSelection);
        QObject::connect(view, &MindMapView::expansionRequested, controller, &MindMapController::setExpanded);
        QObject::connect(view, &MindMapView::appearanceChanged, controller, &MindMapController::refreshAppearance);
        QObject::connect(view, &MindMapView::editRequested, editSelection, &QAction::trigger);
        QObject::connect(view, &MindMapView::topicEditRequested, controller, &MindMapController::renameNode);
        controller->newDocument(QStringLiteral("Central topic"));
        updateActions();
    }
};
MindMapEditor::MindMapEditor(QWidget *parent) : MindMapEditor(EditorConfig{}, parent) {}
MindMapEditor::MindMapEditor(const EditorConfig &config, QWidget *parent)
    : QWidget(parent), d(std::make_unique<Private>(this, config)) {}
MindMapEditor::~MindMapEditor() {
    // Disarm the input before QWidget teardown can send it a committing FocusOut.
    delete d->view;
}
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
bool MindMapEditor::focusRoot() {
    d->view->finishTopicEdit(true);
    const auto nodes = d->controller->choices();
    if (nodes.empty() || !d->controller->selectNode(nodes.front().id)) return false;
    d->view->centerNode(nodes.front().id);
    d->view->setFocus(Qt::OtherFocusReason);
    return true;
}
}
