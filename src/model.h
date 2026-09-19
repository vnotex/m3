#ifndef M3_MODEL_H
#define M3_MODEL_H
#include "m3/m3.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
namespace m3 {
using Json = nlohmann::json;
struct Failure { M3Status status; const char *message; };
inline void require(bool condition, M3Status status, const char *message) {
    if (!condition) throw Failure{status, message};
}
struct Image { std::string url; double width = 0, height = 0; };
struct Attributes {
    std::string topic, hyperlink, note;
    Json style = Json::object();
    bool expanded = true;
    std::vector<std::string> tags, icons;
    std::optional<Image> image;
};
struct Node {
    std::string id, parent;
    Attributes attrs;
    std::vector<std::string> children;
};
struct Link {
    std::string id, source, target;
    bool directed = false;
    std::string topic, icon;
    Json style = Json::object();
};
struct Model {
    std::string root;
    std::unordered_map<std::string, Node> nodes;
    std::unordered_map<std::string, Link> links;
};
void insert_node(Model &m, const std::string &parent, size_t index, Node node);
void update_node(Model &m, const std::string &id, const Json &patch);
void move_node(Model &m, const std::string &id, const std::string &parent, size_t index);
void remove_subtree(Model &m, const std::string &id);
void add_link(Model &m, Link link);
void update_link(Model &m, const std::string &id, const Json &patch);
void remove_link(Model &m, const std::string &id);
bool valid_utf8(const char *text) noexcept;
const Node &get_node(const Model &model, const std::string &id);
const Link &get_link(const Model &model, const std::string &id);
std::vector<const Node *> preorder(const Model &model, bool visible = false);
std::vector<const Link *> sorted_links(const Model &model);
}
#endif
