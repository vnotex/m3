#include "mindmap_controller.h"
#include "mindmap_view.h"
#include "html_export.h"
#include <QHash>
#include <QDir>
#include <QUrl>
#include <QPointer>
#include <QTimer>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QStringList>
#include <QStringView>
#include <QUuid>
#include <nlohmann/json.hpp>
#include <array>
#include <cmath>
#include <iterator>
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
std::optional<NodeImage> nodeImage(const Json &value) {
    if (value.is_null()) return std::nullopt;
    return NodeImage{string(value.at("url")), value.at("width").get<double>(), value.at("height").get<double>()};
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
    if (const auto color = style.find("branchColor"); color != style.end() && color->is_string())
        result.branchColor = QColor(string(*color));
    return result;
}
LinkPresentation linkPresentation(const Json &j) {
    auto direction = LinkDirection::None;
    if (j.at("directed").get<bool>()) {
        const auto &style = j.at("style");
        const auto arrow = style.find("arrowDirection");
        direction = arrow != style.end() && *arrow == "backward" ? LinkDirection::Backward
            : arrow != style.end() && *arrow == "both" ? LinkDirection::Both : LinkDirection::Forward;
    }
    return {string(j.at("id")), string(j.at("source")), string(j.at("target")), string(j.at("topic")), direction};
}
}
MindMapController::MindMapController(MindMapView &v, const EditorConfig &config, QObject *parent)
    : QObject(parent), view(v), resourceBase(QDir::cleanPath(QDir::current().absoluteFilePath(config.resourceBasePath))),
      autoRandomBranchColor(config.autoRandomBranchColor) {}
