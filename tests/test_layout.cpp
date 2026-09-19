#include "test_support.h"
#include <cmath>
#include <limits>
#include <vector>
using Layout = std::unique_ptr<M3LayoutResult, decltype(&m3_layout_result_free)>;
static const M3LayoutNode base_nodes[] = {{M3_NO_PARENT,100,40},{0,60,20},{1,40,10},{0,80,30},{0,50,10}};
static const M3NodeSize base_sizes[] = {{"r",100,40},{"a",60,20},{"d",40,10},{"b",80,30},{"c",50,10}};
static M3LayoutOptions options(M3LayoutDirection direction = M3_LAYOUT_BALANCED) { return {direction,40,20}; }
static void near(double actual, double expected) { CHECK(std::abs(actual-expected) < 1e-9); }
static void rect(const M3Rect &actual, const M3Rect &expected) {
    near(actual.x,expected.x); near(actual.y,expected.y); near(actual.width,expected.width); near(actual.height,expected.height);
}
static Layout calculate(const M3LayoutNode *nodes, size_t count, const M3LayoutOptions &opts) {
    M3LayoutResult *raw = nullptr;
    ok(m3_layout_tree(nodes,count,&opts,&raw));
    return Layout(raw,m3_layout_result_free);
}
static Json layout(const Map &map, const M3NodeSize *sizes = base_sizes, size_t count = 5, M3LayoutOptions opts = options()) {
    char *raw = nullptr;
    ok(m3_mindmap_layout_json(map.get(),sizes,count,&opts,&raw));
    Text text(raw,m3_string_free);
    return Json::parse(text.get());
}
static void geometry() {
    const M3Rect balanced[] = {{-50,-20,100,40},{90,-25,60,20},{190,-20,40,10},{-170,-15,80,30},{90,15,50,10}};
    auto result = calculate(base_nodes,5,options());
    CHECK(result->node_count == 5);
    for (size_t i = 0; i < 5; ++i) rect(result->rects[i],balanced[i]);
    rect(result->bounds,{-170,-25,400,50});
    auto again = calculate(base_nodes,5,options());
    for (size_t i = 0; i < 5; ++i) rect(again->rects[i],result->rects[i]);
    const M3Rect right_rects[] = {{-50,-20,100,40},{90,-50,60,20},{190,-45,40,10},{90,-10,80,30},{90,40,50,10}};
    auto right = calculate(base_nodes,5,options(M3_LAYOUT_RIGHT));
    auto left = calculate(base_nodes,5,options(M3_LAYOUT_LEFT));
    rect(right->bounds,{-50,-50,280,100});
    rect(left->bounds,{-230,-50,280,100});
    for (size_t i = 0; i < 5; ++i) {
        rect(right->rects[i],right_rects[i]);
        rect(left->rects[i],{-right_rects[i].x-right_rects[i].width,right_rects[i].y,right_rects[i].width,right_rects[i].height});
        for (size_t j = i+1; j < 5; ++j) {
            const auto &a = result->rects[i]; const auto &b = result->rects[j];
            CHECK(a.x+a.width <= b.x || b.x+b.width <= a.x || a.y+a.height <= b.y || b.y+b.height <= a.y);
        }
    }
    const M3LayoutNode reordered[] = {{4,60,20},{0,40,10},{4,80,30},{4,50,10},{M3_NO_PARENT,100,40}};
    auto order = calculate(reordered,5,options());
    for (size_t i = 0; i < 4; ++i) rect(order->rects[i],balanced[i+1]);
    rect(order->rects[4],balanced[0]);
    const M3LayoutNode tall[] = {{M3_NO_PARENT,100,40},{0,60,20},{1,40,100},{0,80,30},{0,50,10}};
    auto tall_result = calculate(tall,5,options());
    near(tall_result->rects[1].y,-10);
    near(tall_result->rects[2].y,-50);
    near(tall_result->rects[3].x,-170);
    near(tall_result->rects[4].x,-140);
    const M3LayoutNode parent_tall[] = {{M3_NO_PARENT,100,40},{0,60,100},{1,40,10}};
    auto parent_result = calculate(parent_tall,3,options());
    near(parent_result->rects[1].y,-50); near(parent_result->rects[2].y,-5);
    auto map = load(fixture());
    auto semantic = document(map);
    const char *ids[] = {"r","a","d","b","c"};
    for (auto direction : {M3_LAYOUT_BALANCED,M3_LAYOUT_RIGHT,M3_LAYOUT_LEFT}) {
        auto j = layout(map,base_sizes,5,options(direction));
        auto standalone = calculate(base_nodes,5,options(direction));
        for (size_t i = 0; i < 5; ++i) {
            const auto &n = j["nodes"][i];
            CHECK(n["id"] == ids[i]);
            rect({n["x"],n["y"],n["width"],n["height"]},standalone->rects[i]);
        }
        const auto &b = j["bounds"];
        rect({b["x"],b["y"],b["width"],b["height"]},standalone->bounds);
        CHECK(j["crossLinks"] == semantic["crossLinks"]);
        CHECK(j["treeEdges"] == Json::parse(R"([{"source":"r","target":"a"},{"source":"a","target":"d"},{"source":"r","target":"b"},{"source":"r","target":"c"}])"));
    }
    CHECK(document(map) == semantic);
    auto before = layout(map);
    ok(m3_mindmap_update_node(map.get(),"r",R"({"topic":"Much longer topic","style":{"size":999}})"));
    ok(m3_mindmap_add_link(map.get(),R"({"id":"extra","source":"d","target":"r","directed":true})"));
    CHECK(layout(map)["nodes"] == before["nodes"]);
    CHECK(layout(map)["bounds"] == before["bounds"]);

}

