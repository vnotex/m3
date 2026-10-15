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
    const QString display = QStringLiteral("<span class=\"m3-article-url-text\">") + escaped(original) + QStringLiteral("</span>");
    const QString url = safeUrl(original);
    if (url.isEmpty()) return display;
    return QStringLiteral("<a href=\"") + url.toHtmlEscaped() + QStringLiteral("\"><span class=\"m3-article-url-icon\" aria-hidden=\"true\">&#8599;</span>") +
           display + QStringLiteral("</a>");
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
               const QPointF &offset = {}, const QString &attributes = {}, bool topicLink = false) {
    const QString style = rectangleStyle(text.rectangle.translated(-offset)) + fontStyle(text.font) +
        QStringLiteral("color:") + color(text.color) + QStringLiteral(";tab-size:") + number(text.tabStopDistance) +
        QStringLiteral("px;");
    const QString element = topicLink ? QStringLiteral("a") : QStringLiteral("div");
    out += u'<' + element + QStringLiteral(" class=\"m3-text ") + className + QStringLiteral("\"") + attributes +
           QStringLiteral(" style=\"") + attribute(style) + QStringLiteral("\">");
    for (const auto &line : text.lines) {
        out += QStringLiteral("<span class=\"m3-text-line\" dir=\"") +
               (line.direction == Qt::RightToLeft ? QStringLiteral("rtl") : QStringLiteral("ltr")) +
               QStringLiteral("\" style=\"") + rectangleStyle(line.rectangle) + QStringLiteral("line-height:") + number(line.rectangle.height()) +
               QStringLiteral("px;\">") + literal(line.text) + QStringLiteral("</span>");
    }
    out += QStringLiteral("</") + element + QStringLiteral(">\n");
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
const QString &imageData(QHash<qint64, QString> &images, const QImage &pixels) {
    auto found = images.constFind(pixels.cacheKey());
    if (found == images.constEnd()) {
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !pixels.save(&buffer, "PNG"))
            throw std::runtime_error("Could not encode HTML node image");
        found = images.insert(pixels.cacheKey(), QString::fromLatin1(png.toBase64()));
    }
    return found.value();
}
void visualMap(QString &out, const HtmlMap &map, QHash<qint64, QString> &images) {
    if (map.bounds.width() <= 0 || map.bounds.height() <= 0)
        throw std::runtime_error("Invalid map bounds for HTML export");
    const QPointF origin = map.bounds.topLeft();
    const QString width = number(map.bounds.width()), height = number(map.bounds.height());
    const QString size = QStringLiteral("width:") + width + QStringLiteral("px;height:") + height + QStringLiteral("px;");
    out += QStringLiteral("<div id=\"mindmap\" data-width=\"") + width + QStringLiteral("\" data-height=\"") + height +
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
    const QString base = color(map.palette.color(QPalette::Base));
    const QString mid = color(map.palette.color(QPalette::Mid));
    for (const auto &node : map.nodes) {
        out += QStringLiteral("<div class=\"m3-node\" id=\"m3-map-node-") + QString::fromLatin1(node.id.toUtf8().toHex()) +
               QStringLiteral("\" data-node-id=\"") + attribute(node.id) + QStringLiteral("\" style=\"") +
               rectangleStyle(node.rectangle.translated(-origin)) + QStringLiteral("background:") + color(node.background) +
               QStringLiteral(";outline:1px solid ") + color(node.border) + QStringLiteral(";\">\n");
        if (node.icons) textBlock(out, *node.icons, QStringLiteral("m3-icons"));
        textBlock(out, node.topic, QStringLiteral("m3-topic"), {},
                  QStringLiteral(" href=\"#m3-node-") + QString::fromLatin1(node.id.toUtf8().toHex()) +
                  QStringLiteral("\" draggable=\"false\""), true);
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
                out += QStringLiteral("<img class=\"m3-node-image\" alt=\"\" style=\"") + rectangleStyle(image.rectangle) +
                       QStringLiteral("\" src=\"data:image/png;base64,") + imageData(images, image.pixels) + QStringLiteral("\"/>\n");
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
    out += QStringLiteral("</div>\n");
}
void articleValues(QString &out, const Json &values, const QString &group, const QString &item) {
    bool opened = false;
    for (const auto &value : values) {
        const QString text = string(value);
        if (text.trimmed().isEmpty()) continue;
        if (!opened) {
            out += QStringLiteral("<div class=\"") + group + QStringLiteral("\">");
            opened = true;
        }
        out += QStringLiteral("<span class=\"") + item + QStringLiteral("\">") + escaped(text) + QStringLiteral("</span>");
    }
    if (opened) out += QStringLiteral("</div>\n");
}
void articleNote(QString &out, QString note) {
    if (note.trimmed().isEmpty()) return;
    note.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    note.replace(u'\r', u'\n');
    out += QStringLiteral("<div class=\"m3-article-note\">");
    QString paragraph;
    const auto flush = [&] {
        if (!paragraph.isEmpty()) out += QStringLiteral("<p>") + escaped(paragraph) + QStringLiteral("</p>\n");
        paragraph.clear();
    };
    const auto lines = note.split(u'\n');
    for (const auto &line : lines) {
        if (line.trimmed().isEmpty()) flush();
        else {
            if (!paragraph.isEmpty()) paragraph += u'\n';
            paragraph += line;
        }
    }
    flush();
    out += QStringLiteral("</div>\n");
}
static constexpr char stylesheet[] = R"HTML(<style>
body { margin:0; padding:1rem 1rem 2rem; font:1rem/1.5 system-ui,sans-serif; color:#17212b; background:#fff; overflow-wrap:anywhere; }
.m3-topic:focus-visible { outline:2px solid #c45500; outline-offset:2px; }
#mindmap { position:relative; }
#mindmap, #mindmap * { box-sizing:content-box; overflow-wrap:normal; word-break:normal; }
.m3-connectors { position:absolute; left:0; top:0; z-index:0; pointer-events:none; }
.m3-node { position:absolute; z-index:2; border-radius:8px; outline-offset:-.5px; }
.m3-text, .m3-text-line { position:absolute; margin:0; padding:0; white-space:pre; }
.m3-text-line { display:block; user-select:text; }
.m3-topic { text-decoration:none; }
.m3-link-label { z-index:1; }
.m3-tag { position:absolute; border-radius:4px; outline-offset:-1px; }
.m3-node-image, .m3-image-placeholder { position:absolute; display:block; margin:0; padding:0; max-width:none; max-height:none; }
.m3-image-placeholder { outline-offset:-1px; overflow:hidden; }
.m3-node-link { position:absolute; font:20px/20px sans-serif; text-align:center; color:#3498db; text-decoration:none; }
.m3-fold-indicator { position:absolute; border-radius:50%; font:12px/12px sans-serif; text-align:center; outline-offset:-.5px; }
#map-articles-separator { margin:2rem 0; border:0; border-top:1px solid #d9e1e8; }
#articles ul { list-style:disc outside; padding:0; margin:0; overflow:visible; }
#articles > ul { padding-left:1.25em; }
article { margin:0; padding:0 0 1rem; scroll-margin-top:1rem; }
.m3-article-title { margin:0 0 .6rem; line-height:1.45; }
h1.m3-article-title { font-size:2em; }
h2.m3-article-title { font-size:1.5em; }
h3.m3-article-title { font-size:1.17em; }
h4.m3-article-title, h5.m3-article-title, h6.m3-article-title { font-size:1em; }
.m3-article-icon + .m3-article-icon { margin-left:.2em; }
.m3-article-references { margin:.75rem 0; }
.m3-cross-reference { display:inline; margin-right:.6em; text-decoration:none; }
.m3-cross-reference:hover { text-decoration:underline; }
.m3-cross-reference-icon { margin-right:.25em; }
.m3-article-tags { margin:.4rem 0 .75rem; }
.m3-article-tag { display:inline-block; margin:.15rem .4rem .15rem 0; padding:.15rem .65rem; border-radius:1rem; background:#edf2f7; color:#36516c; font-size:.85em; }
.m3-article-note p { margin:.75rem 0; }
.m3-article-url { margin:.75rem 0; }
.m3-article-url-icon { margin-right:.25em; }
.m3-article-figure { margin:1rem 0; }
.m3-article-image { display:block; max-width:100%; height:auto; border-radius:.3rem; }
.m3-article-image-placeholder { display:block; padding:1rem; border:1px dashed #bac7d3; border-radius:.3rem; background:#f5f7f9; color:#52667a; }
a { color:#124e85; }
</style>
)HTML";
}
QString encodeHtml(const QByteArray &document, const HtmlMap &map) {
    const auto data = Json::parse(document.constData(), document.constData() + document.size());
    std::unordered_map<std::string, const Json *> index;
    const auto &nodes = data.at("nodes");
    index.reserve(nodes.size());
    for (const auto &node : nodes) index.emplace(node.at("id").get<std::string>(), &node);
    const auto &root = *index.at(data.at("rootId").get<std::string>());
    struct Reference { const Json *node, *link; };
    std::unordered_map<const Json *, std::vector<Reference>> references;
    for (const auto &link : data.at("crossLinks")) {
        const auto *source = index.at(link.at("source").get<std::string>());
        const auto *target = index.at(link.at("target").get<std::string>());
        const auto &style = link.at("style");
        const auto arrow = style.find("arrowDirection");
        const bool both = !link.at("directed").get<bool>() || (arrow != style.end() && *arrow == "both");
        const bool backward = !both && arrow != style.end() && *arrow == "backward";
        if (!backward) references[source].push_back({target, &link});
        if (backward || (both && source != target)) references[target].push_back({source, &link});
    }
    QHash<qint64, QString> images;
    QString out = QStringLiteral("<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\"/>\n"
                                 "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>\n"
                                 "<link rel=\"icon\" type=\"image/png\" href=\"https://raw.githubusercontent.com/vnotex/m3/main/pics/m3.png\"/>\n<title>");
    out += escaped(topic(root), false) + QStringLiteral("</title>\n") + QString::fromLatin1(stylesheet);
    out += QStringLiteral("</head>\n<body>\n<main>\n<h1>") + escaped(topic(root)) +
           QStringLiteral("</h1>\n<section id=\"visual-map\">\n");
    visualMap(out, map, images);
    out += QStringLiteral("</section>\n<hr id=\"map-articles-separator\"/>\n<section id=\"articles\">\n");
    struct Frame { const Json *node; size_t depth; bool close; };
    std::vector<Frame> pending;
    const auto orphans = data.find("orphanIds");
    if (orphans != data.end()) {
        pending.reserve(orphans->size() + 1);
        for (auto it = orphans->rbegin(); it != orphans->rend(); ++it)
            pending.push_back({index.at(it->get<std::string>()), 1, false});
    }
    pending.push_back({&root, 1, false});
    while (!pending.empty()) {
        const auto frame = pending.back();
        pending.pop_back();
        const auto &node = *frame.node;
        const auto &children = node.at("children");
        if (frame.close) {
            if (!children.empty()) out += QStringLiteral("</ul>\n");
            if (frame.depth > 6) out += QStringLiteral("</li>\n");
            continue;
        }
        const auto heading = frame.depth <= 6 ? u'h' + QString::number(static_cast<int>(frame.depth)) : QStringLiteral("p");
        if (frame.depth > 6) out += QStringLiteral("<li>");
        out += QStringLiteral("<article id=\"") + anchor(node.at("id")) + QStringLiteral("\"><") + heading +
               QStringLiteral(" class=\"m3-article-title\"><span class=\"m3-article-topic\">") + escaped(topic(node)) +
               QStringLiteral("</span></") + heading + QStringLiteral(">\n");
        articleValues(out, node.at("icons"), QStringLiteral("m3-article-icons"), QStringLiteral("m3-article-icon"));
        articleValues(out, node.at("tags"), QStringLiteral("m3-article-tags"), QStringLiteral("m3-article-tag"));
        const QString url = string(node.at("hyperLink"));
        if (!url.trimmed().isEmpty()) out += QStringLiteral("<p class=\"m3-article-url\">") + urlText(url) + QStringLiteral("</p>\n");
        const auto related = references.find(&node);
        if (related != references.end()) {
            out += QStringLiteral("<div class=\"m3-article-references\">");
            for (const auto &reference : related->second) {
                out += QStringLiteral("<a class=\"m3-cross-reference\" href=\"#") + anchor(reference.node->at("id")) +
                       QStringLiteral("\" title=\"") + attribute(string(reference.link->at("topic"))) + QStringLiteral("\">");
                const QString icon = string(reference.link->at("icon"));
                if (!icon.trimmed().isEmpty()) out += QStringLiteral("<span class=\"m3-cross-reference-icon\">") + escaped(icon) + QStringLiteral("</span>");
                out += QStringLiteral("<span aria-hidden=\"true\">&#8599; </span>") + escaped(topic(*reference.node)) + QStringLiteral("</a>");
            }
            out += QStringLiteral("</div>\n");
        }
        articleNote(out, string(node.at("note")));
        const auto image = map.articleImages.constFind(string(node.at("id")));
        if (image != map.articleImages.constEnd()) {
            out += QStringLiteral("<figure class=\"m3-article-figure\">");
            if (!image->pixels.isNull()) {
                out += QStringLiteral("<img class=\"m3-article-image\" alt=\"") + attribute(topic(node)) +
                       QStringLiteral("\" src=\"data:image/png;base64,") + imageData(images, image->pixels) + QStringLiteral("\"/>");
            } else {
                out += QStringLiteral("<span class=\"m3-article-image-placeholder\" data-image-state=\"") +
                       (image->failed ? QStringLiteral("unavailable\">Image unavailable") : QStringLiteral("loading\">Image loading")) +
                       QStringLiteral("</span>");
            }
            out += QStringLiteral("</figure>\n");
        }
        out += QStringLiteral("</article>\n");
        if (frame.depth >= 6 && (!children.empty() || frame.depth > 6)) {
            // Close the child list and its owning LI before a sibling or the next root.
            pending.push_back({&node, frame.depth, true});
            if (!children.empty()) out += QStringLiteral("<ul>\n");
        }
        for (auto it = children.rbegin(); it != children.rend(); ++it)
            pending.push_back({index.at(it->get<std::string>()), frame.depth + 1, false});
    }
    out += QStringLiteral("</section>\n</main>\n</body>\n</html>\n");
    return out;
}
}
