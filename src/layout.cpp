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
    size_t first_root = M3_NO_PARENT, last_root = M3_NO_PARENT;
    // One input pass constructs sibling lists in declared order.
    for (size_t i = 0; i < count; ++i) {
        dimensions(nodes[i].width, nodes[i].height);
        auto parent = nodes[i].parent_index;
        if (parent == M3_NO_PARENT) {
            if (last_root == M3_NO_PARENT) first_root = i;
            else adjacency[last_root].next = i;
            last_root = i;
        }
        else {
            valid(parent < count && parent != i);
            auto &p = adjacency[parent];
            if (p.last == M3_NO_PARENT) p.first = i;
            else adjacency[p.last].next = i;
            p.last = i;
        }
    }
    valid(first_root != M3_NO_PARENT);
    std::vector<size_t> order;
    order.reserve(count);
    // Keep each component contiguous, with parents before children.
    for (size_t root = first_root; root != M3_NO_PARENT; root = adjacency[root].next) {
        const size_t begin = order.size();
        order.push_back(root);
        for (size_t i = begin; i < order.size(); ++i)
            for (size_t c = adjacency[order[i]].first; c != M3_NO_PARENT; c = adjacency[c].next) order.push_back(c);
    }
    // Each non-root has exactly one parent. A cycle cannot reach any root.
    valid(order.size() == count);
    result.rects.resize(count);
    for (size_t root = first_root; root != M3_NO_PARENT; root = adjacency[root].next)
        result.rects[root] = {-nodes[root].width / 2, -nodes[root].height / 2, nodes[root].width, nodes[root].height};
    if (o.direction == M3_LAYOUT_OUTLINE) {
        for (size_t root = first_root; root != M3_NO_PARENT; root = adjacency[root].next) {
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
                    i = i == root ? M3_NO_PARENT : adjacency[i].next;
                }
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
        for (size_t root = first_root; root != M3_NO_PARENT; root = adjacency[root].next) {
            size_t first_left = M3_NO_PARENT;
            if (o.direction == M3_LAYOUT_BALANCED && adjacency[root].first != adjacency[root].last) {
                // Removing the split gap leaves the two side stacks. Choose the
                // prefix nearest half their total; later ties keep more on the right.
                const double target = (stacks[root] - o.vertical_gap) / 2;
                double prefix = 0, best_distance = stacks[root];
                for (size_t c = adjacency[root].first; c != adjacency[root].last; c = adjacency[c].next) {
                    if (c != adjacency[root].first) prefix = finite(prefix + o.vertical_gap);
                    prefix = finite(prefix + spans[c]);
                    const double distance = std::abs(prefix - target);
                    if (distance <= best_distance) {
                        best_distance = distance;
                        first_left = adjacency[c].next;
                    }
                }
            }
            double right_span = 0, left_span = 0;
            bool right = o.direction != M3_LAYOUT_LEFT;
            for (size_t c = adjacency[root].first; c != M3_NO_PARENT; c = adjacency[c].next) {
                if (c == first_left) right = false;
                sides[c] = right ? 1 : -1;
                auto &span = right ? right_span : left_span;
                if (span > 0) span = finite(span + o.vertical_gap);
                span = finite(span + spans[c]);
            }
            double right_cursor = -right_span / 2;
            double left_cursor = o.direction == M3_LAYOUT_BALANCED ? left_span / 2 : -left_span / 2;
            bool right_started = false, left_started = false;
            for (size_t c = adjacency[root].first; c != M3_NO_PARENT; c = adjacency[c].next) {
                auto &cursor = sides[c] == 1 ? right_cursor : left_cursor;
                auto &started = sides[c] == 1 ? right_started : left_started;
                if (o.direction == M3_LAYOUT_BALANCED && sides[c] == -1) {
                    if (started) cursor = finite(cursor - o.vertical_gap);
                    centers[c] = finite(cursor - spans[c] / 2);
                    cursor = finite(cursor - spans[c]);
                } else {
                    if (started) cursor = finite(cursor + o.vertical_gap);
                    centers[c] = finite(cursor + spans[c] / 2);
                    cursor = finite(cursor + spans[c]);
                }
                started = true;
            }
        }
        for (size_t i : order) {
            const size_t parent = nodes[i].parent_index;
            if (parent == M3_NO_PARENT) continue;
            const auto &p = result.rects[parent];
            const auto &n = nodes[i];
            if (nodes[parent].parent_index != M3_NO_PARENT) sides[i] = sides[parent];
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
    if (adjacency[first_root].next != M3_NO_PARENT) {
        double bottom = 0;
        for (size_t begin = 0; begin < order.size();) {
            size_t end = begin + 1;
            double top = result.rects[order[begin]].y;
            while (end < order.size() && nodes[order[end]].parent_index != M3_NO_PARENT) {
                top = std::min(top, result.rects[order[end]].y);
                ++end;
            }
            const double offset = begin == 0 ? 0 : finite(finite(bottom + o.vertical_gap) - top);
            bottom = top;
            for (size_t i = begin; i < end; ++i) {
                auto &r = result.rects[order[i]];
                if (begin != 0) r.y = finite(r.y + offset);
                bottom = std::max(bottom, finite(r.y + r.height));
            }
            begin = end;
        }
    }
    double min_x = result.rects[first_root].x, min_y = result.rects[first_root].y;
    double max_x = finite(min_x + nodes[first_root].width), max_y = finite(min_y + nodes[first_root].height);
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
    const auto nodes = preorder(m);
    std::unordered_map<std::string_view, size_t> indexes;
    indexes.reserve(nodes.size());
    std::vector<M3LayoutNode> input;
    input.reserve(nodes.size());
    for (const auto *n : nodes) {
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
    Json records = Json::array(), edges = Json::array(), links = Json::array();
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto *n = nodes[i];
        auto record = rectangle(geometry.rects[i]);
        record["id"] = n->id;
        records.push_back(std::move(record));
        if (!n->parent.empty()) edges.push_back({{"source",n->parent},{"target",n->id}});
    }
    for (const auto *l : sorted_links(m)) links.push_back(encode_link(*l));
    return {{"nodes",std::move(records)},{"treeEdges",std::move(edges)},
        {"crossLinks",std::move(links)},{"bounds",rectangle(geometry.bounds)}};
}
}