static void collapse() {
    auto map = load(fixture());
    const auto original = layout(map);
    const auto semantic = document(map);
    ok(m3_mindmap_update_node(map.get(),"a",R"({"expanded":false})"));
    const auto collapsed = layout(map);
    Json ids = Json::array();
    for (const auto &n : collapsed["nodes"]) ids.push_back(n["id"]);
    CHECK(ids == Json({"r","a","b","c"}));
    CHECK(collapsed["treeEdges"] == Json::parse(R"([{"source":"r","target":"a"},{"source":"r","target":"b"},{"source":"r","target":"c"}])"));
    CHECK(collapsed["crossLinks"] == Json::array({link(map,"l1"),link(map,"l3"),link(map,"l4")}));
    CHECK(collapsed["bounds"] == Json({{"x",-170},{"y",-25},{"width",320},{"height",50}}));
    CHECK(document(map)["crossLinks"] == semantic["crossLinks"]);
    CHECK(node(map,"d")["id"] == "d");
    const M3NodeSize visible_sizes[] = {{"r",100,40},{"a",60,20},{"b",80,30},{"c",50,10}};
    CHECK(layout(map,visible_sizes,4) == collapsed);
    ok(m3_mindmap_update_node(map.get(),"a",R"({"expanded":true})"));
    CHECK(layout(map) == original);
    ok(m3_mindmap_update_node(map.get(),"r",R"({"expanded":false})"));
    auto root = layout(map,base_sizes,1);
    CHECK(root["nodes"] == Json::array({original["nodes"][0]}));
    CHECK(root["crossLinks"].empty() && root["treeEdges"].empty());
    CHECK(root["bounds"] == Json({{"x",-50},{"y",-20},{"width",100},{"height",40}}));
    CHECK(document(map)["nodes"].size() == 5);
    CHECK(document(map)["crossLinks"] == semantic["crossLinks"]);
}


