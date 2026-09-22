#include "mindmap_controller.h"
#include "mindmap_view.h"
#include "html_export.h"
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QStringView>
#include <QUuid>
#include <nlohmann/json.hpp>
#include <cmath>
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
struct ParsedTopic { QString topic; QStringList tags; };
ParsedTopic parseTopicEdit(const QString &draft) {
    if (!draft.contains(u'#')) return {draft, {}};
    static const QRegularExpression tokens(QStringLiteral(R"(##|#([\p{L}\p{N}_-][\p{L}\p{M}\p{N}_-]*))"));
    ParsedTopic result;
    result.topic.reserve(draft.size());
    const QStringView source(draft);
    auto matches = tokens.globalMatchView(source);
    qsizetype offset = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result.topic.append(source.sliced(offset, match.capturedStart() - offset));
        if (match.capturedLength(1) > 0) result.tags.append(match.captured(1));
        else result.topic.append(u'#');
        offset = match.capturedEnd();
    }
    result.topic.append(source.sliced(offset));
    if (!result.tags.isEmpty()) {
        QChar *characters = result.topic.data();
        qsizetype written = 0;
        bool lineStart = true, pendingSpace = false;
        for (qsizetype i = 0; i < result.topic.size(); ++i) {
            const QChar character = characters[i];
            if (character == u'\n') {
                characters[written++] = character;
                lineStart = true;
                pendingSpace = false;
            } else if (character.isSpace()) {
                pendingSpace = lineStart == false;
            } else {
                if (pendingSpace) characters[written++] = u' ';
                characters[written++] = character;
                lineStart = false;
                pendingSpace = false;
            }
        }
        result.topic.truncate(written);
    }
    return result;
}
M3LayoutDirection coreDirection(MindMapEditor::LayoutDirection d) {
    switch (d) {
    case MindMapEditor::LayoutDirection::Balanced: return M3_LAYOUT_BALANCED;
    case MindMapEditor::LayoutDirection::Right: return M3_LAYOUT_RIGHT;
    case MindMapEditor::LayoutDirection::Left: return M3_LAYOUT_LEFT;
    case MindMapEditor::LayoutDirection::Outline: return M3_LAYOUT_OUTLINE;
    }
    throw std::runtime_error("Invalid layout direction");
}
QRectF rectangle(const Json &j) {
    return {j.at("x").get<double>(), j.at("y").get<double>(),
            j.at("width").get<double>(), j.at("height").get<double>()};
}
NodeStyle nodeStyle(const Json &style) {
    NodeStyle result;
    if (!style.is_object()) return result;
    if (const auto size = style.find("fontSize"); size != style.end()) {
        qreal pixels = 0;
        if (size->is_number()) pixels = size->get<qreal>();
        else if (size->is_string()) {
            QString text = string(*size).trimmed();
            if (text.endsWith(QStringLiteral("px"), Qt::CaseInsensitive)) {
                text.chop(2);
                bool valid = false;
                pixels = text.toDouble(&valid);
                if (!valid) pixels = 0;
            }
        }
        if (std::isfinite(pixels) && pixels >= 1 && pixels <= 256) result.fontSize = pixels;
    }
    if (const auto weight = style.find("fontWeight"); weight != style.end()) {
        if (weight->is_number()) {
            const double value = weight->get<double>();
            if (std::isfinite(value) && value >= 100 && value <= 900) result.bold = value >= 600;
        } else if (weight->is_string()) {
            const QString value = string(*weight).trimmed();
            if (value.compare(QStringLiteral("bold"), Qt::CaseInsensitive) == 0) result.bold = true;
            else if (value.compare(QStringLiteral("normal"), Qt::CaseInsensitive) == 0) result.bold = false;
        }
    }
    if (const auto fontStyle = style.find("fontStyle"); fontStyle != style.end() && fontStyle->is_string()) {
        const QString value = string(*fontStyle).trimmed();
        if (value.compare(QStringLiteral("italic"), Qt::CaseInsensitive) == 0) result.italic = true;
        else if (value.compare(QStringLiteral("normal"), Qt::CaseInsensitive) == 0) result.italic = false;
    }
    if (const auto color = style.find("color"); color != style.end() && color->is_string())
        result.textColor = QColor(string(*color));
    if (const auto color = style.find("background"); color != style.end() && color->is_string())
        result.backgroundColor = QColor(string(*color));
    return result;
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
QString MindMapController::toMarkdown() {
    try {
        char *raw = nullptr;
        const auto result = m3_mindmap_to_markdown(model.get(), &raw);
        Text text(raw, m3_string_free);
        requireStatus(result);
        return QString::fromUtf8(text.get());
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
QString MindMapController::toHtml() {
    try {
        const auto document = snapshot(model.get());
        auto presentation = prepare(model.get(), direction);
        const auto image = view.renderImage(std::move(presentation));
        return encodeHtml(document, image);
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
Presentation MindMapController::prepare(const M3Mindmap *map, MindMapEditor::LayoutDirection requested) {
    const auto bytes = snapshot(map);
    const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
    const auto &nodes = data.at("nodes");
    std::unordered_map<std::string, const Json *> index;
    index.reserve(nodes.size());
    for (const auto &n : nodes) index.emplace(n.at("id").get<std::string>(), &n);
    Presentation result;
    result.outline = requested == MindMapEditor::LayoutDirection::Outline;
    std::vector<const Json *> pending{index.at(data.at("rootId").get<std::string>())};
    while (!pending.empty()) {
        const auto &record = *pending.back();
        pending.pop_back();
        NodePresentation node;
        node.id = string(record.at("id"));
        node.topic = string(record.at("topic"));
        node.hyperlink = string(record.at("hyperLink"));
        for (const auto &icon : record.at("icons")) node.icons.append(string(icon));
        node.tags.reserve(record.at("tags").size());
        for (const auto &tag : record.at("tags")) {
            auto value = string(tag);
            if (!value.trimmed().isEmpty()) node.tags.push_back({std::move(value), {}, {}});
        }
        node.root = record.at("id") == data.at("rootId");
        node.expanded = record.at("expanded").get<bool>();
        const auto &children = record.at("children");
        node.hasChildren = children.empty() == false;
        node.style = nodeStyle(record.at("style"));
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
    const M3LayoutOptions options{coreDirection(requested), result.outline ? 32.0 : 64.0,
                                  result.outline ? 8.0 : 20.0};
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
bool MindMapController::commitTopicEdit(const QString &id, const QString &draft) {
    if (!strings({id, draft})) return false;
    try {
        const auto parsed = parseTopicEdit(draft);
        Json patch{{"topic", utf8(parsed.topic)}};
        if (!parsed.tags.isEmpty()) {
            char *raw = nullptr;
            const auto read = m3_mindmap_get_node_json(model.get(), id.toUtf8().constData(), &raw);
            Text text(raw, m3_string_free);
            requireStatus(read);
            const auto current = Json::parse(text.get());
            auto &tags = patch["tags"] = current.at("tags");
            for (const auto &tag : parsed.tags) tags.push_back(utf8(tag));
        }
        return updateNodeProperties(id, QByteArray::fromStdString(patch.dump()));
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::updateNodeProperties(const QString &id, const QByteArray &patch) {
    if (!strings({id})) return false;
    if (patch.contains('\0')) return fail(tr("JSON cannot contain NUL bytes"));
    try {
        auto update = Json::parse(patch.constData(), patch.constData() + patch.size());
        char *raw = nullptr;
        const auto read = m3_mindmap_get_node_json(model.get(), id.toUtf8().constData(), &raw);
        Text text(raw, m3_string_free);
        requireStatus(read);
        const auto original = Json::parse(text.get());
        if (update.is_object()) {
            const auto style = update.find("style");
            if (style != update.end() && style->is_object()) {
                Json merged = original.at("style");
                for (auto member = style->begin(); member != style->end(); ++member) {
                    if (member->is_null()) merged.erase(member.key());
                    else merged[member.key()] = *member;
                }
                *style = std::move(merged);
            }
        }
        bool modified = update.is_object() == false;
        if (!modified) {
            for (auto member = update.begin(); member != update.end(); ++member) {
                const auto previous = original.find(member.key());
                if (previous == original.end() || *previous != *member) {
                    modified = true;
                    break;
                }
            }
        }
        const auto serialized = update.dump();
        const auto result = m3_mindmap_update_node(model.get(), id.toUtf8().constData(), serialized.c_str());
        if (modified) return changed(result);
        if (!status(result)) return false;
        success();
        return true;
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
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
    if (!changed(m3_mindmap_update_node(model.get(), id.toUtf8().constData(), patch.c_str()), fallback)) return false;
    if (expanded) view.centerNode(id);
    return true;
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
NodeProperties MindMapController::nodeProperties(const QString &id) {
    if (!model || id.isEmpty()) return {};
    try {
        const auto bytes = snapshot(model.get());
        const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        const auto key = utf8(id);
        for (const auto &record : data.at("nodes")) {
            if (record.at("id") != key) continue;
            NodeProperties result;
            result.id = string(record.at("id"));
            result.topic = string(record.at("topic"));
            result.hyperlink = string(record.at("hyperLink"));
            result.note = string(record.at("note"));
            for (const auto &tag : record.at("tags")) result.tags.append(string(tag));
            for (const auto &icon : record.at("icons")) result.icons.append(string(icon));
            result.root = record.at("id") == data.at("rootId");
            result.style = nodeStyle(record.at("style"));
            return result;
        }
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    return {};
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
