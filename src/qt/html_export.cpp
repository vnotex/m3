#include "html_export.h"
#include <QBuffer>
#include <QUrl>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace m3::qt {
namespace {
using Json = nlohmann::json;
QString string(const Json &value) { return QString::fromStdString(value.get<std::string>()); }
QString escaped(QString text, bool lineBreaks = true) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(u'\r', u'\n');
    if (!lineBreaks) text = text.simplified();
    text = text.toHtmlEscaped();
    QString result;
    result.reserve(text.size());
    for (const QChar c : text) {
        if (c == u'\n' && lineBreaks) result += QStringLiteral("<br/>");
        else if ((c.unicode() < 0x20 && c != u'\t' && c != u'\n') || c.unicode() == 0x7f)
            result += QStringLiteral("&#") + QString::number(c.unicode()) + u';';
        else result += c;
    }
    return result;
}
QString topic(const Json &node) {
    const auto value = string(node.at("topic"));
    return value.isEmpty() ? QStringLiteral("(untitled)") : value;
}
QString anchor(const Json &id) {
    return QStringLiteral("m3-node-") + QString::fromLatin1(string(id).toUtf8().toHex());
}
QString urlText(const QString &original) {
    const QString display = original.isEmpty() ? QStringLiteral("(empty)") : escaped(original);
    for (const QChar c : original) if (c.category() == QChar::Other_Control) return display;
    const QUrl url(original, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https") &&
                          scheme != QStringLiteral("mailto")) ||
        ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) && url.host().isEmpty()))
        return display;
    return QStringLiteral("<a href=\"") + url.toString(QUrl::FullyEncoded).toHtmlEscaped() +
           QStringLiteral("\">") + display + QStringLiteral("</a>");
}
void metadata(QString &out, const char *label, const QString &value, bool url = false) {
    out += QStringLiteral("<p><strong>") + QString::fromLatin1(label) + QStringLiteral(":</strong> ");
    out += url ? urlText(value) : (value.isEmpty() ? QStringLiteral("(empty)") : escaped(value));
    out += QStringLiteral("</p>\n");
}
void endpoint(QString &out, const Json &node) {
    out += QStringLiteral("<a href=\"#") + anchor(node.at("id")) + QStringLiteral("\">") +
           escaped(topic(node)) + QStringLiteral("</a>");
}
static constexpr char stylesheet[] = R"HTML(<style>
body { margin:0; padding:5rem 1rem 2rem; font:1rem/1.5 system-ui,sans-serif; color:#17212b; background:#fff; overflow-wrap:anywhere; }
main { max-width:80rem; margin:auto; }
#view-toggle { position:fixed; top:1rem; left:1rem; z-index:10; min-height:44px; color:#fff; background:#173e67; border:2px solid #173e67; border-radius:.3rem; padding:.5rem 1rem; font:inherit; cursor:pointer; }
#view-toggle:focus-visible { outline:3px solid #c45500; outline-offset:3px; }
[hidden]{display:none !important}
#visual-map img { max-width:100%; height:auto; }
ul { padding-inline-start:1.25rem; }
article { scroll-margin-top:5rem; }
article h3 { margin-bottom:.5rem; }
p { margin:.4rem 0; }
a { color:#124e85; }
</style>
)HTML";
static constexpr char switchScript[] = R"HTML(<script id="view-switch-script">
(() => {
    const button = document.getElementById('view-toggle');
    const visual = document.getElementById('visual-map');
    const articles = document.getElementById('articles');
    button.addEventListener('click', () => {
        const showArticles = articles.hidden;
        visual.hidden = showArticles;
        articles.hidden = !showArticles;
        button.textContent = showArticles ? 'Show visual map' : 'Show articles';
        button.focus({preventScroll: true});
        window.scrollTo(0, 0);
    });
})();
</script>
)HTML";
}
QString encodeHtml(const QByteArray &document, const QImage &mapImage) {
    const auto data = Json::parse(document.constData(), document.constData() + document.size());
    std::unordered_map<std::string, const Json *> index;
    const auto &nodes = data.at("nodes");
    index.reserve(nodes.size());
    for (const auto &node : nodes) index.emplace(node.at("id").get<std::string>(), &node);
    const auto &root = *index.at(data.at("rootId").get<std::string>());
    QByteArray png;
    QBuffer buffer(&png);
    if (mapImage.isNull() || !buffer.open(QIODevice::WriteOnly) || !mapImage.save(&buffer, "PNG"))
        throw std::runtime_error("Could not encode HTML map image");
    QString out = QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\"/>\n"
                                 "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n<title>");
    out += escaped(topic(root), false) + QStringLiteral("</title>\n") + QString::fromLatin1(stylesheet);
    out += QStringLiteral("</head>\n<body>\n<button type=\"button\" id=\"view-toggle\" aria-controls=\"visual-map articles\">Show articles</button>\n"
                          "<main>\n<h1>") + escaped(topic(root)) + QStringLiteral("</h1>\n"
                          "<section id=\"visual-map\">\n<h2>Visual map</h2>\n<img alt=\"Mind map\" src=\"data:image/png;base64,");
    out += QString::fromLatin1(png.toBase64());
    out += QStringLiteral("\" width=\"") + QString::number(mapImage.width()) + QStringLiteral("\" height=\"") +
           QString::number(mapImage.height()) + QStringLiteral("\"/>\n"
           "<p>Static snapshot of the visible map. Switch to articles to read all nodes, including collapsed descendants.</p>\n"
           "</section>\n<section id=\"articles\" hidden=\"hidden\">\n<h2>Articles</h2>\n<ul>\n");
    struct Frame { const Json *node; bool close; };
    std::vector<Frame> pending{{&root, false}};
    while (!pending.empty()) {
        const auto frame = pending.back();
        pending.pop_back();
        const auto &node = *frame.node;
        const auto &children = node.at("children");
        if (frame.close) {
            if (!children.empty()) out += QStringLiteral("</ul>\n");
            out += QStringLiteral("</li>\n");
            continue;
        }
        out += QStringLiteral("<li><article id=\"") + anchor(node.at("id")) + QStringLiteral("\"><h3>") +
               escaped(topic(node)) + QStringLiteral("</h3>\n");
        const auto note = string(node.at("note")), url = string(node.at("hyperLink"));
        if (!note.isEmpty()) metadata(out, "Note", note);
        if (!url.isEmpty()) metadata(out, "URL", url, true);
        for (const auto &tag : node.at("tags")) metadata(out, "Tag", string(tag));
        for (const auto &icon : node.at("icons")) metadata(out, "Icon", string(icon));
        const auto &image = node.at("image");
        if (!image.is_null()) {
            metadata(out, "Image URL", string(image.at("url")), true);
            metadata(out, "Image size", QString::fromStdString(image.at("width").dump()) +
                     QString::fromUtf8(" × ") + QString::fromStdString(image.at("height").dump()));
        }
        out += QStringLiteral("</article>\n");
        pending.push_back({&node, true});
        if (!children.empty()) out += QStringLiteral("<ul>\n");
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            pending.push_back({index.at(it->get<std::string>()), false});
    }
    out += QStringLiteral("</ul>\n");
    const auto &links = data.at("crossLinks");
    if (!links.empty()) {
        out += QStringLiteral("<section><h3>Cross-links</h3>\n<ul>\n");
        for (const auto &link : links) {
            out += QStringLiteral("<li>");
            endpoint(out, *index.at(link.at("source").get<std::string>()));
            out += link.at("directed").get<bool>() ? QString::fromUtf8(" → ") : QString::fromUtf8(" ↔ ");
            endpoint(out, *index.at(link.at("target").get<std::string>()));
            out += u'\n';
            metadata(out, "ID", string(link.at("id")));
            const auto label = string(link.at("topic")), icon = string(link.at("icon"));
            if (!label.isEmpty()) metadata(out, "Label", label);
            if (!icon.isEmpty()) metadata(out, "Icon", icon);
            out += QStringLiteral("</li>\n");
        }
        out += QStringLiteral("</ul>\n</section>\n");
    }
    out += QStringLiteral("</section>\n</main>\n") + QString::fromLatin1(switchScript) + QStringLiteral("</body>\n</html>\n");
    return out;
}
}
