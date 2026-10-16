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
                   const char *prefix) {
    bool first = true;
    for (const auto &value : values) {
        if (value.empty()) continue;
        if (first) out.append(indent, ' ');
        else out += ' ';
        out += prefix;
        append_text(out, value);
        first = false;
    }
    if (!first) out += "\n\n";
}
struct Reference { const Node *node; const Link *link; };
void append_title(std::string &out, const std::string &title) {
    for (unsigned char byte : title) {
        switch (byte) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\\': out += "\\\\"; break;
        default:
            if (byte < 0x20 || byte == 0x7f) {
                out += "&#";
                out += std::to_string(byte);
                out += ';';
            } else out += static_cast<char>(byte);
        }
    }
}
void append_reference(std::string &out, const Reference &reference) {
    out += '[';
    if (reference.link->icon.find_first_not_of(" \t\r\n\f\v") != std::string::npos) {
        append_text(out, reference.link->icon);
        out += ' ';
    }
    out += "↗ ";
    append_topic(out, reference.node->attrs.topic);
    out += "](#";
    append_anchor(out, reference.node->id);
    if (!reference.link->topic.empty()) {
        out += " \"";
        append_title(out, reference.link->topic);
        out += '"';
    }
    out += ')';
}
void append_content(std::string &out, size_t indent, const Attributes &attrs,
                    const std::vector<Reference> *references) {
    append_values(out, indent, attrs.icons, "");
    append_values(out, indent, attrs.tags, "\\#");
    if (!attrs.hyperlink.empty()) {
        out.append(indent, ' ');
        out += '[';
        append_text(out, attrs.hyperlink);
        out += "](<";
        append_destination(out, attrs.hyperlink);
        out += ">)\n\n";
    }
    if (references) {
        out.append(indent, ' ');
        bool first = true;
        for (const auto &reference : *references) {
            if (!first) out += ' ';
            append_reference(out, reference);
            first = false;
        }
        out += "\n\n";
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
}
std::string encode_markdown(const Model &model) {
    std::unordered_map<const Node *, std::vector<Reference>> references;
    for (const auto *link : sorted_links(model)) {
        const auto *source = &get_node(model, link->source);
        const auto *target = &get_node(model, link->target);
        const auto arrow = link->style.find("arrowDirection");
        const bool both = !link->directed || (arrow != link->style.end() && *arrow == "both");
        const bool backward = !both && arrow != link->style.end() && *arrow == "backward";
        if (!backward) references[source].push_back({target, link});
        if (backward || (both && source != target)) references[target].push_back({source, link});
    }
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
        const auto related = references.find(&node);
        append_content(out, indent, node.attrs, related == references.end() ? nullptr : &related->second);
        for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
            pending.push_back({&get_node(model, *it), frame.depth + 1});
    }
    out.pop_back();
    return out;
}
}
