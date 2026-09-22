#include "markdown_codec.h"
namespace m3 {
namespace {
void append_text(std::string &out, const std::string &text) {
    for (size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<unsigned char>(text[i]);
        switch (byte) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\\': case '`': case '*': case '_': case '{': case '}':
        case '[': case ']': case '(': case ')': case '#': case '+':
        case '-': case '.': case '!': case '|': case '~':
            out += '\\';
            out += static_cast<char>(byte);
            break;
        case '\r':
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out += "<br>";
            break;
        case '\n': out += "<br>"; break;
        default:
            if (byte < 0x20 || byte == 0x7f) {
                out += "&#";
                out += std::to_string(byte);
                out += ';';
            } else out += static_cast<char>(byte);
        }
    }
}
void append_topic(std::string &out, const std::string &topic) {
    if (topic.empty()) out += "(untitled)";
    else append_text(out, topic);
}
void append_anchor(std::string &out, const std::string &id) {
    static constexpr char hex[] = "0123456789abcdef";
    out += "m3-node-";
    for (unsigned char byte : id) {
        out += hex[byte >> 4];
        out += hex[byte & 0xf];
    }
}
void append_destination(std::string &out, const std::string &url) {
    static constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char byte : url) {
        if (byte <= 0x20 || byte >= 0x7f || byte == '<' || byte == '>' || byte == '"' || byte == '\\') {
            out += '%';
            out += hex[byte >> 4];
            out += hex[byte & 0xf];
        } else if (byte == '&') out += "&amp;";
        else out += static_cast<char>(byte);
    }
}
void append_paragraph(std::string &out, size_t indent, const char *label, const std::string &text) {
    out.append(indent, ' ');
    out += label;
    if (text.empty()) out += "(empty)";
    else append_text(out, text);
    out += "\n\n";
}
void append_metadata(std::string &out, size_t indent, const Attributes &attrs) {
    if (!attrs.note.empty()) append_paragraph(out, indent, "**Note:** ", attrs.note);
    if (!attrs.hyperlink.empty()) {
        out.append(indent, ' ');
        out += "**URL:** [";
        append_text(out, attrs.hyperlink);
        out += "](<";
        append_destination(out, attrs.hyperlink);
        out += ">)\n\n";
    }
    for (const auto &tag : attrs.tags) append_paragraph(out, indent, "**Tag:** ", tag);
    for (const auto &icon : attrs.icons) append_paragraph(out, indent, "**Icon:** ", icon);
    if (attrs.image) {
        const auto &image = *attrs.image;
        out.append(indent, ' ');
        out += "**Image URL:** ";
        if (image.url.empty()) out += "(empty)\n\n";
        else {
            std::string destination;
            append_destination(destination, image.url);
            out += '[';
            append_text(out, image.url);
            out += "](<";
            out += destination;
            out += ">)\n\n";
            out.append(indent, ' ');
            out += "![Image](<";
            out += destination;
            out += ">)\n\n";
        }
        out.append(indent, ' ');
        out += "**Image size:** ";
        out += Json(image.width).dump();
        out += " × ";
        out += Json(image.height).dump();
        out += "\n\n";
    }
}
void append_endpoint(std::string &out, const Model &model, const std::string &id) {
    out += '[';
    append_topic(out, get_node(model, id).attrs.topic);
    out += "](#";
    append_anchor(out, id);
    out += ')';
}
}
std::string encode_markdown(const Model &model) {
    struct Frame { const Node *node; size_t depth; };
    std::vector<Frame> pending{{&get_node(model, model.root), 0}};
    std::string out;
    while (!pending.empty()) {
        const auto frame = pending.back();
        pending.pop_back();
        const auto &node = *frame.node;
        size_t indent = 0;
        if (frame.depth < 6) {
            out.append(frame.depth + 1, '#');
            out += ' ';
        } else {
            out.append((frame.depth - 6) * 4, ' ');
            out += "- ";
            indent = (frame.depth - 5) * 4;
        }
        out += "<a id=\"";
        append_anchor(out, node.id);
        out += "\"></a>";
        append_topic(out, node.attrs.topic);
        out += "\n\n";
        append_metadata(out, indent, node.attrs);
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
            pending.push_back({&get_node(model, *it), frame.depth + 1});
    }
    if (!model.links.empty()) {
        out += "---\n\n## Cross-links\n\n";
        for (const auto *link : sorted_links(model)) {
            out += "- ";
            append_endpoint(out, model, link->source);
            out += link->directed ? " → " : " ↔ ";
            append_endpoint(out, model, link->target);
            out += "\n\n";
            append_paragraph(out, 4, "**ID:** ", link->id);
            if (!link->topic.empty()) append_paragraph(out, 4, "**Label:** ", link->topic);
            if (!link->icon.empty()) append_paragraph(out, 4, "**Icon:** ", link->icon);
        }
    }
    out.pop_back();
    return out;
}
}