static void invalid() {
    auto empty = calculate(nullptr,0,options());
    CHECK(empty->node_count == 0 && empty->rects == nullptr);
    rect(empty->bounds,{0,0,0,0});
    m3_layout_result_free(nullptr);
    auto reject = [&](const std::vector<M3LayoutNode> &nodes, M3LayoutOptions opts = options()) {
        M3LayoutResult *out = reinterpret_cast<M3LayoutResult *>(1);
        CHECK(m3_layout_tree(nodes.data(),nodes.size(),&opts,&out) == M3_ERR_INVALID_ARGUMENT);
        CHECK(out == nullptr && *m3_last_error());
    };
    reject({{M3_NO_PARENT,10,10},{2,10,10},{1,10,10}});
    reject({{1,10,10},{0,10,10}});
    reject({{M3_NO_PARENT,10,10},{M3_NO_PARENT,10,10}});
    reject({{M3_NO_PARENT,10,10},{3,10,10}});
    reject({{M3_NO_PARENT,10,10},{1,10,10}});
    reject({{M3_NO_PARENT,0,10}});
    reject({{M3_NO_PARENT,std::numeric_limits<double>::infinity(),10}});
    reject({{M3_NO_PARENT,10,std::numeric_limits<double>::quiet_NaN()}});
    reject({{M3_NO_PARENT,10,10}}, {static_cast<M3LayoutDirection>(7),40,20});
    reject({{M3_NO_PARENT,10,10}}, {M3_LAYOUT_RIGHT,-1,20});
    reject({{M3_NO_PARENT,10,10}}, {M3_LAYOUT_RIGHT,40,-1});
    reject({{M3_NO_PARENT,10,10}}, {M3_LAYOUT_RIGHT,40,std::numeric_limits<double>::infinity()});
    const double max = std::numeric_limits<double>::max();
    reject({{M3_NO_PARENT,max,10},{0,max,10}}, {M3_LAYOUT_RIGHT,max,0});
    reject({{M3_NO_PARENT,10,10},{0,10,max},{0,10,max}});
    M3LayoutResult *out = reinterpret_cast<M3LayoutResult *>(1);
    auto opts = options();
    CHECK(m3_layout_tree(nullptr,1,&opts,&out) == M3_ERR_INVALID_ARGUMENT && out == nullptr);
    out = reinterpret_cast<M3LayoutResult *>(1);
    CHECK(m3_layout_tree(nullptr,0,nullptr,&out) == M3_ERR_INVALID_ARGUMENT && out == nullptr);
    CHECK(m3_layout_tree(nullptr,0,&opts,nullptr) == M3_ERR_INVALID_ARGUMENT);
    auto map = load(fixture());
    auto baseline = document(map);
    auto bad_sizes = [&](const M3NodeSize *sizes, size_t count, M3Status expected, M3LayoutOptions o = options()) {
        char *text = reinterpret_cast<char *>(1);
        CHECK(m3_mindmap_layout_json(map.get(),sizes,count,&o,&text) == expected);
        CHECK(text == nullptr && *m3_last_error());
        CHECK(document(map) == baseline);
    };
    bad_sizes(base_sizes,4,M3_ERR_INVALID_ARGUMENT);
    bad_sizes(nullptr,0,M3_ERR_INVALID_ARGUMENT);
    bad_sizes(nullptr,5,M3_ERR_INVALID_ARGUMENT);
    auto duplicate = std::vector<M3NodeSize>(base_sizes,base_sizes+5);
    duplicate.push_back(base_sizes[0]);
    bad_sizes(duplicate.data(),duplicate.size(),M3_ERR_INVALID_ARGUMENT);
    const M3NodeSize unknown[] = {{"unknown",10,10}};
    bad_sizes(unknown,1,M3_ERR_NOT_FOUND);
    const M3NodeSize utf8[] = {{"\xf4\x90\x80\x80",10,10}};
    bad_sizes(utf8,1,M3_ERR_INVALID_ARGUMENT);
    bad_sizes(base_sizes,5,M3_ERR_INVALID_ARGUMENT,{static_cast<M3LayoutDirection>(-1),40,20});
    ok(m3_mindmap_update_node(map.get(),"a",R"({"expanded":false})"));
    baseline = document(map);
    auto hidden_bad = std::vector<M3NodeSize>(base_sizes,base_sizes+5);
    hidden_bad[2].height = 0;
    bad_sizes(hidden_bad.data(),hidden_bad.size(),M3_ERR_INVALID_ARGUMENT);
    hidden_bad[2].height = std::numeric_limits<double>::quiet_NaN();
    bad_sizes(hidden_bad.data(),hidden_bad.size(),M3_ERR_INVALID_ARGUMENT);
    hidden_bad = std::vector<M3NodeSize>(base_sizes,base_sizes+5);
    hidden_bad.push_back(base_sizes[2]);
    bad_sizes(hidden_bad.data(),hidden_bad.size(),M3_ERR_INVALID_ARGUMENT);
}


static void deep() {
    constexpr size_t count = 10000, retained = 5000;
    Json nodes = Json::array();
    std::vector<std::string> ids;
    ids.reserve(count);
    for (size_t i = 0; i < count; ++i) ids.push_back(std::to_string(i));
    std::vector<M3NodeSize> sizes;
    sizes.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Json children = Json::array();
        if (i+1 < count) children.push_back(ids[i+1]);
        nodes.push_back({{"id",ids[i]},{"children",std::move(children)}});
        sizes.push_back({ids[i].c_str(),10,4});
    }
    auto map = load({{"schemaVersion",1},{"rootId","0"},{"nodes",std::move(nodes)},
        {"crossLinks",Json::array({{{"id","long"},{"source","0"},{"target","9999"},{"directed",true}},
                                   {{"id","short"},{"source","0"},{"target","1"},{"directed",false}}})}});
    const auto exported = document(map);
    CHECK(exported["nodes"].size() == count);
    for (size_t i = 0; i < count; ++i) CHECK(exported["nodes"][i]["id"] == ids[i]);
    const auto geometry = layout(map,sizes.data(),sizes.size(),{M3_LAYOUT_RIGHT,2,1});
    CHECK(geometry["nodes"].size() == count && geometry["treeEdges"].size() == count-1);
    near(geometry["nodes"][0]["x"],-5);
    near(geometry["nodes"][count-1]["x"],-5+12*(count-1));
    near(geometry["nodes"][count-1]["y"],-2);
    near(geometry["bounds"]["width"],10+12*(count-1));
    CHECK(document(map) == exported);
    ok(m3_mindmap_remove_subtree(map.get(),ids[retained].c_str()));
    const auto remaining = document(map);
    CHECK(remaining["nodes"].size() == retained);
    for (size_t i = 0; i < retained; ++i) CHECK(remaining["nodes"][i]["id"] == ids[i]);
    CHECK(remaining["nodes"][retained-1]["children"].empty());
    CHECK(remaining["crossLinks"].size() == 1 && remaining["crossLinks"][0]["id"] == "short");
    auto reloaded = load(remaining);
    CHECK(document(reloaded) == remaining);
}

int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "geometry") geometry();
        else if (name == "collapse") collapse();
        else if (name == "invalid") invalid();
        else if (name == "deep") deep();
        else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
