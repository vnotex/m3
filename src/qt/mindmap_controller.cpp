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
#include <QUuid>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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
    auto matches = tokens.globalMatch(draft);
    qsizetype offset = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        result.topic.append(draft.constData() + offset, match.capturedStart() - offset);
        if (match.capturedLength(1) > 0) result.tags.append(match.captured(1));
        else result.topic.append(u'#');
        offset = match.capturedEnd();
    }
    result.topic.append(draft.constData() + offset, draft.size() - offset);
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
// One preparation context per command: sibling colors include earlier batch entries.
class NodeInsertion {
public:
    NodeInsertion(const QByteArray &snapshot, const QString &parent, bool automatic)
        : parentId(utf8(parent)) {
        if (!automatic || parent.isEmpty()) return;
        const auto data = Json::parse(snapshot.constData(), snapshot.constData() + snapshot.size());
        const auto orphans = data.find("orphanIds");
        colorBranches = data.at("rootId") == parentId ||
            (orphans != data.end() && std::find(orphans->begin(), orphans->end(), parentId) != orphans->end());
        if (!colorBranches) return;
        const auto &nodes = data.at("nodes");
        const auto &root = *std::find_if(nodes.begin(), nodes.end(),
            [this](const Json &node) { return node.at("id") == parentId; });
        rootColor = branchColor(root.at("style"));
        const auto &children = root.at("children");
        auto child = children.begin();
        // Native snapshots are root-first child preorder; skip each subtree.
        for (const auto &node : nodes) {
            if (child == children.end()) break;
            if (node.at("id") != *child) continue;
            remember(branchColor(node.at("style")));
            ++child;
        }
    }
    QString prepare(Json &record) {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        record["id"] = utf8(id);
        if (!colorBranches) return id;
        auto style = record.find("style");
        if (style != record.end()) {
            // Preserve caller style, including explicit branch-color policy. The
            // C insertion validates malformed records, without publishing any child.
            if (!style->is_object()) return id;
            if (style->contains("branchColor")) {
                remember(branchColor(*style));
                return id;
            }
        }
        std::array<size_t, std::size(nodeColors)> candidates;
        int count = 0;
        for (size_t i = 0; i < used.size(); ++i)
            if (!used[i]) candidates[count++] = i;
        if (count == 0)
            for (size_t i = 0; i < used.size(); ++i) candidates[count++] = i;
        const auto picked = candidates[QRandomGenerator::global()->bounded(count)];
        record["style"]["branchColor"] = nodeColors[picked].hex;
        used[picked] = true;
        return id;
    }
    const std::string parentId;
private:
    static QColor branchColor(const Json &style) {
        const auto color = style.find("branchColor");
        return color != style.end() && color->is_string() ? QColor(string(*color)) : QColor();
    }
    void remember(QColor color) {
        static const auto palette = [] {
            std::array<QColor, std::size(nodeColors)> colors;
            for (size_t i = 0; i < colors.size(); ++i)
                colors[i] = QColor(QString::fromLatin1(nodeColors[i].hex));
            return colors;
        }();
        if (!color.isValid()) color = rootColor;
        for (size_t i = 0; i < palette.size(); ++i)
            if (color == palette[i]) used[i] = true;
    }
    bool colorBranches = false;
    QColor rootColor;
    std::array<bool, std::size(nodeColors)> used{};
};
}
MindMapController::MindMapController(MindMapView &v, const EditorConfig &config, QObject *parent)
    : QObject(parent), undoLimit(size_t(std::max(0, config.undoLimit))),
      initialVisibleLevels(std::max(0, config.initialVisibleLevels)), view(v),
      resourceBase(QDir::cleanPath(QDir::current().absoluteFilePath(config.resourceBasePath))),
      resolveRelativeUrls(config.resolveRelativeUrls), autoRandomBranchColor(config.autoRandomBranchColor) {}
