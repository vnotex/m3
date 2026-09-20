#include "mindmap_controller.h"
#include "mindmap_view.h"
#include <QHash>
#include <QUuid>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>

namespace m3::qt {
namespace {
using Json = nlohmann::json;
using Text = std::unique_ptr<char, decltype(&m3_string_free)>;
QString string(const Json &j) { return QString::fromStdString(j.get<std::string>()); }
std::string utf8(const QString &s) { return s.toUtf8().toStdString(); }
void requireStatus(M3Status result) {
    if (result != M3_OK) throw std::runtime_error(m3_last_error());
}
M3LayoutDirection coreDirection(MindMapEditor::LayoutDirection d) {
    switch (d) {
    case MindMapEditor::LayoutDirection::Balanced: return M3_LAYOUT_BALANCED;
    case MindMapEditor::LayoutDirection::Right: return M3_LAYOUT_RIGHT;
    case MindMapEditor::LayoutDirection::Left: return M3_LAYOUT_LEFT;
    }
    throw std::runtime_error("Invalid layout direction");
}
QRectF rectangle(const Json &j) {
    return {j.at("x").get<double>(), j.at("y").get<double>(),
            j.at("width").get<double>(), j.at("height").get<double>()};
}
LinkPresentation linkPresentation(const Json &j) {
    return {string(j.at("id")), string(j.at("source")), string(j.at("target")),
            string(j.at("topic")), j.at("directed").get<bool>()};
}
}
MindMapController::MindMapController(MindMapView &v, QObject *parent) : QObject(parent), view(v) {}
bool MindMapController::fail(const QString &message) {
    error = message;
    emit errorOccurred(error);
    return false;
}
bool MindMapController::status(M3Status result) {
    return result == M3_OK || fail(QString::fromUtf8(m3_last_error()));
}
bool MindMapController::strings(std::initializer_list<QString> values) {
    for (const auto &s : values) if (s.contains(QChar::Null)) return fail(tr("Text cannot contain NUL characters"));
    return true;
}
void MindMapController::success() { error.clear(); emit commandSucceeded(); }
QByteArray MindMapController::snapshot(const M3Mindmap *map) {
    char *raw = nullptr;
    const auto result = m3_mindmap_to_json(map, &raw);
    Text text(raw, m3_string_free);
    requireStatus(result);
    return QByteArray(text.get());
}
QByteArray MindMapController::toJson() {
    try { return snapshot(model.get()); }
    catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
Presentation MindMapController::prepare(const M3Mindmap *map, MindMapEditor::LayoutDirection requested) {
    const auto bytes = snapshot(map);
    const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
    const auto &nodes = data.at("nodes");
    std::unordered_map<std::string, const Json *> index;
    index.reserve(nodes.size());
    for (const auto &n : nodes) index.emplace(n.at("id").get<std::string>(), &n);
    Presentation result;
    std::vector<const Json *> pending{index.at(data.at("rootId").get<std::string>())};
    while (!pending.empty()) {
        const auto &record = *pending.back();
        pending.pop_back();
        NodePresentation node;
        node.id = string(record.at("id"));
        node.topic = string(record.at("topic"));
        node.root = record.at("id") == data.at("rootId");
        node.expanded = record.at("expanded").get<bool>();
        const auto &children = record.at("children");
        node.hasChildren = children.empty() == false;
        view.prepare(node);
        if (node.expanded)
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                pending.push_back(index.at(it->get<std::string>()));
        result.nodes.push_back(std::move(node));
    }
    std::vector<QByteArray> ids;
    ids.reserve(result.nodes.size());
    for (const auto &node : result.nodes) ids.push_back(node.id.toUtf8());
    std::vector<M3NodeSize> sizes;
    sizes.reserve(result.nodes.size());
    for (size_t i = 0; i < result.nodes.size(); ++i)
        sizes.push_back({ids[i].constData(), result.nodes[i].rectangle.width(), result.nodes[i].rectangle.height()});
    const M3LayoutOptions options{coreDirection(requested), 64, 20};
    char *raw = nullptr;
    const auto status = m3_mindmap_layout_json(map, sizes.data(), sizes.size(), &options, &raw);
    Text layoutText(raw, m3_string_free);
    requireStatus(status);
    const auto layout = Json::parse(layoutText.get());
    QHash<QString, size_t> visible;
    for (size_t i = 0; i < result.nodes.size(); ++i) visible.insert(result.nodes[i].id, i);
    for (const auto &n : layout.at("nodes")) result.nodes[visible.value(string(n.at("id")))].rectangle = rectangle(n);
    for (const auto &e : layout.at("treeEdges")) result.treeEdges.push_back({string(e.at("source")), string(e.at("target"))});
    for (const auto &l : layout.at("crossLinks")) result.links.push_back(linkPresentation(l));
    result.bounds = rectangle(layout.at("bounds"));
    return result;
}
void MindMapController::install(Presentation presentation, bool fit) {
    QSet<QString> nodes, links;
    for (const auto &n : presentation.nodes) nodes.insert(n.id);
    for (const auto &l : presentation.links) links.insert(l.id);
    view.install(std::move(presentation), fit);
    visibleNodes.swap(nodes);
    visibleLinks.swap(links);
}
void MindMapController::selection(const QString &node, const QString &link) {
    if (node == selectedNode && link == selectedLink) {
        view.ensureNodeVisible(node);
        return;
    }
    selectedNode = node;
    selectedLink = link;
    view.setSelection(node, link);
    view.ensureNodeVisible(node);
    emit selectionChanged(node, link);
}
bool MindMapController::replace(Map candidate) {
    try {
        auto presentation = prepare(candidate.get(), direction);
        const QString root = presentation.nodes.front().id;
        install(std::move(presentation), true);
        model.swap(candidate);
        success();
        selection(root, {});
        view.setSelection(selectedNode, selectedLink);
        emit documentChanged();
        return true;
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::newDocument(const QString &topic) {
    if (!strings({topic})) return false;
    M3Mindmap *raw = nullptr;
    const auto result = m3_mindmap_create("root", topic.toUtf8().constData(), &raw);
    Map candidate(raw, m3_mindmap_destroy);
    return status(result) && replace(std::move(candidate));
}
bool MindMapController::loadJson(const QByteArray &json) {
    if (json.contains('\0')) return fail(tr("JSON cannot contain NUL bytes"));
    M3Mindmap *raw = nullptr;
    const auto result = m3_mindmap_from_json(json.constData(), &raw);
    Map candidate(raw, m3_mindmap_destroy);
    return status(result) && replace(std::move(candidate));
}
bool MindMapController::selectNode(const QString &id) {
    if (!visibleNodes.contains(id)) return fail(tr("Node is not visible"));
    success(); selection(id, {}); return true;
}
bool MindMapController::selectLink(const QString &id) {
    if (!visibleLinks.contains(id)) return fail(tr("Link is not visible"));
    success(); selection({}, id); return true;
}
void MindMapController::clearSelection() { success(); selection({}, {}); }
bool MindMapController::changed(M3Status result, const QString &preferredNode, const QString &preferredLink) {
    if (!status(result)) return false;
    success();
    try {
        install(prepare(model.get(), direction), false);
        if (visibleNodes.contains(preferredNode)) selection(preferredNode, {});
        else if (visibleLinks.contains(preferredLink)) selection({}, preferredLink);
        else if (visibleNodes.contains(selectedNode)) view.setSelection(selectedNode, {});
        else if (visibleLinks.contains(selectedLink)) view.setSelection({}, selectedLink);
        else selection({}, {});
    } catch (const std::exception &e) {
        visibleNodes.clear(); visibleLinks.clear();
        view.showError(QString::fromUtf8(e.what()));
        selection({}, {});
        fail(QString::fromUtf8(e.what()));
    }
    emit documentChanged();
    return true;
}
QString MindMapController::addNode(const QString &parent, const QString &topic, int index) {
    if (!strings({parent, topic})) return {};
    if (index < -1) { fail(tr("Invalid insertion index")); return {}; }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto record = Json{{"id", utf8(id)}, {"topic", utf8(topic)}}.dump();
    return changed(m3_mindmap_insert_node(model.get(), parent.toUtf8().constData(),
                   index == -1 ? M3_APPEND : size_t(index), record.c_str()), id) ? id : QString();
}
bool MindMapController::renameNode(const QString &id, const QString &topic) {
    if (!strings({id, topic})) return false;
    const auto patch = Json{{"topic", utf8(topic)}}.dump();
    return changed(m3_mindmap_update_node(model.get(), id.toUtf8().constData(), patch.c_str()));
}
bool MindMapController::removeNode(const QString &id) {
    if (!strings({id})) return false;
    QHash<QString, QString> parents;
    for (const auto &n : choices()) parents.insert(n.id, n.parent);
    QString fallback;
    for (QString current = selectedNode; !current.isEmpty(); current = parents.value(current))
        if (current == id) { fallback = parents.value(id); break; }
    while (!fallback.isEmpty() && !visibleNodes.contains(fallback)) fallback = parents.value(fallback);
    return changed(m3_mindmap_remove_subtree(model.get(), id.toUtf8().constData()), fallback);
}
bool MindMapController::moveNode(const QString &id, const QString &parent, int index) {
    if (!strings({id, parent})) return false;
    if (index < -1) return fail(tr("Invalid insertion index"));
    return changed(m3_mindmap_move_node(model.get(), id.toUtf8().constData(), parent.toUtf8().constData(),
                                      index == -1 ? M3_APPEND : size_t(index)));
}
bool MindMapController::setExpanded(const QString &id, bool expanded) {
    if (!strings({id})) return false;
    QString fallback;
    if (!expanded) {
        QHash<QString, QString> parents;
        for (const auto &n : choices()) parents.insert(n.id, n.parent);
        for (QString current = selectedNode; !current.isEmpty(); current = parents.value(current))
            if (current == id) { fallback = id; break; }
    }
    const auto patch = Json{{"expanded", expanded}}.dump();
    return changed(m3_mindmap_update_node(model.get(), id.toUtf8().constData(), patch.c_str()), fallback);
}
QString MindMapController::addLink(const QString &source, const QString &target, bool directed, const QString &topic) {
    if (!strings({source, target, topic})) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto record = Json{{"id", utf8(id)}, {"source", utf8(source)}, {"target", utf8(target)},
                             {"directed", directed}, {"topic", utf8(topic)}}.dump();
    return changed(m3_mindmap_add_link(model.get(), record.c_str()), {}, id) ? id : QString();
}
bool MindMapController::updateLink(const QString &id, const QString &source, const QString &target, bool directed, const QString &topic) {
    if (!strings({id, source, target, topic})) return false;
    const auto patch = Json{{"source", utf8(source)}, {"target", utf8(target)},
                            {"directed", directed}, {"topic", utf8(topic)}}.dump();
    return changed(m3_mindmap_update_link(model.get(), id.toUtf8().constData(), patch.c_str()));
}
bool MindMapController::removeLink(const QString &id) {
    return strings({id}) && changed(m3_mindmap_remove_link(model.get(), id.toUtf8().constData()));
}
bool MindMapController::setLayoutDirection(MindMapEditor::LayoutDirection requested) {
    try {
        auto presentation = prepare(model.get(), requested);
        install(std::move(presentation), false);
        direction = requested;
        view.setSelection(selectedNode, selectedLink);
        success();
        return true;
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
std::vector<NodeChoice> MindMapController::choices() {
    std::vector<NodeChoice> result;
    if (!model) return result;
    try {
        const auto bytes = snapshot(model.get());
        const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        QHash<QString, QString> parents;
        for (const auto &n : data.at("nodes"))
            for (const auto &c : n.at("children")) parents.insert(string(c), string(n.at("id")));
        for (const auto &n : data.at("nodes")) {
            NodeChoice choice;
            choice.id = string(n.at("id")); choice.topic = string(n.at("topic"));
            choice.parent = parents.value(choice.id); choice.expanded = n.at("expanded").get<bool>();
            for (const auto &c : n.at("children")) choice.children.append(string(c));
            result.push_back(std::move(choice));
        }
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    return result;
}
LinkPresentation MindMapController::linkChoice(const QString &id) {
    try {
        const auto bytes = snapshot(model.get());
        const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        for (const auto &l : data.at("crossLinks")) if (string(l.at("id")) == id) return linkPresentation(l);
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    return {};
}
void MindMapController::refreshAppearance() {
    if (!model) return;
    try {
        install(prepare(model.get(), direction), false);
        view.setSelection(selectedNode, selectedLink);
    } catch (const std::exception &e) {
        visibleNodes.clear(); visibleLinks.clear();
        view.showError(QString::fromUtf8(e.what()));
        selection({}, {});
        fail(QString::fromUtf8(e.what()));
    }
}
}