QString MindMapController::resolveResourceUrl(const QString &value) const {
    if (value.isEmpty()) return {};
    const QString path = QDir::fromNativeSeparators(value);
    if (QDir::isAbsolutePath(path)) return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    // Spaces are valid filename input, but malformed URL escapes stay invalid.
    QString encodedPath = path;
    encodedPath.replace(u' ', QStringLiteral("%20"));
    QUrl reference(encodedPath, QUrl::StrictMode);
    if (!reference.isValid()) return value;
    if (!reference.scheme().isEmpty()) {
        if (reference.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0 ||
            reference.path().startsWith(u'/') || !reference.host().isEmpty()) return value;
        reference.setScheme(QString());
    }
    const QUrl base = QUrl::fromLocalFile(resourceBase.endsWith(u'/') ? resourceBase : resourceBase + u'/');
    return base.resolved(reference).toString(QUrl::FullyEncoded);
}
void MindMapController::provideImage(const QString &url, quint64 requestId, const QImage &image) {
    auto resource = imageResources.find(url);
    if (resource == imageResources.end() || resource->requestId != requestId || resource->completed) return;
    resource->pixels = image;
    resource->completed = true;
    scheduleImageRefresh();
}
void MindMapController::reloadImages() {
    imageResources.clear();
    ++imageGeneration;
    scheduleImageRefresh();
}
void MindMapController::scheduleImageRefresh() {
    if (imageRefreshScheduled) return;
    imageRefreshScheduled = true;
    QTimer::singleShot(0, this, [this] {
        imageRefreshScheduled = false;
        refreshAppearance();
    });
}
void MindMapController::scheduleImageRequests() {
    if (imageRequestsScheduled) return;
    imageRequestsScheduled = true;
    QTimer::singleShot(0, this, [this] {
        imageRequestsScheduled = false;
        if (!model) return;
        const auto generation = imageGeneration;
        const QPointer<MindMapController> alive(this);
        try {
            const auto bytes = snapshot(model.get());
            const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
            QSet<QString> referenced, visible;
            QStringList requests;
            for (const auto &node : data.at("nodes")) {
                const auto image = nodeImage(node.at("image"));
                if (!image || image->url.isEmpty()) continue;
                const QString url = resolveResourceUrl(image->url);
                referenced.insert(url);
                if (visibleNodes.contains(string(node.at("id"))) && !visible.contains(url)) {
                    visible.insert(url);
                    requests.append(url);
                }
            }
            for (auto it = imageResources.begin(); it != imageResources.end();) {
                if (!referenced.contains(it.key())) it = imageResources.erase(it);
                else ++it;
            }
            for (const auto &url : requests) {
                if (imageResources.contains(url)) continue;
                const quint64 id = nextImageRequestId++;
                imageResources.insert(url, ImageResource{id, {}, false});
                emit imageRequested(url, id);
                if (!alive || imageGeneration != generation) return;
            }
        } catch (const std::exception &e) {
            if (alive) fail(QString::fromUtf8(e.what()));
        }
    });
}
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
Presentation MindMapController::prepare(const M3Mindmap *map, MindMapEditor::LayoutDirection requested, bool useImageCache) {
    const auto bytes = snapshot(map);
    const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
    const auto &nodes = data.at("nodes");
    std::unordered_map<std::string, const Json *> index;
    index.reserve(nodes.size());
    for (const auto &n : nodes) index.emplace(n.at("id").get<std::string>(), &n);
    Presentation result;
    result.outline = requested == MindMapEditor::LayoutDirection::Outline;
    struct Frame { const Json *record; QColor inheritedBranchColor; };
    std::vector<Frame> pending{{index.at(data.at("rootId").get<std::string>()), {}}};
    while (!pending.empty()) {
        const auto frame = pending.back();
        pending.pop_back();
        const auto &record = *frame.record;
        NodePresentation node;
        node.id = string(record.at("id"));
        node.topic = string(record.at("topic"));
        node.hyperlink = string(record.at("hyperLink"));
        node.image = nodeImage(record.at("image"));
        if (useImageCache && node.image && !node.image->url.isEmpty()) {
            const auto resource = imageResources.constFind(resolveResourceUrl(node.image->url));
            if (resource != imageResources.cend()) {
                node.imagePixels = resource->pixels;
                node.imageFailed = resource->completed && resource->pixels.isNull();
            }
        }
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
        if (!node.style.branchColor.isValid()) node.style.branchColor = frame.inheritedBranchColor;
        view.prepare(node);
        if (node.expanded)
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                pending.push_back({index.at(it->get<std::string>()), node.style.branchColor});
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
    const QPointer<MindMapController> guard(this);
    view.install(std::move(presentation), fit);
    if (!guard) return;
    visibleNodes.swap(nodes);
    visibleLinks.swap(links);
    ++imageGeneration; // Also stop request delivery across a reentrant visual/semantic rebuild.
    scheduleImageRequests();
}
void MindMapController::selection(QStringList nodes, QString link, bool ensureVisible) {
    const bool modified = nodes != selectedNodes || link != selectedLink;
    selectedNodes = nodes;
    selectedLink = link;
    const QPointer<MindMapController> guard(this);
    // Reapply even unchanged membership after a scene rebuild.
    view.setSelection(nodes, link);
    if (!guard) return;
    if (ensureVisible && nodes.size() == 1) view.ensureNodeVisible(nodes.front());
    if (!guard) return;
    if (modified) emit selectionChanged(nodes.size() == 1 ? nodes.front() : QString(), link);
}
void MindMapController::restoreSelection() {
    auto nodes = selectedNodes;
    for (qsizetype i = nodes.size(); i > 0; --i)
        if (!visibleNodes.contains(nodes.at(i - 1))) nodes.removeAt(i - 1);
    selection(std::move(nodes), visibleLinks.contains(selectedLink) ? selectedLink : QString(), false);
}
bool MindMapController::replace(Map candidate) {
    const QPointer<MindMapController> guard(this);
    try {
        auto presentation = prepare(candidate.get(), direction, false);
        const QString root = presentation.nodes.front().id;
        install(std::move(presentation), true);
        if (!guard) return true;
        model.swap(candidate);
        imageResources.clear();
        ++imageGeneration;
        success();
        if (!guard) return true;
        selection({root}, {});
        if (!guard) return true;
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
    const QStringList nodes{id};
    const QPointer<MindMapController> guard(this);
    success();
    if (guard) selection(nodes, {});
    return true;
}
bool MindMapController::toggleNodeSelection(const QString &id) {
    if (!visibleNodes.contains(id)) return fail(tr("Node is not visible"));
    auto nodes = selectedNodes;
    if (!nodes.removeOne(id)) nodes.append(id);
    const QPointer<MindMapController> guard(this);
    success();
    if (guard) selection(std::move(nodes), {});
    return true;
}
bool MindMapController::selectLink(const QString &id) {
    if (!visibleLinks.contains(id)) return fail(tr("Link is not visible"));
    const QString link = id;
    const QPointer<MindMapController> guard(this);
    success();
    if (guard) selection({}, link);
    return true;
}
void MindMapController::clearSelection() {
    const QPointer<MindMapController> guard(this);
    success();
    if (guard) selection({}, {});
}
bool MindMapController::changed(M3Status result, const QString &preferredNode, const QString &preferredLink) {
    if (!status(result)) return false;
    const QPointer<MindMapController> guard(this);
    success();
    if (!guard) return true;
    try {
        install(prepare(model.get(), direction), false);
        if (!guard) return true;
        if (visibleNodes.contains(preferredNode)) selection({preferredNode}, {});
        else if (visibleLinks.contains(preferredLink)) selection({}, preferredLink);
        else restoreSelection();
    } catch (const std::exception &e) {
        visibleNodes.clear(); visibleLinks.clear();
        view.showError(QString::fromUtf8(e.what()));
        if (!guard) return true;
        selection({}, {});
        if (!guard) return true;
        fail(QString::fromUtf8(e.what()));
    }
    if (!guard) return true;
    emit documentChanged();
    return true;
}
QString MindMapController::addNode(const QString &parent, const QString &topic, int index) {
    if (!strings({parent, topic})) return {};
    if (index < -1) { fail(tr("Invalid insertion index")); return {}; }
    try {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        Json record{{"id", utf8(id)}, {"topic", utf8(topic)}};
        if (autoRandomBranchColor) {
            const auto bytes = snapshot(model.get());
            const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
            if (data.at("rootId") == utf8(parent)) {
                static const auto palette = [] {
                    std::array<QColor, std::size(nodeColors)> colors;
                    for (size_t i = 0; i < colors.size(); ++i)
                        colors[i] = QColor(QString::fromLatin1(nodeColors[i].hex));
                    return colors;
                }();
                std::array<bool, std::size(nodeColors)> used{};
                const auto &nodes = data.at("nodes");
                const auto &root = nodes.front();
                const QColor rootColor = nodeStyle(root.at("style")).branchColor;
                const auto &children = root.at("children");
                auto child = children.begin();
                // Native snapshots are root-first child preorder; skip each subtree
                // until the next direct child, without building another node index.
                for (const auto &node : nodes) {
                    if (child == children.end()) break;
                    if (node.at("id") != *child) continue;
                    QColor color = nodeStyle(node.at("style")).branchColor;
                    if (!color.isValid()) color = rootColor;
                    for (size_t i = 0; i < palette.size(); ++i)
                        if (color == palette[i]) used[i] = true;
                    ++child;
                }
                std::array<size_t, std::size(nodeColors)> candidates;
                int count = 0;
                for (size_t i = 0; i < used.size(); ++i)
                    if (!used[i]) candidates[count++] = i;
                if (count == 0)
                    for (size_t i = 0; i < used.size(); ++i) candidates[count++] = i;
                const auto picked = candidates[QRandomGenerator::global()->bounded(count)];
                record["style"] = {{"branchColor", nodeColors[picked].hex}};
            }
        }
        const auto serialized = record.dump();
        return changed(m3_mindmap_insert_node(model.get(), parent.toUtf8().constData(),
                       index == -1 ? M3_APPEND : size_t(index), serialized.c_str()), id) ? id : QString();
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
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
    QString fallback;
    if (selectedNodes.size() == 1) {
        const QPointer<MindMapController> guard(this);
        const auto nodes = choices();
        if (!guard) return false;
        QHash<QString, QString> parents;
        for (const auto &n : nodes) parents.insert(n.id, n.parent);
        for (QString current = selectedNodeId(); !current.isEmpty(); current = parents.value(current))
            if (current == id) { fallback = parents.value(id); break; }
        while (!fallback.isEmpty() && !visibleNodes.contains(fallback)) fallback = parents.value(fallback);
    }
    return changed(m3_mindmap_remove_subtree(model.get(), id.toUtf8().constData()), fallback);
}
bool MindMapController::removeSelectedNodes() {
    if (selectedNodes.isEmpty()) return fail(tr("No nodes are selected"));
    if (selectedNodes.size() == 1) return removeNode(selectedNodeId());
    try {
        const auto bytes = snapshot(model.get());
        const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        const QString root = string(data.at("rootId"));
        QHash<QString, QString> parents;
        parents.insert(root, {});
        for (const auto &node : data.at("nodes"))
            for (const auto &child : node.at("children")) parents.insert(string(child), string(node.at("id")));
        QSet<QString> selected;
        for (const auto &id : selectedNodes) {
            if (id == root) return fail(tr("Cannot remove root"));
            if (!parents.contains(id)) return fail(tr("Node not found"));
            selected.insert(id);
        }
        QStringList roots;
        for (const auto &id : selectedNodes) {
            QString ancestor = parents.value(id);
            while (!ancestor.isEmpty() && !selected.contains(ancestor)) ancestor = parents.value(ancestor);
            if (ancestor.isEmpty()) roots.append(id);
        }
        // Keep disjoint roots in selection order; the first root supplies the visible parent fallback.
        QString fallback = parents.value(roots.front());
        while (!fallback.isEmpty() && !visibleNodes.contains(fallback)) fallback = parents.value(fallback);
        M3Mindmap *raw = nullptr;
        const auto result = m3_mindmap_from_json(bytes.constData(), &raw);
        Map candidate(raw, m3_mindmap_destroy);
        requireStatus(result);
        for (const auto &id : roots)
            requireStatus(m3_mindmap_remove_subtree(candidate.get(), id.toUtf8().constData()));
        model.swap(candidate);
        return changed(M3_OK, fallback);
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
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
    const QPointer<MindMapController> guard(this);
    if (!expanded && selectedNodes.size() == 1) {
        const auto nodes = choices();
        if (!guard) return false;
        QHash<QString, QString> parents;
        for (const auto &n : nodes) parents.insert(n.id, n.parent);
        for (QString current = selectedNodeId(); !current.isEmpty(); current = parents.value(current))
            if (current == id) { fallback = id; break; }
    }
    const auto patch = Json{{"expanded", expanded}}.dump();
    if (!changed(m3_mindmap_update_node(model.get(), id.toUtf8().constData(), patch.c_str()), fallback)) return false;
    if (guard && expanded) view.centerNode(id);
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
bool MindMapController::setLinkDirection(const QString &id, LinkDirection direction) {
    if (!strings({id})) return false;
    try {
        char *raw = nullptr;
        const auto read = m3_mindmap_get_link_json(model.get(), id.toUtf8().constData(), &raw);
        Text text(raw, m3_string_free);
        requireStatus(read);
        auto current = Json::parse(text.get());
        if (linkPresentation(current).direction == direction) { success(); return true; }
        auto style = std::move(current.at("style"));
        switch (direction) {
        case LinkDirection::None:
        case LinkDirection::Forward: style.erase("arrowDirection"); break;
        case LinkDirection::Backward: style["arrowDirection"] = "backward"; break;
        case LinkDirection::Both: style["arrowDirection"] = "both"; break;
        default: return fail(tr("Invalid link direction"));
        }
        const auto patch = Json{{"directed", direction != LinkDirection::None}, {"style", std::move(style)}}.dump();
        return changed(m3_mindmap_update_link(model.get(), id.toUtf8().constData(), patch.c_str()));
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::reconnectLink(const QString &id, bool source, const QString &original, const QString &node) {
    if (!strings({id, original, node})) return false;
    if (selectedLink != id) return false;
    try {
        char *raw = nullptr;
        const auto read = m3_mindmap_get_link_json(model.get(), id.toUtf8().constData(), &raw);
        Text text(raw, m3_string_free);
        requireStatus(read);
        const auto current = Json::parse(text.get());
        const char *endpoint = source ? "source" : "target";
        if (current.at(endpoint) != utf8(original)) return false;
        if (original == node) { success(); return true; }
        const auto patch = Json{{endpoint, utf8(node)}}.dump();
        return changed(m3_mindmap_update_link(model.get(), id.toUtf8().constData(), patch.c_str()));
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::commitLinkTopicEdit(const QString &id, const QString &topic) {
    if (!strings({id, topic})) return false;
    const auto patch = Json{{"topic", utf8(topic)}}.dump();
    return changed(m3_mindmap_update_link(model.get(), id.toUtf8().constData(), patch.c_str()));
}
bool MindMapController::removeLink(const QString &id) {
    return strings({id}) && changed(m3_mindmap_remove_link(model.get(), id.toUtf8().constData()));
}
bool MindMapController::setLayoutDirection(MindMapEditor::LayoutDirection requested) {
    const QPointer<MindMapController> guard(this);
    try {
        auto presentation = prepare(model.get(), requested);
        install(std::move(presentation), false);
        if (!guard) return true;
        direction = requested;
        restoreSelection();
        if (!guard) return true;
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
            result.image = nodeImage(record.at("image"));
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
    const QPointer<MindMapController> guard(this);
    try {
        install(prepare(model.get(), direction), false);
        if (!guard) return;
        restoreSelection();
    } catch (const std::exception &e) {
        visibleNodes.clear(); visibleLinks.clear();
        view.showError(QString::fromUtf8(e.what()));
        if (!guard) return;
        selection({}, {});
        if (!guard) return;
        fail(QString::fromUtf8(e.what()));
    }
}
}
