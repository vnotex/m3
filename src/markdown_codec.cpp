#include "markdown_codec.h"
#include <string_view>
namespace m3 {
namespace {
void append_text(std::string &out, std::string_view text) {
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
void append_note(std::string &out, size_t indent, std::string_view note) {
    bool paragraph = false;
    while (!note.empty()) {
        const auto end = note.find_first_of("\r\n");
        auto line = note.substr(0, end);
        if (end == std::string_view::npos) note = {};
        else {
            const auto newline = note[end];
            note.remove_prefix(end + 1);
            if (newline == '\r' && !note.empty() && note.front() == '\n') note.remove_prefix(1);
        }
        if (line.find_first_not_of(" \t") == std::string_view::npos) {
            if (paragraph) out += "\n\n";
            paragraph = false;
            continue;
        }
        if (paragraph) out += "\\\n";
        out.append(indent, ' ');
        // Authored indentation is text, not a Markdown code block or nested list.
        while (!line.empty() && line.front() == ' ') {
            out += "&#32;";
            line.remove_prefix(1);
        }
        append_text(out, line);
        paragraph = true;
    }
    if (paragraph) out += "\n\n";
}
void append_values(std::string &out, size_t indent, const std::vector<std::string> &values,
                   const char *separator) {
    bool first = true;
    for (const auto &value : values) {
        if (value.empty()) continue;
        if (first) out.append(indent, ' ');
        else out += separator;
        append_text(out, value);
        first = false;
    }
    if (!first) out += "\n\n";
}
void append_content(std::string &out, size_t indent, const Attributes &attrs) {
    append_values(out, indent, attrs.icons, " ");
    append_values(out, indent, attrs.tags, ", ");
    if (!attrs.hyperlink.empty()) {
        out.append(indent, ' ');
        out += '[';
        append_text(out, attrs.hyperlink);
        out += "](<";
        append_destination(out, attrs.hyperlink);
        out += ">)\n\n";
    }
    append_note(out, indent, attrs.note);
    if (attrs.image && !attrs.image->url.empty()) {
        out.append(indent, ' ');
        out += "![";
        append_topic(out, attrs.topic);
        out += "](<";
        append_destination(out, attrs.image->url);
        out += ">)\n\n";
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
    std::vector<Frame> pending;
    pending.reserve(model.orphans.size() + 1);
    for (auto it = model.orphans.rbegin(); it != model.orphans.rend(); ++it)
        pending.push_back({&get_node(model, *it), 0});
    pending.push_back({&get_node(model, model.root), 0});
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
        append_content(out, indent, node.attrs);
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
            if (!link->icon.empty() || !link->topic.empty()) {
                out += " — ";
                append_text(out, link->icon);
                if (!link->icon.empty() && !link->topic.empty()) out += ' ';
                append_text(out, link->topic);
            }
            out += "\n\n";
        }
    }
    out.pop_back();
    return out;
}
}