void MindMapController::setResourceBasePath(const QString &path) {
    resourceBase = QDir::cleanPath(QDir::current().absoluteFilePath(path));
    reloadImages();
}
void MindMapController::setReadOnly(bool value) {
    if (readOnly == value) return;
    readOnly = value;
    const QPointer<MindMapController> guard(this);
    view.setReadOnly(value);
    if (guard) view.setSelection(selectedNodes, selectedLink);
    if (guard) notifyHistoryAvailability();
}
QString MindMapController::resolveResourceUrl(const QString &value) const {
    if (!resolveRelativeUrls || value.isEmpty()) return value;
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
            const auto &bytes = currentSnapshot;
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
bool MindMapController::writable() {
    return !readOnly || fail(tr("The document is read-only"));
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
    if (currentSnapshot.isEmpty()) { fail(tr("No document is loaded")); return {}; }
    return currentSnapshot;
}
QByteArray MindMapController::nodeJson(const QString &id) {
    if (!strings({id})) return {};
    try {
        char *raw = nullptr;
        const auto result = m3_mindmap_get_node_json(model.get(), id.toUtf8().constData(), &raw);
        Text text(raw, m3_string_free);
        requireStatus(result);
        return QByteArray(text.get());
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
QVector<OutlineEntry> MindMapController::outline() {
    try {
        char *raw = nullptr;
        const auto exported = m3_mindmap_get_outline_json(model.get(), &raw);
        Text text(raw, m3_string_free);
        requireStatus(exported);
        if (!text || !*text) throw std::runtime_error("The document outline is empty");
        const auto data = Json::parse(text.get());
        struct Frame { const Json *node; int level; };
        std::vector<Frame> pending;
        if (const auto orphans = data.find("orphans"); orphans != data.end()) {
            for (auto it = orphans->rbegin(); it != orphans->rend(); ++it)
                pending.push_back({&*it, 1});
        }
        pending.push_back({&data, 1});
        QVector<OutlineEntry> result;
        while (!pending.empty()) {
            const auto frame = pending.back();
            pending.pop_back();
            const auto &node = *frame.node;
            result.push_back({string(node.at("id")), string(node.at("topic")), frame.level});
            const auto &children = node.at("children");
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                pending.push_back({&*it, frame.level + 1});
        }
        if (result.isEmpty()) throw std::runtime_error("The document outline is empty");
        return result;
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
bool MindMapController::revealNode(const QString &id) { return revealTarget({id, false}); }
bool MindMapController::revealTarget(FindTarget target) {
    if (!strings({target.id})) return false;
    const QPointer<MindMapController> guard(this);
    const auto revision = documentRevision;
    try {
        if (!(target.link ? visibleLinks : visibleNodes).contains(target.id)) {
            const auto data = Json::parse(currentSnapshot.constData(), currentSnapshot.constData() + currentSnapshot.size());
            QHash<QString, const Json *> nodes;
            QHash<QString, QString> parents;
            for (const auto &node : data.at("nodes")) {
                const auto id = string(node.at("id"));
                nodes.insert(id, &node);
                for (const auto &child : node.at("children")) parents.insert(string(child), id);
            }
            QStringList endpoints;
            if (target.link) {
                for (const auto &link : data.at("crossLinks")) {
                    if (string(link.at("id")) != target.id) continue;
                    endpoints = QStringList{string(link.at("source")), string(link.at("target"))};
                    break;
                }
                if (endpoints.isEmpty()) return fail(tr("Link not found"));
            } else {
                if (!nodes.contains(target.id)) return fail(tr("Node not found"));
                endpoints.append(target.id);
            }
            auto collapsed = collapsedNodes;
            for (const auto &endpoint : endpoints) {
                for (QString ancestor = parents.value(endpoint); !ancestor.isEmpty(); ancestor = parents.value(ancestor)) {
                    collapsed.remove(ancestor);
                }
            }
            auto presentation = prepare(currentSnapshot, direction, collapsed);
            collapsedNodes.swap(collapsed);
            install(std::move(presentation), false);
            if (!guard || revision != documentRevision) return false;
        }
        if (!(target.link ? visibleLinks : visibleNodes).contains(target.id))
            return fail(tr("The requested item could not be revealed"));
        success();
        if (!guard || revision != documentRevision) return false;
        if (target.link) {
            selection({}, target.id, false);
            if (guard && revision == documentRevision && selectedLink == target.id) view.ensureLinkVisible(target.id);
        } else {
            selection({target.id}, {});
        }
        return true;
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
FindResult MindMapController::findText(const QString &text, Qt::CaseSensitivity sensitivity, bool backward, bool incremental) {
    if (text.isEmpty()) { clearFind(); return {}; }
    const QPointer<MindMapController> guard(this);
    const auto revision = documentRevision;
    try {
        const FindTarget previous = currentFindMatch >= 0 && currentFindMatch < findMatches.size()
            ? findMatches.at(currentFindMatch) : FindTarget{};
        const bool changed = !findCacheValid || findQuery != text || findSensitivity != sensitivity;
        if (changed) {
            const auto data = Json::parse(currentSnapshot.constData(), currentSnapshot.constData() + currentSnapshot.size());
            QVector<FindTarget> matches;
            // Canonical snapshots already export full node preorder followed by
            // bytewise ID-sorted links, independent of presentation visibility.
            for (const auto &node : data.at("nodes"))
                if (string(node.at("topic")).contains(text, sensitivity)) matches.push_back({string(node.at("id")), false});
            for (const auto &link : data.at("crossLinks"))
                if (string(link.at("topic")).contains(text, sensitivity)) matches.push_back({string(link.at("id")), true});
            findQuery = text;
            findSensitivity = sensitivity;
            findMatches.swap(matches);
            findCacheValid = true;
        }
        const int total = int(findMatches.size());
        if (!total) { currentFindMatch = -1; return {}; }
        if (incremental) {
            currentFindMatch = 0;
            for (int i = 0; i < total; ++i) {
                const auto &match = findMatches.at(i);
                if (match.id == previous.id && match.link == previous.link) { currentFindMatch = i; break; }
            }
        } else if (changed || currentFindMatch < 0) {
            currentFindMatch = backward ? total - 1 : 0;
        } else {
            currentFindMatch = (currentFindMatch + (backward ? total - 1 : 1)) % total;
        }
        const auto target = findMatches.at(currentFindMatch);
        const bool revealed = revealTarget(target);
        if (!guard || revision != documentRevision || !findCacheValid) return {};
        if (!revealed) currentFindMatch = -1;
        return {int(findMatches.size()), currentFindMatch};
    } catch (const std::exception &e) {
        findCacheValid = false;
        currentFindMatch = -1;
        fail(QString::fromUtf8(e.what()));
        return {};
    }
}
void MindMapController::clearFind() {
    findQuery.clear();
    findMatches.clear();
    currentFindMatch = -1;
    findCacheValid = false;
}
QString MindMapController::selectedText() {
    try {
        if (!selectedLink.isEmpty()) {
            char *raw = nullptr;
            const auto result = m3_mindmap_get_link_json(model.get(), selectedLink.toUtf8().constData(), &raw);
            Text text(raw, m3_string_free);
            requireStatus(result);
            return string(Json::parse(text.get()).at("topic"));
        }
        QStringList topics;
        topics.reserve(selectedNodes.size());
        for (const auto &id : selectedNodes) {
            char *raw = nullptr;
            const auto result = m3_mindmap_get_node_json(model.get(), id.toUtf8().constData(), &raw);
            Text text(raw, m3_string_free);
            requireStatus(result);
            topics.append(string(Json::parse(text.get()).at("topic")));
        }
        return topics.join(QLatin1Char('\n'));
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
QByteArray MindMapController::copySelectedNodes() {
    if (selectedNodes.isEmpty()) return {};
    try {
        auto data = Json::parse(currentSnapshot.constData(), currentSnapshot.constData() + currentSnapshot.size());
        std::unordered_set<std::string> selected, included;
        for (const auto &id : selectedNodes) selected.insert(utf8(id));
        Json roots = Json::array(), nodes = Json::array(), links = Json::array();
        size_t pending = 0;
        // Canonical preorder makes selected ancestor/descendant overlaps one subtree.
        for (auto &node : data.at("nodes")) {
            const auto &id = node.at("id").get_ref<const std::string &>();
            if (pending == 0) {
                if (!selected.count(id)) continue;
                roots.push_back(id);
                pending = 1;
            }
            --pending;
            pending += node.at("children").size();
            included.insert(id);
            nodes.push_back(std::move(node));
        }
        if (roots.empty()) return {};
        for (auto &link : data.at("crossLinks")) {
            if (included.count(link.at("source").get_ref<const std::string &>()) &&
                included.count(link.at("target").get_ref<const std::string &>()))
                links.push_back(std::move(link));
        }
        Json result{{"schemaVersion", 1}, {"rootId", std::move(roots.front())},
                    {"nodes", std::move(nodes)}, {"crossLinks", std::move(links)}};
        if (roots.size() > 1) {
            roots.erase(roots.begin());
            result["orphanIds"] = std::move(roots);
        }
        return QByteArray::fromStdString(result.dump());
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
bool MindMapController::pasteNodes(const QString &parent, const QByteArray &json) {
    if (!writable() || !strings({parent})) return false;
    if (json.contains('\0')) return fail(tr("JSON cannot contain NUL bytes"));
    try {
        M3Mindmap *raw = nullptr;
        const auto parsed = m3_mindmap_from_json(json.constData(), &raw);
        Map copied(raw, m3_mindmap_destroy);
        requireStatus(parsed);
        const auto bytes = snapshot(copied.get());
        auto source = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        auto destination = Json::parse(currentSnapshot.constData(), currentSnapshot.constData() + currentSnapshot.size());
        auto &nodes = destination.at("nodes");
        const auto parentId = utf8(parent);
        const auto target = std::find_if(nodes.begin(), nodes.end(), [&](const Json &node) {
            return node.at("id") == parentId;
        });
        if (target == nodes.end()) return fail(tr("Node not found"));
        std::unordered_map<std::string, std::string> ids;
        ids.reserve(source.at("nodes").size());
        for (const auto &node : source.at("nodes"))
            ids.emplace(node.at("id").get<std::string>(), utf8(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        const QString first = QString::fromStdString(ids.at(source.at("rootId").get_ref<const std::string &>()));
        // Attach roots before growing nodes, which may invalidate the target iterator.
        target->at("children").push_back(utf8(first));
        if (const auto orphans = source.find("orphanIds"); orphans != source.end())
            for (const auto &root : *orphans) target->at("children").push_back(ids.at(root.get_ref<const std::string &>()));
        for (auto &node : source.at("nodes")) {
            node["id"] = ids.at(node.at("id").get_ref<const std::string &>());
            for (auto &child : node.at("children")) child = ids.at(child.get_ref<const std::string &>());
            nodes.push_back(std::move(node));
        }
        for (auto &link : source.at("crossLinks")) {
            link["id"] = utf8(QUuid::createUuid().toString(QUuid::WithoutBraces));
            link["source"] = ids.at(link.at("source").get_ref<const std::string &>());
            link["target"] = ids.at(link.at("target").get_ref<const std::string &>());
            destination.at("crossLinks").push_back(std::move(link));
        }
        const auto merged = destination.dump();
        raw = nullptr;
        const auto mergedStatus = m3_mindmap_from_json(merged.c_str(), &raw);
        Map candidate(raw, m3_mindmap_destroy);
        requireStatus(mergedStatus);
        const QPointer<MindMapController> guard(this);
        const auto revision = documentRevision + 1;
        const bool committed = commitChange(std::move(candidate), first);
        // Do not change folds on failure or touch a replacement document from a host callback.
        if (committed && guard && documentRevision == revision && !visibleNodes.contains(first)) revealNode(first);
        return committed;
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
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
QString MindMapController::toHtml(MindMapEditor::HtmlExportMode mode) {
    try {
        const auto document = currentSnapshot;
        HtmlMap map;
        auto presentation = prepare(document, direction, collapsedNodes, true, 0, &map);
        view.renderHtmlMap(std::move(presentation), map);
        return encodeHtml(document, map, mode == MindMapEditor::HtmlExportMode::Interactive);
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
Presentation MindMapController::prepare(const QByteArray &json,
                                       MindMapEditor::LayoutDirection requested, QSet<QString> &collapsed,
                                       bool useImageCache, int initialLevels, HtmlMap *html) {
    const auto data = Json::parse(json.constData(), json.constData() + json.size());
    const auto &nodes = data.at("nodes");
    std::unordered_map<std::string, const Json *> index;
    index.reserve(nodes.size());
    for (const auto &n : nodes) index.emplace(n.at("id").get<std::string>(), &n);
    for (auto it = collapsed.begin(); it != collapsed.end();) {
        if (index.find(utf8(*it)) == index.end()) it = collapsed.erase(it);
        else ++it;
    }
    Presentation result;
    result.outline = requested == MindMapEditor::LayoutDirection::Outline;
    // Reserve the full tree's geometry. Folding only changes visibility, never
    // branch balancing or sibling positions, including documents loaded folded.
    std::vector<M3LayoutNode> sizes;
    sizes.reserve(nodes.size());
    std::vector<size_t> visibleIndexes;
    struct Frame {
        const Json *record;
        QColor inheritedBranchColor;
        size_t parent, visibleParent;
        int level;
        bool visible;
    };
    std::vector<Frame> pending;
    if (const auto orphans = data.find("orphanIds"); orphans != data.end()) {
        for (auto it = orphans->rbegin(); it != orphans->rend(); ++it)
            pending.push_back({index.at(it->get<std::string>()), {},
                               M3_NO_PARENT, M3_NO_PARENT, 1, true});
    }
    pending.push_back({index.at(data.at("rootId").get<std::string>()), {},
                       M3_NO_PARENT, M3_NO_PARENT, 1, true});
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
        if (html && node.image && !node.image->url.isEmpty())
            html->articleImages.insert(node.id, {node.imagePixels, node.imageFailed});
        for (const auto &icon : record.at("icons")) node.icons.append(string(icon));
        node.tags.reserve(record.at("tags").size());
        for (const auto &tag : record.at("tags")) {
            auto value = string(tag);
            if (!value.trimmed().isEmpty()) node.tags.push_back({std::move(value), {}, {}});
        }
        node.root = record.at("id") == data.at("rootId");
        node.treeRoot = frame.parent == M3_NO_PARENT;
        if (initialLevels > 0 && frame.level >= initialLevels) collapsed.insert(node.id);
        node.expanded = !collapsed.contains(node.id);
        const auto &children = record.at("children");
        node.hasChildren = children.empty() == false;
        node.style = nodeStyle(record.at("style"));
        if (!node.style.branchColor.isValid()) node.style.branchColor = frame.inheritedBranchColor;
        view.prepare(node);
        const size_t position = sizes.size();
        sizes.push_back({frame.parent, node.rectangle.width(), node.rectangle.height()});
        // Preorder follows declared children, not the flat JSON record order.
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            pending.push_back({index.at(it->get<std::string>()), node.style.branchColor,
                               position, result.nodes.size(), frame.level + 1, frame.visible && node.expanded});
        if (frame.visible) {
            if (frame.visibleParent != M3_NO_PARENT)
                result.treeEdges.push_back({result.nodes[frame.visibleParent].id, node.id});
            visibleIndexes.push_back(position);
            result.nodes.push_back(std::move(node));
        }
    }
    const M3LayoutOptions options{coreDirection(requested), result.outline ? 32.0 : 64.0,
                                  result.outline ? 8.0 : 20.0};
    M3LayoutResult *raw = nullptr;
    const auto status = m3_layout_tree(sizes.data(), sizes.size(), &options, &raw);
    const std::unique_ptr<M3LayoutResult, decltype(&m3_layout_result_free)> layout(raw, m3_layout_result_free);
    requireStatus(status);
    QSet<QString> visible;
    for (size_t i = 0; i < result.nodes.size(); ++i) {
        auto &node = result.nodes[i];
        const auto &rect = layout->rects[visibleIndexes[i]];
        node.rectangle = QRectF(rect.x, rect.y, rect.width, rect.height);
        result.bounds = result.bounds.united(node.rectangle);
        visible.insert(node.id);
    }
    for (const auto &link : data.at("crossLinks")) {
        if (visible.contains(string(link.at("source"))) && visible.contains(string(link.at("target"))))
            result.links.push_back(linkPresentation(link));
    }
    return result;
}
void MindMapController::install(Presentation presentation, bool resetCamera) {
    QSet<QString> nodes, links;
    for (const auto &n : presentation.nodes) nodes.insert(n.id);
    for (const auto &l : presentation.links) links.insert(l.id);
    const QPointer<MindMapController> guard(this);
    const auto revision = documentRevision;
    view.install(std::move(presentation), resetCamera);
    if (!guard) return;
    if (revision != documentRevision) {
        // A host may replace the document while installation cancels a draft.
        // The nested document owns the model/history; rebuild its scene too.
        refreshAppearance();
        return;
    }
    visibleNodes.swap(nodes);
    visibleLinks.swap(links);
    ++imageGeneration; // Also stop request delivery across a reentrant visual/semantic rebuild.
    scheduleImageRequests();
}
void MindMapController::selection(QStringList nodes, QString link, bool ensureVisible) {
    const bool modified = nodes != selectedNodes || link != selectedLink;
    selectedNodes = nodes;
    selectedLink = link;
    if (!history.empty()) {
        history[historyCursor].nodes = nodes;
        history[historyCursor].link = link;
    }
    const QPointer<MindMapController> guard(this);
    // Reapply even unchanged membership after a scene rebuild.
    view.setSelection(nodes, link);
    if (!guard || nodes != selectedNodes || link != selectedLink) return;
    if (ensureVisible && nodes.size() == 1) view.ensureNodeVisible(nodes.front());
    if (!guard || nodes != selectedNodes || link != selectedLink) return;
    if (modified) emit selectionChanged(nodes.size() == 1 ? nodes.front() : QString(), link);
}
void MindMapController::restoreSelection() {
    auto nodes = selectedNodes;
    for (qsizetype i = nodes.size(); i > 0; --i)
        if (!visibleNodes.contains(nodes.at(i - 1))) nodes.removeAt(i - 1);
    selection(std::move(nodes), visibleLinks.contains(selectedLink) ? selectedLink : QString(), false);
}
bool MindMapController::replace(Map candidate) {
    try {
        auto bytes = snapshot(candidate.get());
        QSet<QString> collapsed;
        auto presentation = prepare(bytes, direction, collapsed, false, initialVisibleLevels);
        const QString root = presentation.nodes.front().id;
        std::vector<HistoryState> baseline;
        baseline.push_back({bytes, {root}, {}});
        // All fallible semantic/presentation preparation and baseline allocation
        // precede publication. A later scene failure cannot roll this load back.
        model.swap(candidate);
        currentSnapshot.swap(bytes);
        history.swap(baseline);
        historyCursor = 0;
        collapsedNodes.swap(collapsed);
        clearFind();
        ++documentRevision;
        imageResources.clear();
        ++imageGeneration;
        return finishChange(std::move(presentation), true);
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
void MindMapController::notifyHistoryAvailability() {
    const QPointer<MindMapController> guard(this);
    const bool undo = canUndo();
    if (undo != undoWasAvailable) {
        undoWasAvailable = undo;
        emit undoAvailable(undo);
    }
    if (!guard) return;
    // Recompute after the first signal: a synchronous host may edit/load/toggle
    // read-only or destroy the editor instead of merely observing availability.
    const bool redo = canRedo();
    if (redo != redoWasAvailable) {
        redoWasAvailable = redo;
        emit redoAvailable(redo);
    }
}
void MindMapController::trimHistory() {
    if (!undoLimit) return;
    const size_t capacity = undoLimit + size_t(1);
    if (history.size() <= capacity) return;
    const size_t past = std::min(historyCursor, history.size() - capacity);
    history.erase(history.begin(), history.begin() + past);
    historyCursor -= past;
    // Keep the current snapshot even when most of the excess is redo history.
    if (history.size() > capacity) history.resize(capacity);
}
void MindMapController::setUndoLimit(int limit) {
    const size_t normalized = size_t(std::max(0, limit));
    if (undoLimit == normalized) return;
    undoLimit = normalized;
    trimHistory();
    notifyHistoryAvailability();
}
template<typename Operation>
bool MindMapController::transact(Operation &&operation,
                                 const QString &preferredNode, const QString &preferredLink) {
    if (!writable()) return false;
    try {
        M3Mindmap *raw = nullptr;
        const auto parsed = m3_mindmap_from_json(currentSnapshot.constData(), &raw);
        Map candidate(raw, m3_mindmap_destroy);
        requireStatus(parsed);
        requireStatus(operation(candidate.get()));
        return commitChange(std::move(candidate), preferredNode, preferredLink);
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::commitChange(Map candidate, const QString &preferredNode, const QString &preferredLink) {
    auto bytes = snapshot(candidate.get());
    if (bytes == currentSnapshot) { success(); return true; }
    HistoryState next{bytes, selectedNodes, selectedLink};
    const size_t required = historyCursor + 2;
    if (required > history.capacity())
        history.reserve(std::max(required, history.capacity() * 2));
    // No fallible work remains before publication. Reserve before truncating
    // redo; a failed command must retain every prior state and selection.
    model.swap(candidate);
    currentSnapshot.swap(bytes);
    history.resize(historyCursor + 1);
    history.push_back(std::move(next));
    ++historyCursor;
    ++documentRevision;
    trimHistory();
    return finishChange({}, false, false, preferredNode, preferredLink);
}
bool MindMapController::finishChange(std::optional<Presentation> prepared, bool resetCamera, bool restoring,
                                    const QString &preferredNode, const QString &preferredLink) {
    const QPointer<MindMapController> guard(this);
    const auto revision = documentRevision;
    auto nodes = history[historyCursor].nodes;
    auto link = history[historyCursor].link;
    const bool wasRestoring = restoringHistory;
    restoringHistory = restoringHistory || restoring;
    findCacheValid = false;
    try {
        success();
        if (!guard) return true;
        if (revision == documentRevision) {
            auto presentation = prepared ? std::move(*prepared) : prepare(currentSnapshot, direction, collapsedNodes);
            const QString root = presentation.nodes.front().id;
            install(std::move(presentation), resetCamera);
            if (!guard) return true;
            if (revision == documentRevision) {
                if (visibleNodes.contains(preferredNode)) { nodes = QStringList{preferredNode}; link.clear(); }
                else if (visibleLinks.contains(preferredLink)) { nodes.clear(); link = preferredLink; }
                for (qsizetype i = nodes.size(); i > 0; --i)
                    if (!visibleNodes.contains(nodes.at(i - 1))) nodes.removeAt(i - 1);
                if (!visibleLinks.contains(link)) link.clear();
                if (restoring && nodes.isEmpty() && link.isEmpty()) nodes = QStringList{root};
                selection(std::move(nodes), std::move(link), !restoring && !resetCamera && !preferredNode.isEmpty());
            }
        }
    } catch (const std::exception &e) {
        if (!guard) return true;
        if (revision == documentRevision) {
            visibleNodes.clear(); visibleLinks.clear();
            view.showError(QString::fromUtf8(e.what()));
            if (!guard) return true;
            if (revision == documentRevision) selection({}, {}, false);
        }
        if (!guard) return true;
        fail(QString::fromUtf8(e.what()));
    }
    if (!guard) return true;
    notifyHistoryAvailability();
    if (!guard) return true;
    emit documentChanged();
    if (guard) restoringHistory = wasRestoring;
    return true;
}
bool MindMapController::restoreHistory(size_t cursor) {
    try {
        const auto target = history[cursor];
        M3Mindmap *raw = nullptr;
        const auto parsed = m3_mindmap_from_json(target.json.constData(), &raw);
        Map candidate(raw, m3_mindmap_destroy);
        requireStatus(parsed);
        auto collapsed = collapsedNodes;
        auto presentation = prepare(target.json, direction, collapsed);
        model.swap(candidate);
        currentSnapshot = target.json;
        historyCursor = cursor;
        collapsedNodes.swap(collapsed);
        ++documentRevision;
        return finishChange(std::move(presentation), false, true);
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::undo() {
    if (readOnly) return false;
    const QPointer<MindMapController> guard(this);
    if (!view.finishTopicEdit(true) || !guard || !canUndo()) return false;
    return restoreHistory(historyCursor - 1);
}
bool MindMapController::redo() {
    if (readOnly) return false;
    const QPointer<MindMapController> guard(this);
    if (!view.finishTopicEdit(true) || !guard || !canRedo()) return false;
    return restoreHistory(historyCursor + 1);
}
QString MindMapController::addNode(const QString &parent, const QString &topic, int index) {
    if (!writable()) return {};
    if (!strings({parent, topic})) return {};
    if (index < -1) { fail(tr("Invalid insertion index")); return {}; }
    try {
        NodeInsertion insertion(currentSnapshot, parent, autoRandomBranchColor);
        Json record{{"topic", utf8(topic)}};
        const QString id = insertion.prepare(record);
        const auto serialized = record.dump();
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_insert_node(candidate, insertion.parentId.c_str(),
                index == -1 ? M3_APPEND : size_t(index), serialized.c_str());
        }, id) ? id : QString();
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
QStringList MindMapController::addNodes(const QString &parent, const QByteArray &nodesJson, int index) {
    if (!writable() || !strings({parent})) return {};
    if (index < -1) { fail(tr("Invalid insertion index")); return {}; }
    try {
        // Do not normalize duplicate members away before the C record validator.
        std::vector<std::unordered_set<std::string>> keys;
        auto records = Json::parse(nodesJson.constData(), nodesJson.constData() + nodesJson.size(),
            [&keys](int, Json::parse_event_t event, Json &value) {
                if (event == Json::parse_event_t::object_start) keys.emplace_back();
                else if (event == Json::parse_event_t::object_end) keys.pop_back();
                else if (event == Json::parse_event_t::key &&
                         !keys.back().insert(value.get<std::string>()).second)
                    throw std::runtime_error("Duplicate node property");
                return true;
            });
        if (!records.is_array() || records.empty()) {
            fail(tr("Expected a nonempty node array")); return {};
        }
        for (const auto &record : records) {
            if (!record.is_object() || !record.contains("topic") || !record.at("topic").is_string() ||
                record.contains("id") || record.contains("children") || record.contains("expanded")) {
                fail(tr("Invalid node insertion properties")); return {};
            }
        }
        NodeInsertion insertion(currentSnapshot, parent, autoRandomBranchColor);
        QStringList ids;
        ids.reserve(qsizetype(records.size()));
        const bool inserted = transact([&](M3Mindmap *candidate) {
            size_t position = index == -1 ? M3_APPEND : size_t(index);
            for (auto &record : records) {
                ids.append(insertion.prepare(record));
                const auto serialized = record.dump();
                const auto result = m3_mindmap_insert_node(candidate, insertion.parentId.c_str(),
                    position, serialized.c_str());
                if (result != M3_OK) return result;
                if (index != -1) ++position;
            }
            return M3_OK;
        });
        if (!inserted) return {};
        return ids;
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
bool MindMapController::duplicateNode(const QString &id) {
    if (!writable() || !strings({id})) return false;
    try {
        auto data = Json::parse(currentSnapshot.constData(), currentSnapshot.constData() + currentSnapshot.size());
        auto &nodes = data.at("nodes");
        const auto key = utf8(id);
        const auto source = std::find_if(nodes.begin(), nodes.end(),
            [&](const Json &node) { return node.at("id") == key; });
        if (source == nodes.end()) return fail(tr("Node not found"));
        std::string parent;
        size_t position = 0;
        if (data.at("rootId") != key) {
            for (const auto &node : nodes) {
                const auto &children = node.at("children");
                const auto child = std::find(children.begin(), children.end(), key);
                if (child == children.end()) continue;
                parent = node.at("id").get<std::string>();
                position = size_t(std::distance(children.begin(), child)) + 1;
                break;
            }
            if (parent.empty()) {
                const auto &orphans = data.at("orphanIds");
                position = size_t(std::distance(orphans.begin(), std::find(orphans.begin(), orphans.end(), key))) + 1;
            }
        }
        const QString created = QUuid::createUuid().toString(QUuid::WithoutBraces);
        std::unordered_map<std::string, std::string> ids;
        struct Frame { const std::string *parent; size_t remaining; };
        std::vector<Frame> pending{{&parent, 1}};
        return transact([&](M3Mindmap *candidate) {
            bool first = true;
            // Canonical snapshots are preorder; child counts bound this subtree.
            for (auto node = source; !pending.empty(); ++node) {
                auto &record = *node;
                const auto children = record.at("children").size();
                const QString nextId = first ? created : QUuid::createUuid().toString(QUuid::WithoutBraces);
                const auto &newId = ids.emplace(record.at("id").get<std::string>(), utf8(nextId)).first->second;
                record["id"] = newId;
                record["children"] = Json::array();
                const auto serialized = record.dump();
                requireStatus(m3_mindmap_insert_node(candidate, pending.back().parent->c_str(),
                    first ? position : M3_APPEND, serialized.c_str()));
                if (--pending.back().remaining == 0) pending.pop_back();
                // References to owned unordered_map values survive rehashing.
                if (children) pending.push_back({&newId, children});
                first = false;
            }
            for (auto &link : data.at("crossLinks")) {
                const auto from = ids.find(link.at("source").get_ref<const std::string &>());
                const auto to = ids.find(link.at("target").get_ref<const std::string &>());
                if (from == ids.end() || to == ids.end()) continue;
                link["id"] = utf8(QUuid::createUuid().toString(QUuid::WithoutBraces));
                link["source"] = from->second;
                link["target"] = to->second;
                const auto serialized = link.dump();
                requireStatus(m3_mindmap_add_link(candidate, serialized.c_str()));
            }
            return M3_OK;
        }, created);
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::renameNode(const QString &id, const QString &topic) {
    if (!writable()) return false;
    if (!strings({id, topic})) return false;
    try {
        const auto patch = Json{{"topic", utf8(topic)}}.dump();
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_update_node(candidate, id.toUtf8().constData(), patch.c_str());
        });
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::commitTopicEdit(const QString &id, const QString &draft, const QString &previousDraft) {
    if (!writable()) return false;
    if (!strings({id, draft, previousDraft})) return false;
    try {
        const auto parsed = parseTopicEdit(draft);
        const auto previous = parseTopicEdit(previousDraft);
        Json patch{{"topic", utf8(parsed.topic)}};
        if (!parsed.tags.isEmpty() || !previous.tags.isEmpty()) {
            char *raw = nullptr;
            const auto read = m3_mindmap_get_node_json(model.get(), id.toUtf8().constData(), &raw);
            Text text(raw, m3_string_free);
            requireStatus(read);
            const auto current = Json::parse(text.get());
            auto &tags = patch["tags"] = current.at("tags");
            // Replace only the last accepted draft's exact trailing contribution.
            // Older duplicates and independently changed tags belong to the host.
            if (!previous.tags.isEmpty() && tags.size() >= size_t(previous.tags.size())) {
                const auto suffix = tags.end() - previous.tags.size();
                if (std::equal(previous.tags.cbegin(), previous.tags.cend(), suffix,
                    [](const QString &tag, const Json &value) { return value == utf8(tag); }))
                    tags.erase(suffix, tags.end());
            }
            for (const auto &tag : parsed.tags) tags.push_back(utf8(tag));
        }
        return updateNodeProperties(id, QByteArray::fromStdString(patch.dump()));
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::updateNodeProperties(const QString &id, const QByteArray &patch) {
    if (!writable()) return false;
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
        const auto serialized = update.dump();
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_update_node(candidate, id.toUtf8().constData(), serialized.c_str());
        });
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::removeNode(const QString &id) {
    if (!writable()) return false;
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
    return transact([&](M3Mindmap *candidate) {
        return m3_mindmap_remove_subtree(candidate, id.toUtf8().constData());
    }, fallback);
}
bool MindMapController::removeSelectedNodes() {
    if (!writable()) return false;
    if (selectedNodes.isEmpty()) return fail(tr("No nodes are selected"));
    if (selectedNodes.size() == 1) return removeNode(selectedNodeId());
    try {
        const auto &bytes = currentSnapshot;
        const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        const QString root = string(data.at("rootId"));
        QHash<QString, QString> parents;
        for (const auto &node : data.at("nodes")) parents.insert(string(node.at("id")), {});
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
        return transact([&](M3Mindmap *candidate) {
            for (const auto &id : roots)
                requireStatus(m3_mindmap_remove_subtree(candidate, id.toUtf8().constData()));
            return M3_OK;
        }, fallback);
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::moveNode(const QString &id, const QString &parent, int index) {
    if (!writable()) return false;
    if (!strings({id, parent})) return false;
    if (index < -1) return fail(tr("Invalid insertion index"));
    return transact([&](M3Mindmap *candidate) {
        return m3_mindmap_move_node(candidate, id.toUtf8().constData(), parent.toUtf8().constData(),
                                  index == -1 ? M3_APPEND : size_t(index));
    });
}
bool MindMapController::setExpanded(const QString &id, bool expanded, bool recursive) {
    if (!strings({id})) return false;
    const QPointer<MindMapController> guard(this);
    const auto revision = documentRevision;
    try {
        const auto nodes = choices();
        if (!guard || revision != documentRevision) return false;
        const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const NodeChoice &n) { return n.id == id; });
        if (node == nodes.end()) return fail(tr("Node not found"));
        if (!recursive && node->expanded == expanded) { success(); return true; }
        auto collapsed = collapsedNodes;
        bool changed = false;
        // choices() preserves the native snapshot's full-forest preorder. Count
        // pending descendants to stop before siblings or the next orphan tree.
        size_t pending = 1;
        for (auto current = node; pending != 0; ++current) {
            if (current->expanded != expanded) {
                if (expanded) collapsed.remove(current->id);
                else collapsed.insert(current->id);
                changed = true;
            }
            --pending;
            if (recursive) pending += size_t(current->children.size());
        }
        if (!changed) { success(); return true; }
        QString fallback;
        if (!expanded && selectedNodes.size() == 1) {
            QHash<QString, QString> parents;
            for (const auto &n : nodes) parents.insert(n.id, n.parent);
            for (QString current = selectedNodeId(); !current.isEmpty(); current = parents.value(current))
                if (current == id) { fallback = id; break; }
        }
        auto presentation = prepare(currentSnapshot, direction, collapsed);
        collapsedNodes.swap(collapsed);
        install(std::move(presentation), false);
        if (!guard || revision != documentRevision) return true;
        if (visibleNodes.contains(fallback)) selection({fallback}, {});
        else restoreSelection();
        if (guard && revision == documentRevision) success();
        return true;
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
QString MindMapController::addLink(const QString &source, const QString &target, bool directed, const QString &topic) {
    if (!writable()) return {};
    if (!strings({source, target, topic})) return {};
    try {
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto record = Json{{"id", utf8(id)}, {"source", utf8(source)}, {"target", utf8(target)},
                                 {"directed", directed}, {"topic", utf8(topic)}}.dump();
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_add_link(candidate, record.c_str());
        }, {}, id) ? id : QString();
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); return {}; }
}
bool MindMapController::updateLink(const QString &id, const QString &source, const QString &target, bool directed, const QString &topic) {
    if (!writable()) return false;
    if (!strings({id, source, target, topic})) return false;
    try {
        const auto patch = Json{{"source", utf8(source)}, {"target", utf8(target)},
                                {"directed", directed}, {"topic", utf8(topic)}}.dump();
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_update_link(candidate, id.toUtf8().constData(), patch.c_str());
        });
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::setLinkDirection(const QString &id, LinkDirection linkDirection) {
    if (!writable()) return false;
    if (!strings({id})) return false;
    try {
        char *raw = nullptr;
        const auto read = m3_mindmap_get_link_json(model.get(), id.toUtf8().constData(), &raw);
        Text text(raw, m3_string_free);
        requireStatus(read);
        auto current = Json::parse(text.get());
        if (linkPresentation(current).direction == linkDirection) { success(); return true; }
        auto style = std::move(current.at("style"));
        switch (linkDirection) {
        case LinkDirection::None:
        case LinkDirection::Forward: style.erase("arrowDirection"); break;
        case LinkDirection::Backward: style["arrowDirection"] = "backward"; break;
        case LinkDirection::Both: style["arrowDirection"] = "both"; break;
        default: return fail(tr("Invalid link direction"));
        }
        const auto patch = Json{{"directed", linkDirection != LinkDirection::None}, {"style", std::move(style)}}.dump();
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_update_link(candidate, id.toUtf8().constData(), patch.c_str());
        });
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::reconnectLink(const QString &id, bool source, const QString &original, const QString &node) {
    if (!writable()) return false;
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
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_update_link(candidate, id.toUtf8().constData(), patch.c_str());
        });
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::commitLinkTopicEdit(const QString &id, const QString &topic) {
    if (!writable()) return false;
    if (!strings({id, topic})) return false;
    try {
        const auto patch = Json{{"topic", utf8(topic)}}.dump();
        return transact([&](M3Mindmap *candidate) {
            return m3_mindmap_update_link(candidate, id.toUtf8().constData(), patch.c_str());
        });
    } catch (const std::exception &e) { return fail(QString::fromUtf8(e.what())); }
}
bool MindMapController::removeLink(const QString &id) {
    if (!writable()) return false;
    if (!strings({id})) return false;
    return transact([&](M3Mindmap *candidate) {
        return m3_mindmap_remove_link(candidate, id.toUtf8().constData());
    });
}
bool MindMapController::setLayoutDirection(MindMapEditor::LayoutDirection requested) {
    const QPointer<MindMapController> guard(this);
    try {
        auto presentation = prepare(currentSnapshot, requested, collapsedNodes);
        install(std::move(presentation), false);
        if (!guard) return true;
        const bool changed = direction != requested;
        direction = requested;
        if (changed) {
            emit layoutDirectionChanged(direction);
            if (!guard) return true;
        }
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
        const auto &bytes = currentSnapshot;
        const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        QHash<QString, QString> parents;
        for (const auto &n : data.at("nodes"))
            for (const auto &c : n.at("children")) parents.insert(string(c), string(n.at("id")));
        for (const auto &n : data.at("nodes")) {
            NodeChoice choice;
            choice.id = string(n.at("id")); choice.topic = string(n.at("topic"));
            choice.parent = parents.value(choice.id);
            choice.root = n.at("id") == data.at("rootId");
            choice.expanded = !collapsedNodes.contains(choice.id);
            for (const auto &c : n.at("children")) choice.children.append(string(c));
            result.push_back(std::move(choice));
        }
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    return result;
}
NodeProperties MindMapController::nodeProperties(const QString &id) {
    if (!model || id.isEmpty()) return {};
    try {
        const auto &bytes = currentSnapshot;
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
            const auto orphans = data.find("orphanIds");
            result.treeRoot = result.root ||
                (orphans != data.end() &&
                 std::find(orphans->begin(), orphans->end(), key) != orphans->end());
            result.style = nodeStyle(record.at("style"));
            return result;
        }
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    return {};
}
LinkPresentation MindMapController::linkChoice(const QString &id) {
    try {
        const auto &bytes = currentSnapshot;
        const auto data = Json::parse(bytes.constData(), bytes.constData() + bytes.size());
        for (const auto &l : data.at("crossLinks")) if (string(l.at("id")) == id) return linkPresentation(l);
    } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    return {};
}
void MindMapController::refreshAppearance() {
    if (!model) return;
    const QPointer<MindMapController> guard(this);
    try {
        install(prepare(currentSnapshot, direction, collapsedNodes), false);
        if (!guard) return;
        restoreSelection();
    } catch (const std::exception &e) {
        // A visual-only refresh failed before installation; retain the previous
        // scene and draft rather than discarding uncommitted user input.
        fail(QString::fromUtf8(e.what()));
    }
}
}
