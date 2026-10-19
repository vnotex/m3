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
    auto result = calculate(base_nodes,5,options());
    CHECK(result->node_count == 5);
    rect(result->rects[0],{-50,-20,100,40});
    CHECK(result->rects[1].x > result->rects[0].x+result->rects[0].width);
    for (size_t i : {3,4}) CHECK(result->rects[i].x+result->rects[i].width < result->rects[0].x);
    CHECK(result->rects[3].y > result->rects[4].y+result->rects[4].height);
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
    for (size_t i = 0; i < 4; ++i) rect(order->rects[i],result->rects[i+1]);
    rect(order->rects[4],result->rects[0]);
    const M3LayoutNode tall[] = {{M3_NO_PARENT,100,40},{0,60,20},{1,40,100},{0,80,30},{0,50,10}};
    auto tall_result = calculate(tall,5,options());
    near(tall_result->rects[1].y,-10);
    near(tall_result->rects[2].y,-50);
    near(tall_result->rects[3].x,-170);
    near(tall_result->rects[4].x,-140);
    const M3LayoutNode parent_tall[] = {{M3_NO_PARENT,100,40},{0,60,100},{1,40,10}};
    auto parent_result = calculate(parent_tall,3,options());
    near(parent_result->rects[1].y,-50); near(parent_result->rects[2].y,-5);
    const M3LayoutNode outline_nodes[] = {{4,40,10},{5,60,20},{1,150,30},{5,80,50},{1,25,15},{M3_NO_PARENT,100,40},{0,70,25}};
    const M3Rect outline_rects[] = {{46,117,40,10},{-18,28,60,20},{14,56,150,30},{-18,168,80,50},{14,94,25,15},{-50,-20,100,40},{78,135,70,25}};
    auto outline = calculate(outline_nodes,7,{M3_LAYOUT_OUTLINE,32,8});
    for (size_t i = 0; i < 7; ++i) rect(outline->rects[i],outline_rects[i]);
    rect(outline->bounds,{-50,-20,214,238});
    auto zero_gaps = calculate(outline_nodes,7,{M3_LAYOUT_OUTLINE,0,0});
    const size_t preorder[] = {5,1,2,4,0,6,3};
    double y = -20;
    for (size_t i : preorder) {
        near(zero_gaps->rects[i].x,-50); near(zero_gaps->rects[i].y,y);
        y += outline_nodes[i].height;
    }
    rect(zero_gaps->bounds,{-50,-20,150,190});
    auto map = load(fixture());
    auto semantic = document(map);
    const char *ids[] = {"r","a","d","b","c"};
    for (auto direction : {M3_LAYOUT_BALANCED,M3_LAYOUT_RIGHT,M3_LAYOUT_LEFT,M3_LAYOUT_OUTLINE}) {
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

static void balanced_clockwise() {
    const M3LayoutNode children[] = {
        {M3_NO_PARENT,100,40},{0,60,20},{0,60,20},{0,60,20},
        {0,60,20},{0,60,20},{0,60,20}
    };
    auto center = [](const M3Rect &r) { return r.y + r.height / 2; };
    auto no_overlap = [](const M3LayoutResult &result) {
        for (size_t i = 0; i < result.node_count; ++i) {
            for (size_t j = i+1; j < result.node_count; ++j) {
                const auto &a = result.rects[i]; const auto &b = result.rects[j];
                CHECK(a.x+a.width <= b.x || b.x+b.width <= a.x || a.y+a.height <= b.y || b.y+b.height <= a.y);
            }
        }
    };
    auto check_six = [&](const M3LayoutResult &result, size_t root_index) {
        const auto &root = result.rects[root_index];
        const double ys[] = {-40,0,40,40,0,-40};
        for (size_t i = 0; i < 6; ++i) {
            const auto &child = result.rects[root_index+i+1];
            if (i < 3) CHECK(child.x > root.x+root.width);
            else CHECK(child.x+child.width < root.x);
            rect(child,{i < 3 ? 90.0 : -150.0,center(root)+ys[i]-10,60,20});
        }
    };
    auto six = calculate(children,7,options());
    rect(six->rects[0],{-50,-20,100,40});
    check_six(*six,0);
    no_overlap(*six);
    for (double gap : {20.0,0.0}) {
        auto five = calculate(children,6,{M3_LAYOUT_BALANCED,40,gap});
        const double step = 20+gap;
        for (size_t i = 0; i < 5; ++i) {
            const double y = i < 3 ? (static_cast<double>(i)-1)*step : (3.5-static_cast<double>(i))*step;
            rect(five->rects[i+1],{i < 3 ? 90.0 : -150.0,y-10,60,20});
        }
        no_overlap(*five);
    }
    auto one = calculate(children,2,options());
    rect(one->rects[1],{90,-10,60,20});
    auto zero = calculate(children,1,options());
    rect(zero->rects[0],{-50,-20,100,40});
    rect(zero->bounds,zero->rects[0]);

    const M3LayoutNode tall[] = {{M3_NO_PARENT,100,40},{0,60,20},{1,40,100},{0,80,30},{0,50,10}};
    auto uneven = calculate(tall,5,options());
    CHECK(uneven->rects[1].x > uneven->rects[0].x+uneven->rects[0].width);
    for (size_t i : {3,4}) CHECK(uneven->rects[i].x+uneven->rects[i].width < uneven->rects[0].x);
    CHECK(center(uneven->rects[3]) > center(uneven->rects[4]));
    no_overlap(*uneven);

    std::vector<M3LayoutNode> descendants(children,children+7);
    descendants.push_back({6,60,20}); descendants.push_back({6,60,20});
    auto branch = calculate(descendants.data(),descendants.size(),options());
    CHECK(branch->rects[5].x+branch->rects[5].width < branch->rects[0].x);
    CHECK(branch->rects[6].x+branch->rects[6].width < branch->rects[0].x);
    CHECK(center(branch->rects[5]) > center(branch->rects[6]));
    CHECK(branch->rects[7].x+branch->rects[7].width < branch->rects[6].x);
    CHECK(branch->rects[8].x+branch->rects[8].width < branch->rects[6].x);
    CHECK(center(branch->rects[7]) < center(branch->rects[8]));
    no_overlap(*branch);

    std::vector<M3LayoutNode> forest(children,children+7);
    forest.push_back(children[0]);
    for (size_t i = 1; i < 7; ++i) forest.push_back({7,60,20});
    auto trees = calculate(forest.data(),forest.size(),options());
    check_six(*trees,0); check_six(*trees,7);
    double first_bottom = trees->rects[0].y+trees->rects[0].height;
    double second_top = trees->rects[7].y;
    for (size_t i = 1; i < 7; ++i) {
        first_bottom = std::max(first_bottom,trees->rects[i].y+trees->rects[i].height);
        second_top = std::min(second_top,trees->rects[i+7].y);
    }
    near(second_top-first_bottom,20);
    no_overlap(*trees);
}

static void orphans() {
    // Interleave components and place descendants before their roots.
    const M3LayoutNode forest[] = {
        {3,40,10},{4,60,20},{4,80,30},{6,60,20},{M3_NO_PARENT,100,40},{4,50,10},
        {M3_NO_PARENT,100,40},{6,80,30},{M3_NO_PARENT,30,120},{1,40,10},{6,50,10}
    };
    const size_t main_indexes[] = {4,1,9,2,5}, orphan_indexes[] = {6,3,0,7,10};
    const M3NodeSize sizes[] = {
        {"t",40,10},{"a",60,20},{"b",80,30},{"p",60,20},{"r",100,40},{"c",50,10},
        {"o",100,40},{"q",80,30},{"z",30,120},{"d",40,10},{"s",50,10}
    };
    const size_t preorder[] = {4,1,9,2,5,6,3,0,7,10,8};
    auto input = fixture();
    input["orphanIds"] = {"o","z"};
    input["nodes"].push_back({{"id","z"}});
    input["nodes"].push_back({{"id","t"}});
    input["nodes"].push_back({{"id","q"}});
    input["nodes"].push_back({{"id","o"},{"children",{"p","q","s"}}});
    input["nodes"].push_back({{"id","s"}});
    input["nodes"].push_back({{"id","p"},{"children",{"t"}}});
    input["crossLinks"].push_back({{"id","forest"},{"source","d"},{"target","t"},{"directed",false}});
    auto map = load(input);
    const auto semantic = document(map);
    for (auto direction : {M3_LAYOUT_BALANCED,M3_LAYOUT_RIGHT,M3_LAYOUT_LEFT,M3_LAYOUT_OUTLINE}) {
        for (double gap : {0.0,20.0}) {
            const M3LayoutOptions opts{direction,40,gap};
            auto single = calculate(base_nodes,5,opts);
            auto result = calculate(forest,11,opts);
            CHECK(result->node_count == 11);
            const double offset = single->bounds.height + gap;
            for (size_t i = 0; i < 5; ++i) {
                const auto &r = single->rects[i];
                rect(result->rects[main_indexes[i]],r);
                rect(result->rects[orphan_indexes[i]],{r.x,r.y+offset,r.width,r.height});
            }
            rect(result->rects[8],{-15,single->bounds.y+2*offset,30,120});
            rect(result->bounds,{single->bounds.x,single->bounds.y,single->bounds.width,2*offset+120});
            for (size_t i = 0; i < 11; ++i) {
                for (size_t j = i+1; j < 11; ++j) {
                    const auto &a = result->rects[i]; const auto &b = result->rects[j];
                    CHECK(a.x+a.width <= b.x || b.x+b.width <= a.x || a.y+a.height <= b.y || b.y+b.height <= a.y);
                }
            }
            const auto projected = layout(map,sizes,11,opts);
            CHECK(projected["nodes"].size() == 11 && projected["treeEdges"].size() == 8);
            CHECK(projected["crossLinks"] == semantic["crossLinks"]);
            for (size_t i = 0; i < 11; ++i) {
                const auto &n = projected["nodes"][i];
                CHECK(n["id"] == sizes[preorder[i]].id);
                rect({n["x"],n["y"],n["width"],n["height"]},result->rects[preorder[i]]);
            }
            const auto &bounds = projected["bounds"];
            rect({bounds["x"],bounds["y"],bounds["width"],bounds["height"]},result->bounds);
            char *out = reinterpret_cast<char *>(1);
            CHECK(m3_mindmap_layout_json(map.get(),sizes+1,10,&opts,&out) == M3_ERR_INVALID_ARGUMENT);
            CHECK(out == nullptr && *m3_last_error());
        }
    }
    CHECK(document(map) == semantic);
    constexpr size_t count = 10000;
    const std::vector<M3LayoutNode> roots(count,{M3_NO_PARENT,10,4});
    for (auto direction : {M3_LAYOUT_BALANCED,M3_LAYOUT_RIGHT,M3_LAYOUT_LEFT,M3_LAYOUT_OUTLINE}) {
        auto result = calculate(roots.data(),roots.size(),{direction,2,1});
        CHECK(result->node_count == count);
        for (size_t i = 0; i < count; ++i) rect(result->rects[i],{-5,-2+5*static_cast<double>(i),10,4});
        rect(result->bounds,{-5,-2,10,4+5*static_cast<double>(count-1)});
    }
}

static void legacy_expansion() {
    const auto input = fixture();
    auto plain = load(input);
    auto legacy_input = input;
    for (auto &n : legacy_input["nodes"]) n["expanded"] = false;
    auto legacy = load(legacy_input);
    const auto semantic = document(plain);
    CHECK(document(legacy) == semantic);
    const M3NodeSize missing_descendant[] = {{"r",100,40},{"a",60,20},{"b",80,30},{"c",50,10}};
    for (auto direction : {M3_LAYOUT_BALANCED,M3_LAYOUT_RIGHT,M3_LAYOUT_LEFT,M3_LAYOUT_OUTLINE}) {
        auto opts = options(direction);
        const auto complete = layout(legacy,base_sizes,5,opts);
        CHECK(complete == layout(plain,base_sizes,5,opts));
        CHECK(complete["crossLinks"] == semantic["crossLinks"]);
        Json ids = Json::array();
        for (const auto &n : complete["nodes"]) ids.push_back(n["id"]);
        CHECK(ids == Json({"r","a","d","b","c"}));
        char *out = reinterpret_cast<char *>(1);
        CHECK(m3_mindmap_layout_json(legacy.get(),missing_descendant,4,&opts,&out) == M3_ERR_INVALID_ARGUMENT);
        CHECK(out == nullptr && *m3_last_error());
    }
    CHECK(document(legacy) == semantic);
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
    reject({{M3_NO_PARENT,10,10},{M3_NO_PARENT,10,10},{3,10,10},{2,10,10}});
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
    for (auto direction : {M3_LAYOUT_BALANCED,M3_LAYOUT_RIGHT,M3_LAYOUT_LEFT,M3_LAYOUT_OUTLINE}) {
        reject({{M3_NO_PARENT,10,max},{M3_NO_PARENT,10,max}}, {direction,0,0});
        reject({{M3_NO_PARENT,10,max*0.6},{M3_NO_PARENT,10,max*0.6}}, {direction,0,0});
        reject({{M3_NO_PARENT,10,10},{M3_NO_PARENT,10,10},{M3_NO_PARENT,10,10}}, {direction,0,max});
    }
    reject({{M3_NO_PARENT,max,10},{0,max,10}}, {M3_LAYOUT_RIGHT,max,0});
    reject({{M3_NO_PARENT,10,10},{0,10,max},{0,10,max}});
    reject({{M3_NO_PARENT,10,10}}, {M3_LAYOUT_OUTLINE,std::numeric_limits<double>::quiet_NaN(),0});
    reject({{M3_NO_PARENT,10,10},{0,10,10},{1,10,10}}, {M3_LAYOUT_OUTLINE,max,0});
    reject({{M3_NO_PARENT,10,10},{0,10,max},{0,10,max}}, {M3_LAYOUT_OUTLINE,0,0});
    reject({{M3_NO_PARENT,10,10},{0,10,10},{0,10,10}}, {M3_LAYOUT_OUTLINE,0,max});
    reject({{M3_NO_PARENT,max,10},{0,max/2,10}}, {M3_LAYOUT_OUTLINE,max,0});
    M3LayoutResult *out = reinterpret_cast<M3LayoutResult *>(1);
    auto opts = options();
    CHECK(m3_layout_tree(nullptr,1,&opts,&out) == M3_ERR_INVALID_ARGUMENT && out == nullptr);
    out = reinterpret_cast<M3LayoutResult *>(1);
    CHECK(m3_layout_tree(nullptr,0,nullptr,&out) == M3_ERR_INVALID_ARGUMENT && out == nullptr);
    CHECK(m3_layout_tree(nullptr,0,&opts,nullptr) == M3_ERR_INVALID_ARGUMENT);
    auto map = load(fixture());
    const auto baseline = document(map);
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
    bad_sizes(base_sizes,5,M3_ERR_INVALID_ARGUMENT,{M3_LAYOUT_OUTLINE,max,0});
    auto invalid_dimensions = std::vector<M3NodeSize>(base_sizes,base_sizes+5);
    invalid_dimensions[2].height = 0;
    bad_sizes(invalid_dimensions.data(),invalid_dimensions.size(),M3_ERR_INVALID_ARGUMENT);
    invalid_dimensions[2].height = std::numeric_limits<double>::quiet_NaN();
    bad_sizes(invalid_dimensions.data(),invalid_dimensions.size(),M3_ERR_INVALID_ARGUMENT);
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
    const auto outline = layout(map,sizes.data(),sizes.size(),{M3_LAYOUT_OUTLINE,2,1});
    CHECK(outline["nodes"].size() == count && outline["treeEdges"].size() == count-1);
    near(outline["nodes"][count-1]["x"],-5+2*(count-1));
    near(outline["nodes"][count-1]["y"],-2+5*(count-1));
    near(outline["bounds"]["width"],10+2*(count-1));
    near(outline["bounds"]["height"],4+5*(count-1));
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
        else if (name == "balanced_clockwise") balanced_clockwise();
        else if (name == "orphans") orphans();
        else if (name == "legacy_expansion") legacy_expansion();
        else if (name == "invalid") invalid();
        else if (name == "deep") deep();
        else CHECK(false);
        return 0;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
