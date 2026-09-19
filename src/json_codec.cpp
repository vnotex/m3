#include "json_codec.h"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <unordered_set>
namespace m3 {
namespace {
void schema(bool valid) { require(valid, M3_ERR_SCHEMA, "Invalid document schema"); }
void keys(const Json &j, std::initializer_list<std::string_view> allowed) {
    schema(j.is_object());
    for (auto it = j.begin(); it != j.end(); ++it)
        schema(std::find(allowed.begin(), allowed.end(), it.key()) != allowed.end());
}
std::string identifier(const Json &j) {
    auto id = j.get<std::string>();
    schema(!id.empty());
    return id;
}
}
Json parse(const char *text) {
    std::vector<std::unordered_set<std::string>> members;
    return Json::parse(text, [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start) members.emplace_back();
        if (event == Json::parse_event_t::key) {
            const auto &key = value.get_ref<const std::string &>();
            schema(key.find('\0') == std::string::npos);
            require(members.back().insert(key).second, M3_ERR_JSON, "Duplicate JSON member");
        }
        if (event == Json::parse_event_t::value && value.is_string())
            schema(value.get_ref<const std::string &>().find('\0') == std::string::npos);
        if (event == Json::parse_event_t::object_end) members.pop_back();
        return true;
    });
}
namespace {
void apply_attributes(Attributes &a, const Json &j) {
    if (j.contains("topic")) a.topic = j["topic"].get<std::string>();
    if (j.contains("note")) a.note = j["note"].get<std::string>();
    if (j.contains("hyperLink")) a.hyperlink = j["hyperLink"].get<std::string>();
    if (j.contains("expanded")) a.expanded = j["expanded"].get<bool>();
    if (j.contains("style")) { schema(j["style"].is_object()); a.style = j["style"]; }
    if (j.contains("tags")) a.tags = j["tags"].get<std::vector<std::string>>();
    if (j.contains("icons")) a.icons = j["icons"].get<std::vector<std::string>>();
    if (j.contains("image")) {
        const auto &i = j["image"];
        if (i.is_null()) a.image.reset();
        else {
            keys(i, {"url", "width", "height"});
            Image image{i.at("url").get<std::string>(), i.at("width").get<double>(), i.at("height").get<double>()};
            schema(std::isfinite(image.width) && image.width >= 0 && std::isfinite(image.height) && image.height >= 0);
            a.image = std::move(image);
        }
    }
}
void apply_link_fields(Link &l, const Json &j) {
    if (j.contains("source")) l.source = identifier(j["source"]);
    if (j.contains("target")) l.target = identifier(j["target"]);
    if (j.contains("directed")) l.directed = j["directed"].get<bool>();
    if (j.contains("topic")) l.topic = j["topic"].get<std::string>();
    if (j.contains("icon")) l.icon = j["icon"].get<std::string>();
    if (j.contains("style")) { schema(j["style"].is_object()); l.style = j["style"]; }
}
}
Node decode_node(const Json &j) {
    keys(j, {"id", "topic", "note", "hyperLink", "expanded", "style", "tags", "icons", "image", "children"});
    Node n;
    n.id = identifier(j.at("id"));
    apply_attributes(n.attrs, j);
    n.children = j.value("children", std::vector<std::string>{});
    return n;
}
Link decode_link(const Json &j) {
    keys(j, {"id", "source", "target", "directed", "topic", "icon", "style"});
    Link l;
    l.id = identifier(j.at("id"));
    schema(j.contains("source") && j.contains("target") && j.contains("directed"));
    apply_link_fields(l, j);
    return l;
}
Attributes patch_attributes(const Attributes &original, const Json &patch) {
    keys(patch, {"topic", "note", "hyperLink", "expanded", "style", "tags", "icons", "image"});
    Attributes result = original;
    apply_attributes(result, patch);
    return result;
}
Link patch_link(const Link &original, const Json &patch) {
    keys(patch, {"source", "target", "directed", "topic", "icon", "style"});
    Link result = original;
    apply_link_fields(result, patch);
    return result;
}
Model decode_document(const Json &j) {
    keys(j, {"schemaVersion", "rootId", "nodes", "crossLinks"});
    schema(j.at("schemaVersion").is_number_integer() && j["schemaVersion"] == 1);
    Model m;
    m.root = identifier(j.at("rootId"));
    schema(j.at("nodes").is_array() && !j["nodes"].empty());
    m.nodes.reserve(j["nodes"].size());
    for (const auto &v : j["nodes"]) {
        auto n = decode_node(v);
        auto id = n.id;
        schema(m.nodes.emplace(std::move(id), std::move(n)).second);
    }
    schema(m.nodes.count(m.root) == 1);
    for (auto &entry : m.nodes) for (const auto &id : entry.second.children) {
        auto child = m.nodes.find(id);
        schema(child != m.nodes.end());
        schema(id != m.root && id != entry.first && child->second.parent.empty());
        child->second.parent = entry.first;
    }
    for (const auto &entry : m.nodes)
        schema(entry.first == m.root || !entry.second.parent.empty());
    // Parent uniqueness prevents any reachable cycle. Disconnected cycles are
    // rejected by the root traversal's count, without recursion.
    schema(preorder(m).size() == m.nodes.size());
    if (j.contains("crossLinks")) {
        schema(j["crossLinks"].is_array());
        for (const auto &v : j["crossLinks"]) {
            auto l = decode_link(v);
            schema(m.nodes.count(l.source) && m.nodes.count(l.target));
            auto id = l.id;
            schema(m.links.emplace(std::move(id), std::move(l)).second);
        }
    }
    return m;
}
Json encode_node(const Node &n) {
    const auto &a = n.attrs;
    Json image = nullptr;
    if (a.image) image = {{"url", a.image->url}, {"width", a.image->width}, {"height", a.image->height}};
    return {{"id", n.id}, {"topic", a.topic}, {"style", a.style}, {"expanded", a.expanded},
        {"tags", a.tags}, {"icons", a.icons}, {"hyperLink", a.hyperlink}, {"image", image},
        {"note", a.note}, {"children", n.children}};
}
Json encode_link(const Link &l) {
    return {{"id", l.id}, {"source", l.source}, {"target", l.target}, {"directed", l.directed},
        {"topic", l.topic}, {"icon", l.icon}, {"style", l.style}};
}
Json encode_document(const Model &m) {
    Json nodes = Json::array(), links = Json::array();
    for (const auto *n : preorder(m)) nodes.push_back(encode_node(*n));
    for (const auto *l : sorted_links(m)) links.push_back(encode_link(*l));
    return {{"schemaVersion", 1}, {"rootId", m.root}, {"nodes", std::move(nodes)}, {"crossLinks", std::move(links)}};
}
}
