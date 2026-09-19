#include "json_codec.h"
#include "layout.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
struct M3Mindmap { m3::Model model; };
namespace {
struct OwnedLayout : M3LayoutResult { std::vector<M3Rect> storage; };
thread_local char error_text[256] = {};
template<class F> M3Status boundary(F &&f) noexcept {
    error_text[0] = '\0';
    try { f(); return M3_OK; }
    catch (const m3::Failure &e) { std::snprintf(error_text, sizeof error_text, "%s", e.message); return e.status; }
    catch (const std::bad_alloc &) { std::snprintf(error_text, sizeof error_text, "Allocation failed"); return M3_ERR_OUT_OF_MEMORY; }
    catch (const m3::Json::parse_error &) { std::snprintf(error_text, sizeof error_text, "Malformed JSON"); return M3_ERR_JSON; }
    catch (const m3::Json::exception &) { std::snprintf(error_text, sizeof error_text, "Invalid JSON schema"); return M3_ERR_SCHEMA; }
    catch (...) { std::snprintf(error_text, sizeof error_text, "Unexpected internal failure"); return M3_ERR_INTERNAL; }
}
void argument(bool valid) { m3::require(valid, M3_ERR_INVALID_ARGUMENT, "Invalid argument"); }
void id_argument(const char *id) { argument(id && *id && m3::valid_utf8(id)); }
char *snapshot(const m3::Json &j) {
    auto s = j.dump();
    auto p = std::make_unique<char[]>(s.size() + 1);
    std::memcpy(p.get(), s.c_str(), s.size() + 1);
    return p.release();
}
}
extern "C" {
const char *m3_last_error(void) { return error_text; }
void m3_string_free(char *text) { delete[] text; }
void m3_mindmap_destroy(M3Mindmap *map) { delete map; }
M3Status m3_mindmap_create(const char *root_id, const char *topic, M3Mindmap **out_map) {
    if (out_map) *out_map = nullptr;
    return boundary([&] {
        argument(out_map && m3::valid_utf8(topic)); id_argument(root_id);
        auto map = std::make_unique<M3Mindmap>();
        map->model.root = root_id;
        m3::Node root; root.id = root_id; root.attrs.topic = topic;
        map->model.nodes.emplace(root_id, std::move(root));
        *out_map = map.release();
    });
}
M3Status m3_mindmap_from_json(const char *json, M3Mindmap **out_map) {
    if (out_map) *out_map = nullptr;
    return boundary([&] {
        argument(out_map && json);
        auto map = std::make_unique<M3Mindmap>();
        map->model = m3::decode_document(m3::parse(json));
        *out_map = map.release();
    });
}
M3Status m3_mindmap_to_json(const M3Mindmap *map, char **out_json) {
    if (out_json) *out_json = nullptr;
    return boundary([&] { argument(map && out_json); *out_json = snapshot(m3::encode_document(map->model)); });
}
M3Status m3_mindmap_get_node_json(const M3Mindmap *map, const char *id, char **out_json) {
    if (out_json) *out_json = nullptr;
    return boundary([&] { argument(map && out_json); id_argument(id); *out_json = snapshot(m3::encode_node(m3::get_node(map->model, id))); });
}
M3Status m3_mindmap_get_link_json(const M3Mindmap *map, const char *id, char **out_json) {
    if (out_json) *out_json = nullptr;
    return boundary([&] { argument(map && out_json); id_argument(id); *out_json = snapshot(m3::encode_link(m3::get_link(map->model, id))); });
}
M3Status m3_mindmap_insert_node(M3Mindmap *map, const char *parent, size_t index, const char *json) {
    return boundary([&] { argument(map && json); id_argument(parent); m3::insert_node(map->model, parent, index, m3::decode_node(m3::parse(json))); });
}
M3Status m3_mindmap_update_node(M3Mindmap *map, const char *id, const char *json) {
    return boundary([&] { argument(map && json); id_argument(id); m3::update_node(map->model, id, m3::parse(json)); });
}
M3Status m3_mindmap_move_node(M3Mindmap *map, const char *id, const char *parent, size_t index) {
    return boundary([&] { argument(map); id_argument(id); id_argument(parent); m3::move_node(map->model, id, parent, index); });
}
M3Status m3_mindmap_remove_subtree(M3Mindmap *map, const char *id) {
    return boundary([&] { argument(map); id_argument(id); m3::remove_subtree(map->model, id); });
}
M3Status m3_mindmap_add_link(M3Mindmap *map, const char *json) {
    return boundary([&] { argument(map && json); m3::add_link(map->model, m3::decode_link(m3::parse(json))); });
}
M3Status m3_mindmap_update_link(M3Mindmap *map, const char *id, const char *json) {
    return boundary([&] { argument(map && json); id_argument(id); m3::update_link(map->model, id, m3::parse(json)); });
}
M3Status m3_mindmap_remove_link(M3Mindmap *map, const char *id) {
    return boundary([&] { argument(map); id_argument(id); m3::remove_link(map->model, id); });
}
M3Status m3_layout_tree(const M3LayoutNode *nodes, size_t count,
    const M3LayoutOptions *options, M3LayoutResult **out_result) {
    if (out_result) *out_result = nullptr;
    return boundary([&] {
        argument(out_result && options && (nodes || count == 0));
        auto geometry = m3::layout_tree(nodes, count, *options);
        auto result = std::make_unique<OwnedLayout>();
        result->storage = std::move(geometry.rects);
        result->node_count = result->storage.size();
        result->rects = result->storage.empty() ? nullptr : result->storage.data();
        result->bounds = geometry.bounds;
        *out_result = result.release();
    });
}
void m3_layout_result_free(M3LayoutResult *result) { delete static_cast<OwnedLayout *>(result); }
M3Status m3_mindmap_layout_json(const M3Mindmap *map, const M3NodeSize *sizes,
    size_t count, const M3LayoutOptions *options, char **out_json) {
    if (out_json) *out_json = nullptr;
    return boundary([&] {
        argument(map && out_json && options && (sizes || count == 0));
        *out_json = snapshot(m3::layout_model(map->model, sizes, count, *options));
    });
}
}
