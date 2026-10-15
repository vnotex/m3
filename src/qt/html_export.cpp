#include "html_export.h"
#include <QBuffer>
#include <QFontInfo>
#include <QHash>
#include <QUrl>
#include <nlohmann/json.hpp>
#include <cmath>
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
        else if ((c.unicode() < 0x20 && c != u'\t' && c != u'\n') ||
                 c.unicode() == 0xfffe || c.unicode() == 0xffff)
            result += QChar(0xfffd);
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
QString safeUrl(const QString &original) {
    for (const QChar c : original) if (c.category() == QChar::Other_Control) return {};
    const QUrl url(original, QUrl::StrictMode);
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https") &&
                          scheme != QStringLiteral("mailto")) ||
        ((scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) && url.host().isEmpty()))
        return {};
    return url.toString(QUrl::FullyEncoded);
}
QString urlText(const QString &original) {
    const QString display = original.isEmpty() ? QStringLiteral("(empty)") : escaped(original);
    const QString url = safeUrl(original);
    if (url.isEmpty()) return display;
    return QStringLiteral("<a href=\"") + url.toHtmlEscaped() + QStringLiteral("\">") + display + QStringLiteral("</a>");
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
QString number(qreal value) {
    if (!std::isfinite(value)) throw std::runtime_error("Invalid geometry for HTML export");
    return QString::number(value, 'g', 15);
}
QString literal(const QString &value) {
    QString result;
    result.reserve(value.size());
    for (const QChar c : value) {
        switch (c.unicode()) {
        case '&': result += QStringLiteral("&amp;"); break;
        case '<': result += QStringLiteral("&lt;"); break;
        case '>': result += QStringLiteral("&gt;"); break;
        case '"': result += QStringLiteral("&quot;"); break;
        case '\r': result += QStringLiteral("&#13;"); break;
        default:
            result += (c.unicode() < 0x20 && c != u'\n' && c != u'\t') ||
                       c.unicode() == 0xfffe || c.unicode() == 0xffff ? QChar(0xfffd) : c;
        }
    }
    return result;
}
QString attribute(const QString &value) {
    QString result = literal(value);
    result.replace(u'\n', QStringLiteral("&#10;"));
    result.replace(u'\t', QStringLiteral("&#9;"));
    return result;
}
QString cssString(const QString &value) {
    QString result = QStringLiteral("'");
    for (const QChar c : value) {
        if (c.unicode() < 0x20 || c == u'\'' || c == u'"' || c == u'\\' ||
            c == u'<' || c == u'>' || c == u'&')
            result += u'\\' + QString::number(c.unicode(), 16) + u' ';
        else result += c;
    }
    return result + u'\'';
}
QString color(const QColor &value) {
    if (value.alpha() == 255) return value.name(QColor::HexRgb);
    return QStringLiteral("rgba(") + QString::number(value.red()) + u',' + QString::number(value.green()) +
           u',' + QString::number(value.blue()) + u',' + number(value.alphaF()) + u')';
}
QString rectangleStyle(const QRectF &rectangle) {
    return QStringLiteral("left:") + number(rectangle.x()) + QStringLiteral("px;top:") + number(rectangle.y()) +
           QStringLiteral("px;width:") + number(rectangle.width()) + QStringLiteral("px;height:") +
           number(rectangle.height()) + QStringLiteral("px;");
}
QString fontStyle(const QFont &font) {
    const QFontInfo effective(font);
    int weight = effective.weight();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    // Qt 5 uses its 0..99 weight scale; CSS and Qt 6 use the 100..900 scale.
    weight = weight >= QFont::Black ? 900 : weight >= QFont::ExtraBold ? 800 :
             weight >= QFont::Bold ? 700 : weight >= QFont::DemiBold ? 600 :
             weight >= QFont::Medium ? 500 : weight >= QFont::Normal ? 400 :
             weight >= QFont::Light ? 300 : weight >= QFont::ExtraLight ? 200 : 100;
#endif
    return QStringLiteral("font-family:") + cssString(effective.family()) + QStringLiteral(";font-size:") +
           QString::number(effective.pixelSize()) + QStringLiteral("px;font-weight:") + QString::number(weight) +
           QStringLiteral(";font-style:") + (effective.italic() ? QStringLiteral("italic;") : QStringLiteral("normal;"));
}
void textBlock(QString &out, const HtmlText &text, const QString &className,
               const QPointF &offset = {}, const QString &attributes = {}) {
    const QString style = rectangleStyle(text.rectangle.translated(-offset)) + fontStyle(text.font) +
        QStringLiteral("color:") + color(text.color) + QStringLiteral(";tab-size:") + number(text.tabStopDistance) +
        QStringLiteral("px;");
    out += QStringLiteral("<div class=\"m3-text ") + className + QStringLiteral("\"") + attributes +
           QStringLiteral(" style=\"") + attribute(style) + QStringLiteral("\">");
    for (const auto &line : text.lines) {
        out += QStringLiteral("<span class=\"m3-text-line\" dir=\"") +
               (line.direction == Qt::RightToLeft ? QStringLiteral("rtl") : QStringLiteral("ltr")) +
               QStringLiteral("\" style=\"") + rectangleStyle(line.rectangle) + QStringLiteral("line-height:") + number(line.rectangle.height()) +
               QStringLiteral("px;\">") + literal(line.text) + QStringLiteral("</span>");
    }
    out += QStringLiteral("</div>\n");
}
QString pathData(const QPainterPath &path, const QPointF &origin) {
    QString result;
    auto point = [&](const QPainterPath::Element &element) {
        return number(element.x - origin.x()) + u' ' + number(element.y - origin.y());
    };
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        if (element.type == QPainterPath::MoveToElement) result += QStringLiteral("M ") + point(element) + u' ';
        else if (element.type == QPainterPath::LineToElement) result += QStringLiteral("L ") + point(element) + u' ';
        else if (element.type == QPainterPath::CurveToElement) {
            result += QStringLiteral("C ") + point(element) + u' ' + point(path.elementAt(i + 1)) +
                      u' ' + point(path.elementAt(i + 2)) + u' ';
            i += 2;
        }
    }
    return result;
}
QString stroke(const QPen &pen) {
    const qreal width = pen.widthF() == 0 ? 1 : pen.widthF();
    const QString cap = pen.capStyle() == Qt::RoundCap ? QStringLiteral("round")
        : pen.capStyle() == Qt::FlatCap ? QStringLiteral("butt") : QStringLiteral("square");
    const QString join = pen.joinStyle() == Qt::RoundJoin ? QStringLiteral("round")
        : pen.joinStyle() == Qt::BevelJoin ? QStringLiteral("bevel") : QStringLiteral("miter");
    QString result = QStringLiteral(" fill=\"none\" stroke=\"") + color(pen.color()) +
        QStringLiteral("\" stroke-width=\"") + number(width) + QStringLiteral("\" stroke-linecap=\"") + cap +
        QStringLiteral("\" stroke-linejoin=\"") + join + QStringLiteral("\" stroke-miterlimit=\"") +
        number(pen.miterLimit() * 2) + u'"';
    if (pen.style() != Qt::SolidLine && pen.style() != Qt::NoPen) {
        result += QStringLiteral(" stroke-dasharray=\"");
        const auto pattern = pen.dashPattern();
        for (int i = 0; i < pattern.size(); ++i) {
            if (i) result += u' ';
            result += number(pattern[i] * width);
        }
        result += QStringLiteral("\" stroke-dashoffset=\"") + number(pen.dashOffset() * width) + u'"';
    }
    return result;
}
void arrow(QString &out, const QPolygonF &polygon, const QPointF &origin, const QColor &fill, const char *end) {
    if (polygon.isEmpty()) return;
    out += QStringLiteral("<polygon class=\"m3-link-arrow\" data-end=\"") + QString::fromLatin1(end) +
           QStringLiteral("\" fill=\"") + color(fill) + QStringLiteral("\" points=\"");
    for (const auto &point : polygon)
        out += number(point.x() - origin.x()) + u',' + number(point.y() - origin.y()) + u' ';
    out += QStringLiteral("\"/>\n");
}
void visualMap(QString &out, const HtmlMap &map) {
    if (map.bounds.width() <= 0 || map.bounds.height() <= 0)
        throw std::runtime_error("Invalid map bounds for HTML export");
    const QPointF origin = map.bounds.topLeft();
    const QString width = number(map.bounds.width()), height = number(map.bounds.height());
    const QString size = QStringLiteral("width:") + width + QStringLiteral("px;height:") + height + QStringLiteral("px;");
    out += QStringLiteral("<div id=\"map-viewport\" style=\"") + size + QStringLiteral("\">\n") +
           QStringLiteral("<div id=\"mindmap\" data-width=\"") + width + QStringLiteral("\" data-height=\"") + height +
           QStringLiteral("\" style=\"left:0px;top:0px;") + size + QStringLiteral("background:") + color(map.palette.color(QPalette::Base)) +
           QStringLiteral(";\">\n<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"m3-connectors\" aria-hidden=\"true\" width=\"") +
           width + QStringLiteral("\" height=\"") + height + QStringLiteral("\" viewBox=\"0 0 ") + width + u' ' + height + QStringLiteral("\">\n");
    for (const auto &edge : map.treeEdges) {
        out += QStringLiteral("<path class=\"m3-tree-edge\" data-source=\"") + attribute(edge.source) +
               QStringLiteral("\" data-target=\"") + attribute(edge.target) + QStringLiteral("\" d=\"") +
               pathData(edge.path, origin) + u'"' + stroke(edge.pen) + QStringLiteral("/>\n");
    }
    for (const auto &link : map.links) {
        out += QStringLiteral("<g class=\"m3-cross-link\" data-link-id=\"") + attribute(link.id) +
               QStringLiteral("\" data-source=\"") + attribute(link.source) + QStringLiteral("\" data-target=\"") +
               attribute(link.target) + QStringLiteral("\">\n<path class=\"m3-link-curve\" d=\"") +
               pathData(link.curve, origin) + u'"' + stroke(link.pen) + QStringLiteral("/>\n");
        if (!link.leader.isEmpty())
            out += QStringLiteral("<path class=\"m3-link-leader\" d=\"") + pathData(link.leader, origin) + u'"' +
                   stroke(link.leaderPen) + QStringLiteral("/>\n");
        arrow(out, link.sourceArrow, origin, link.pen.color(), "source");
        arrow(out, link.targetArrow, origin, link.pen.color(), "target");
        out += QStringLiteral("</g>\n");
    }
    out += QStringLiteral("</svg>\n");
    for (const auto &link : map.links) {
        if (link.label) textBlock(out, *link.label, QStringLiteral("m3-link-label"), origin,
            QStringLiteral(" data-link-id=\"") + attribute(link.id) + u'"');
    }
    QHash<qint64, QString> images;
    const QString base = color(map.palette.color(QPalette::Base));
    const QString mid = color(map.palette.color(QPalette::Mid));
    for (const auto &node : map.nodes) {
        out += QStringLiteral("<div class=\"m3-node\" id=\"m3-map-node-") + QString::fromLatin1(node.id.toUtf8().toHex()) +
               QStringLiteral("\" data-node-id=\"") + attribute(node.id) + QStringLiteral("\" style=\"") +
               rectangleStyle(node.rectangle.translated(-origin)) + QStringLiteral("background:") + color(node.background) +
               QStringLiteral(";outline:1px solid ") + color(node.border) + QStringLiteral(";\">\n");
        if (node.icons) textBlock(out, *node.icons, QStringLiteral("m3-icons"));
        textBlock(out, node.topic, QStringLiteral("m3-topic"));
        for (const auto &tag : node.tags) {
            out += QStringLiteral("<div class=\"m3-tag\" style=\"") + rectangleStyle(tag.rectangle) +
                   QStringLiteral("background:") + base + QStringLiteral(";outline:1px solid ") + mid + QStringLiteral(";\">\n");
            textBlock(out, tag.text, QStringLiteral("m3-tag-text"));
            out += QStringLiteral("</div>\n");
        }
        if (node.image) {
            const auto &image = *node.image;
            if (image.pixels.isNull()) {
                out += QStringLiteral("<div class=\"m3-image-placeholder\" data-image-state=\"") +
                       (image.failed ? QStringLiteral("unavailable") : QStringLiteral("loading")) +
                       QStringLiteral("\" style=\"") + rectangleStyle(image.rectangle) + QStringLiteral("background:") +
                       color(map.palette.color(QPalette::AlternateBase)) + QStringLiteral(";outline:1px solid ") + mid + QStringLiteral(";\">\n");
                textBlock(out, image.placeholder, QStringLiteral("m3-image-message"));
                out += QStringLiteral("</div>\n");
            } else {
                auto found = images.constFind(image.pixels.cacheKey());
                if (found == images.constEnd()) {
                    QByteArray png;
                    QBuffer buffer(&png);
                    if (!buffer.open(QIODevice::WriteOnly) || !image.pixels.save(&buffer, "PNG"))
                        throw std::runtime_error("Could not encode HTML node image");
                    found = images.insert(image.pixels.cacheKey(), QString::fromLatin1(png.toBase64()));
                }
                out += QStringLiteral("<img class=\"m3-node-image\" alt=\"\" style=\"") + rectangleStyle(image.rectangle) +
                       QStringLiteral("\" src=\"data:image/png;base64,") + found.value() + QStringLiteral("\"/>\n");
            }
        }
        if (!node.hyperlink.isEmpty()) {
            const QString url = safeUrl(node.hyperlink);
            const QString element = url.isEmpty() ? QStringLiteral("span") : QStringLiteral("a");
            out += u'<' + element + QStringLiteral(" class=\"m3-node-link\" title=\"") + attribute(node.hyperlink) +
                   QStringLiteral("\" style=\"") + rectangleStyle(node.linkRectangle) + u'"';
            if (!url.isEmpty()) out += QStringLiteral(" href=\"") + attribute(url) + u'"';
            out += QStringLiteral(">&#8599;</") + element + QStringLiteral(">\n");
        }
        if (node.hasChildren) {
            out += QStringLiteral("<span class=\"m3-fold-indicator\" aria-hidden=\"true\" style=\"") +
                   rectangleStyle(node.foldRectangle) + QStringLiteral("background:") + base + QStringLiteral(";color:") +
                   color(map.palette.color(QPalette::ButtonText)) + QStringLiteral(";outline:1px solid ") +
                   color(map.palette.color(QPalette::ButtonText)) + QStringLiteral(";\">") +
                   (node.expanded ? QStringLiteral("&#8722;") : QStringLiteral("+")) + QStringLiteral("</span>\n");
        }
        out += QStringLiteral("</div>\n");
    }
    out += QStringLiteral("</div>\n</div>\n");
}
static constexpr char stylesheet[] = R"HTML(<style>
body { margin:0; padding:5rem 1rem 2rem; font:1rem/1.5 system-ui,sans-serif; color:#17212b; background:#fff; overflow-wrap:anywhere; }
main { max-width:80rem; margin:auto; }
#view-toggle { position:fixed; top:1rem; left:1rem; z-index:10; min-height:44px; color:#fff; background:#173e67; border:2px solid #173e67; border-radius:.3rem; padding:.5rem 1rem; font:inherit; cursor:pointer; }
#view-toggle:focus-visible { outline:3px solid #c45500; outline-offset:3px; }
[hidden]{display:none !important}
#map-viewport { position:relative; max-width:100%; overflow:auto; }
#mindmap { position:absolute; left:0; top:0; transform-origin:0 0; }
#mindmap, #mindmap * { box-sizing:content-box; overflow-wrap:normal; word-break:normal; }
.m3-connectors { position:absolute; left:0; top:0; z-index:0; pointer-events:none; }
.m3-node { position:absolute; z-index:2; border-radius:8px; outline-offset:-.5px; }
.m3-text, .m3-text-line { position:absolute; margin:0; padding:0; white-space:pre; }
.m3-text-line { display:block; user-select:text; }
.m3-link-label { z-index:1; }
.m3-tag { position:absolute; border-radius:4px; outline-offset:-1px; }
.m3-node-image, .m3-image-placeholder { position:absolute; display:block; margin:0; padding:0; max-width:none; max-height:none; }
.m3-image-placeholder { outline-offset:-1px; overflow:hidden; }
.m3-node-link { position:absolute; font:20px/20px sans-serif; text-align:center; color:#3498db; text-decoration:none; }
.m3-fold-indicator { position:absolute; border-radius:50%; font:12px/12px sans-serif; text-align:center; outline-offset:-.5px; }
ul { padding-inline-start:1.25rem; }
article { scroll-margin-top:5rem; }
article h3 { margin-bottom:.5rem; }
p { margin:.4rem 0; }
a { color:#124e85; }
</style>
)HTML";
static constexpr char switchScript[] = R"HTML(<script id="view-switch-script">
//<![CDATA[
(function () {
    var button = document.getElementById('view-toggle');
    var visual = document.getElementById('visual-map');
    var articles = document.getElementById('articles');
    var viewport = document.getElementById('map-viewport');
    var map = document.getElementById('mindmap');
    var width = Number(map.getAttribute('data-width'));
    var height = Number(map.getAttribute('data-height'));
    function fitMap() {
        if (visual.hidden) return;
        var available = viewport.parentElement.clientWidth;
        if (!(available > 0)) return;
        var scale = Math.min(1, available / width);
        // Ordinary inline styles deliberately yield to the host's !important paper geometry.
        map.style.transform = 'scale(' + scale + ')';
        viewport.style.width = (width * scale) + 'px';
        viewport.style.height = (height * scale) + 'px';
        viewport.style.overflow = 'hidden';
    }
    button.addEventListener('click', function () {
        var showArticles = articles.hidden;
        visual.hidden = showArticles;
        articles.hidden = !showArticles;
        button.textContent = showArticles ? 'Show visual map' : 'Show articles';
        fitMap();
        button.focus({preventScroll: true});
        window.scrollTo(0, 0);
    });
    window.addEventListener('resize', fitMap);
    fitMap();
})();
//]]>
</script>
)HTML";
}
QString encodeHtml(const QByteArray &document, const HtmlMap &map) {
    const auto data = Json::parse(document.constData(), document.constData() + document.size());
    std::unordered_map<std::string, const Json *> index;
    const auto &nodes = data.at("nodes");
    index.reserve(nodes.size());
    for (const auto &node : nodes) index.emplace(node.at("id").get<std::string>(), &node);
    const auto &root = *index.at(data.at("rootId").get<std::string>());
    QString out = QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\"/>\n"
                                 "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n<title>");
    out += escaped(topic(root), false) + QStringLiteral("</title>\n") + QString::fromLatin1(stylesheet);
    out += QStringLiteral("</head>\n<body>\n<button type=\"button\" id=\"view-toggle\" aria-controls=\"visual-map articles\">Show articles</button>\n"
                          "<main>\n<h1>") + escaped(topic(root)) + QStringLiteral("</h1>\n"
                          "<section id=\"visual-map\">\n<h2>Visual map</h2>\n");
    visualMap(out, map);
    out += QStringLiteral("<p>Selectable visual map. Full node details, including collapsed descendants, are available in articles.</p>\n"
                          "</section>\n<section id=\"articles\" hidden=\"hidden\">\n<h2>Articles</h2>\n<ul>\n");
    struct Frame { const Json *node; bool close; };
    std::vector<Frame> pending;
    const auto orphans = data.find("orphanIds");
    if (orphans != data.end()) {
        pending.reserve(orphans->size() + 1);
        for (auto it = orphans->rbegin(); it != orphans->rend(); ++it)
            pending.push_back({index.at(it->get<std::string>()), false});
    }
    pending.push_back({&root, false});
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
            const auto &style = link.at("style");
            const auto arrow = style.find("arrowDirection");
            out += !link.at("directed").get<bool>() ? QString::fromUtf8(" ↔ ")
                : arrow != style.end() && *arrow == "backward" ? QString::fromUtf8(" ← ")
                : arrow != style.end() && *arrow == "both" ? QString::fromUtf8(" ⇄ ") : QString::fromUtf8(" → ");
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
