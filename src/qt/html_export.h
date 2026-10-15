#ifndef M3_QT_HTML_EXPORT_H
#define M3_QT_HTML_EXPORT_H
#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <optional>
#include <vector>

namespace m3::qt {
struct HtmlTextLine {
    QString text;
    QRectF rectangle;
    Qt::LayoutDirection direction = Qt::LeftToRight;
};
struct HtmlText {
    QRectF rectangle;
    QFont font;
    QColor color;
    qreal tabStopDistance = 80;
    std::vector<HtmlTextLine> lines;
};
struct HtmlTag {
    QRectF rectangle;
    HtmlText text;
};
struct HtmlImage {
    QRectF rectangle;
    QImage pixels;
    bool failed = false;
    HtmlText placeholder;
};
struct HtmlNode {
    QString id, hyperlink;
    QRectF rectangle, linkRectangle, foldRectangle;
    QColor background, border;
    HtmlText topic;
    std::optional<HtmlText> icons;
    std::vector<HtmlTag> tags;
    std::optional<HtmlImage> image;
    bool hasChildren = false, expanded = true;
};
struct HtmlTreeEdge {
    QString source, target;
    QPainterPath path;
    QPen pen;
};
struct HtmlLink {
    QString id, source, target;
    QPainterPath curve, leader;
    QPen pen, leaderPen;
    QPolygonF sourceArrow, targetArrow;
    std::optional<HtmlText> label;
};
struct HtmlArticleImage {
    QImage pixels;
    bool failed = false;
};
// Scene coordinates until serialization; child text/image/tag rectangles are node-local.
struct HtmlMap {
    QRectF bounds;
    QPalette palette;
    std::vector<HtmlNode> nodes;
    std::vector<HtmlTreeEdge> treeEdges;
    std::vector<HtmlLink> links;
    QHash<QString, HtmlArticleImage> articleImages;
};
QString encodeHtml(const QByteArray &document, const HtmlMap &map);
}
#endif
