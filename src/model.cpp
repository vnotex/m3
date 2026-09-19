#include "json_codec.h"
#include <unordered_set>
#include <type_traits>
#include <algorithm>
namespace m3 {
bool valid_utf8(const char *text) noexcept {
    if (!text) return false;
    const auto *p = reinterpret_cast<const unsigned char *>(text);
    while (*p) {
        unsigned value = *p++;
        if (value < 0x80) continue;
        unsigned remaining, minimum;
        if (value >= 0xc2 && value <= 0xdf) { remaining = 1; minimum = 0x80; value &= 0x1f; }
        else if (value >= 0xe0 && value <= 0xef) { remaining = 2; minimum = 0x800; value &= 0x0f; }
        else if (value >= 0xf0 && value <= 0xf4) { remaining = 3; minimum = 0x10000; value &= 0x07; }
        else return false;
        while (remaining--) {
            if ((*p & 0xc0) != 0x80) return false;
            value = (value << 6) | (*p++ & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
const Node &get_node(const Model &m, const std::string &id) {
    auto it = m.nodes.find(id);
    require(it != m.nodes.end(), M3_ERR_NOT_FOUND, "Node not found");
    return it->second;
}
const Link &get_link(const Model &m, const std::string &id) {
    auto it = m.links.find(id);
    require(it != m.links.end(), M3_ERR_NOT_FOUND, "Link not found");
    return it->second;
}
std::vector<const Node *> preorder(const Model &m, bool visible) {
    std::vector<const Node *> result, pending{&get_node(m, m.root)};
    if (!visible) result.reserve(m.nodes.size());
    while (!pending.empty()) {
        const auto *node = pending.back();
        pending.pop_back();
        result.push_back(node);
        if (!visible || node->attrs.expanded)
            for (auto it = node->children.rbegin(); it != node->children.rend(); ++it)
                pending.push_back(&get_node(m, *it));
    }
    return result;
}
std::vector<const Link *> sorted_links(const Model &m) {
    std::vector<const Link *> result;
    result.reserve(m.links.size());
    for (const auto &entry : m.links) result.push_back(&entry.second);
    std::sort(result.begin(), result.end(), [](const Link *a, const Link *b) { return a->id < b->id; });
    return result;
}

namespace {
Node &mutable_node(Model &m, const std::string &id) {
    auto it = m.nodes.find(id);
    require(it != m.nodes.end(), M3_ERR_NOT_FOUND, "Node not found");
    return it->second;
}
size_t position(size_t index, size_t count) {
    if (index == M3_APPEND) return count;
    require(index <= count, M3_ERR_INVALID_ARGUMENT, "Index out of range");
    return index;
}
void endpoints(const Model &m, const Link &l) {
    require(m.nodes.count(l.source) && m.nodes.count(l.target), M3_ERR_NOT_FOUND, "Link endpoint not found");
}
}
void insert_node(Model &m, const std::string &parent, size_t index, Node n) {
    auto &p = mutable_node(m, parent);
    require(!m.nodes.count(n.id), M3_ERR_ALREADY_EXISTS, "Node already exists");
    require(n.children.empty(), M3_ERR_SCHEMA, "Inserted node must be a leaf");
    index = position(index, p.children.size());
    auto children = p.children;
    children.insert(children.begin() + index, n.id);
    n.parent = parent;
    auto id = n.id;
    m.nodes.emplace(std::move(id), std::move(n));
    // Rehash does not invalidate references. All allocation preceded this swap.
    p.children.swap(children);
}
void update_node(Model &m, const std::string &id, const Json &patch) {
    auto &n = mutable_node(m, id);
    auto attrs = patch_attributes(n.attrs, patch);
    static_assert(std::is_nothrow_swappable_v<Attributes>);
    using std::swap;
    swap(n.attrs, attrs);
}
void move_node(Model &m, const std::string &id, const std::string &parent, size_t index) {
    auto &n = mutable_node(m, id);
    auto &destination = mutable_node(m, parent);
    require(id != m.root, M3_ERR_INVALID_OPERATION, "Cannot move root");
    for (const Node *ancestor = &destination; ancestor;) {
        require(ancestor->id != id, M3_ERR_INVALID_OPERATION, "Cannot move into subtree");
        ancestor = ancestor->parent.empty() ? nullptr : &get_node(m, ancestor->parent);
    }
    auto &source = mutable_node(m, n.parent);
    auto old_children = source.children;
    old_children.erase(std::find(old_children.begin(), old_children.end(), id));
    if (&source == &destination) {
        index = position(index, old_children.size());
        old_children.insert(old_children.begin() + index, id);
        source.children.swap(old_children);
    } else {
        index = position(index, destination.children.size());
        auto new_children = destination.children;
        new_children.insert(new_children.begin() + index, id);
        auto new_parent = parent;
        source.children.swap(old_children);
        destination.children.swap(new_children);
        n.parent.swap(new_parent);
    }
}
void remove_subtree(Model &m, const std::string &id) {
    const auto &n = get_node(m, id);
    require(id != m.root, M3_ERR_INVALID_OPERATION, "Cannot remove root");
    std::vector<const Node *> pending{&n};
    std::unordered_set<std::string> removed;
    while (!pending.empty()) {
        const auto *current = pending.back(); pending.pop_back();
        removed.insert(current->id);
        for (const auto &child : current->children) pending.push_back(&get_node(m, child));
    }
    auto &siblings = mutable_node(m, n.parent).children;
    // No allocation from here onward. String/vector erasure and map erasure
    // cannot throw with these standard value types and hash functions.
    siblings.erase(std::find(siblings.begin(), siblings.end(), id));
    for (auto it = m.links.begin(); it != m.links.end();) {
        if (removed.count(it->second.source) || removed.count(it->second.target)) it = m.links.erase(it);
        else ++it;
    }
    for (const auto &removed_id : removed) m.nodes.erase(removed_id);
}
void add_link(Model &m, Link l) {
    require(!m.links.count(l.id), M3_ERR_ALREADY_EXISTS, "Link already exists");
    endpoints(m, l);
    auto id = l.id;
    m.links.emplace(std::move(id), std::move(l));
}
void update_link(Model &m, const std::string &id, const Json &patch) {
    auto it = m.links.find(id);
    require(it != m.links.end(), M3_ERR_NOT_FOUND, "Link not found");
    auto l = patch_link(it->second, patch);
    endpoints(m, l);
    static_assert(std::is_nothrow_swappable_v<Link>);
    using std::swap;
    swap(it->second, l);
}
void remove_link(Model &m, const std::string &id) {
    require(m.links.erase(id) == 1, M3_ERR_NOT_FOUND, "Link not found");
}
}
