#include "layout.h"
#include "json_codec.h"
#include <algorithm>
#include <cmath>
#include <string_view>
namespace m3 {
namespace {
void valid(bool condition) { require(condition, M3_ERR_INVALID_ARGUMENT, "Invalid layout input"); }
double finite(double value) { valid(std::isfinite(value)); return value; }
void dimensions(double width, double height) {
    valid(std::isfinite(width) && width > 0 && std::isfinite(height) && height > 0);
}
void validate_options(const M3LayoutOptions &o) {
    valid(o.direction == M3_LAYOUT_BALANCED || o.direction == M3_LAYOUT_RIGHT || o.direction == M3_LAYOUT_LEFT
        || o.direction == M3_LAYOUT_OUTLINE);
    valid(std::isfinite(o.horizontal_gap) && o.horizontal_gap >= 0 && std::isfinite(o.vertical_gap) && o.vertical_gap >= 0);
}
struct Adjacency { size_t first = M3_NO_PARENT, last = M3_NO_PARENT, next = M3_NO_PARENT; };
}
Geometry layout_tree(const M3LayoutNode *nodes, size_t count, const M3LayoutOptions &o) {
    validate_options(o);
    valid(nodes || count == 0);
    Geometry result;
    if (!count) return result;
    std::vector<Adjacency> adjacency(count);
    size_t root = M3_NO_PARENT;
    // One input pass constructs sibling lists in declared order.
    for (size_t i = 0; i < count; ++i) {
        dimensions(nodes[i].width, nodes[i].height);
        auto parent = nodes[i].parent_index;
        if (parent == M3_NO_PARENT) { valid(root == M3_NO_PARENT); root = i; }
        else {
            valid(parent < count && parent != i);
            auto &p = adjacency[parent];
            if (p.last == M3_NO_PARENT) p.first = i;
            else adjacency[p.last].next = i;
            p.last = i;
        }
    }
    valid(root != M3_NO_PARENT);
    std::vector<size_t> order;
    order.reserve(count); order.push_back(root);
    for (size_t i = 0; i < order.size(); ++i)
        for (size_t c = adjacency[order[i]].first; c != M3_NO_PARENT; c = adjacency[c].next) order.push_back(c);
    // Each non-root has exactly one parent. A cycle must be disconnected.
    valid(order.size() == count);
    result.rects.resize(count);
    result.rects[root] = {-nodes[root].width / 2, -nodes[root].height / 2, nodes[root].width, nodes[root].height};
    if (o.direction == M3_LAYOUT_OUTLINE) {
        double bottom = finite(result.rects[root].y + nodes[root].height);
        size_t i = adjacency[root].first;
        while (i != M3_NO_PARENT) {
            const auto &n = nodes[i];
            const auto &p = result.rects[n.parent_index];
            const double y = finite(bottom + o.vertical_gap);
            result.rects[i] = {finite(p.x + o.horizontal_gap), y, n.width, n.height};
            bottom = finite(y + n.height);
            if (adjacency[i].first != M3_NO_PARENT) i = adjacency[i].first;
            else {
                // Parent links let preorder resume without a recursive call stack.
                while (i != root && adjacency[i].next == M3_NO_PARENT) i = nodes[i].parent_index;
                i = adjacency[i].next;
            }
        }
    } else {
        std::vector<double> spans(count), stacks(count), centers(count);
        std::vector<int> sides(count, 1);
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            size_t i = *it;
            double stack = 0;
            for (size_t c = adjacency[i].first; c != M3_NO_PARENT; c = adjacency[c].next) {
                if (c != adjacency[i].first) stack = finite(stack + o.vertical_gap);
                stack = finite(stack + spans[c]);
            }
            stacks[i] = stack;
            spans[i] = std::max(nodes[i].height, stack);
        }
        double right_span = 0, left_span = 0;
        for (size_t c = adjacency[root].first; c != M3_NO_PARENT; c = adjacency[c].next) {
            const bool right = o.direction == M3_LAYOUT_RIGHT || (o.direction == M3_LAYOUT_BALANCED && right_span <= left_span);
            sides[c] = right ? 1 : -1;
            auto &span = right ? right_span : left_span;
            if (span > 0) span = finite(span + o.vertical_gap);
            span = finite(span + spans[c]);
        }
        double right_cursor = -right_span / 2, left_cursor = -left_span / 2;
        bool right_started = false, left_started = false;
        for (size_t c = adjacency[root].first; c != M3_NO_PARENT; c = adjacency[c].next) {
            auto &cursor = sides[c] == 1 ? right_cursor : left_cursor;
            auto &started = sides[c] == 1 ? right_started : left_started;
            if (started) cursor = finite(cursor + o.vertical_gap);
            started = true;
            centers[c] = finite(cursor + spans[c] / 2);
            cursor = finite(cursor + spans[c]);
        }
        for (size_t i : order) {
            if (i == root) continue;
            const size_t parent = nodes[i].parent_index;
            const auto &p = result.rects[parent];
            const auto &n = nodes[i];
            if (parent != root) sides[i] = sides[parent];
            const double x = sides[i] == 1 ? finite(finite(p.x + p.width) + o.horizontal_gap)
                : finite(finite(p.x - o.horizontal_gap) - n.width);
            result.rects[i] = {x, finite(centers[i] - n.height / 2), n.width, n.height};
            double cursor = finite(centers[i] - stacks[i] / 2);
            for (size_t c = adjacency[i].first; c != M3_NO_PARENT; c = adjacency[c].next) {
                centers[c] = finite(cursor + spans[c] / 2);
                cursor = finite(cursor + spans[c]);
                if (adjacency[c].next != M3_NO_PARENT) cursor = finite(cursor + o.vertical_gap);
            }
        }
    }
    double min_x = result.rects[root].x, min_y = result.rects[root].y;
    double max_x = finite(min_x + nodes[root].width), max_y = finite(min_y + nodes[root].height);
    for (const auto &r : result.rects) {
        min_x = std::min(min_x, r.x); min_y = std::min(min_y, r.y);
        max_x = std::max(max_x, finite(r.x + r.width)); max_y = std::max(max_y, finite(r.y + r.height));
    }
    result.bounds = {min_x, min_y, finite(max_x - min_x), finite(max_y - min_y)};
    return result;
}
Json layout_model(const Model &m, const M3NodeSize *sizes, size_t count, const M3LayoutOptions &o) {
    validate_options(o);
    valid(sizes || count == 0);
    std::unordered_map<const Node *, const M3NodeSize *> measurements;
    measurements.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        valid(sizes[i].id && *sizes[i].id && valid_utf8(sizes[i].id));
        dimensions(sizes[i].width, sizes[i].height);
        const std::string id = sizes[i].id;
        const auto &n = get_node(m, id);
        valid(measurements.emplace(&n, &sizes[i]).second);
    }
    const auto visible = preorder(m, true);
    std::unordered_map<std::string_view, size_t> indexes;
    indexes.reserve(visible.size());
    std::vector<M3LayoutNode> input;
    input.reserve(visible.size());
    for (const auto *n : visible) {
        auto measured = measurements.find(n);
        valid(measured != measurements.end());
        const auto *size = measured->second;
        size_t parent = n->parent.empty() ? M3_NO_PARENT : indexes.at(n->parent);
        indexes.emplace(n->id, input.size());
        input.push_back({parent, size->width, size->height});
    }
    const auto geometry = layout_tree(input.data(), input.size(), o);
    auto rectangle = [](const M3Rect &r) -> Json {
        return {{"x",r.x},{"y",r.y},{"width",r.width},{"height",r.height}};
    };
    Json nodes = Json::array(), edges = Json::array(), links = Json::array();
    for (size_t i = 0; i < visible.size(); ++i) {
        const auto *n = visible[i];
        auto record = rectangle(geometry.rects[i]);
        record["id"] = n->id;
        nodes.push_back(std::move(record));
        if (!n->parent.empty()) edges.push_back({{"source",n->parent},{"target",n->id}});
    }
    for (const auto *l : sorted_links(m))
        if (indexes.count(l->source) && indexes.count(l->target)) links.push_back(encode_link(*l));
    return {{"nodes",std::move(nodes)},{"treeEdges",std::move(edges)},
        {"crossLinks",std::move(links)},{"bounds",rectangle(geometry.bounds)}};
}
}
