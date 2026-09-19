#ifndef M3_TEST_SUPPORT_H
#define M3_TEST_SUPPORT_H
#include "m3/m3.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <stdexcept>
using Json = nlohmann::json;
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)
inline void ok(M3Status status) {
    if (status != M3_OK) throw std::runtime_error("API status " + std::to_string(status) + ": " + m3_last_error());
}
using Map = std::unique_ptr<M3Mindmap, decltype(&m3_mindmap_destroy)>;
using Text = std::unique_ptr<char, decltype(&m3_string_free)>;
inline Map load(const Json &j) {
    M3Mindmap *p = nullptr;
    ok(m3_mindmap_from_json(j.dump().c_str(), &p));
    return Map(p, m3_mindmap_destroy);
}
inline Json document(const Map &map) {
    char *p = nullptr;
    ok(m3_mindmap_to_json(map.get(), &p));
    Text text(p, m3_string_free);
    return Json::parse(text.get());
}
inline Json fixture() {
    return Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[
      {"id":"r","topic":"Racine 世界 🌍","note":"Note café","tags":["x","y","x"],
       "icons":["star","flag"],"hyperLink":"opaque:anything","image":{"url":"memory:絵","width":0,"height":12},
       "style":{"nested":{"list":[1,true,"é"]}},"children":["a","b","c"]},
      {"id":"c"},{"id":"d"},{"id":"b"},{"id":"a","children":["d"]}],
      "crossLinks":[
       {"id":"l4","source":"b","target":"c","directed":true},
       {"id":"l2","source":"d","target":"c","directed":false,"topic":"Other","icon":"pin","style":{"dash":[2,3]}},
       {"id":"l1","source":"a","target":"b","directed":true,"topic":"Related","icon":"reference","style":{"color":"#336699","width":2}},
       {"id":"l3","source":"a","target":"a","directed":false}]})");
}
inline Json node(const Map &map, const char *id) {
    char *p = nullptr;
    ok(m3_mindmap_get_node_json(map.get(), id, &p));
    Text text(p, m3_string_free);
    return Json::parse(text.get());
}
inline Json link(const Map &map, const char *id) {
    char *p = nullptr;
    ok(m3_mindmap_get_link_json(map.get(), id, &p));
    Text text(p, m3_string_free);
    return Json::parse(text.get());
}
#endif
