#ifndef M3_QT_PRESENTATION_H
#define M3_QT_PRESENTATION_H
#include <QColor>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTextDocument>
#include <memory>
#include <optional>
#include <vector>

namespace m3::qt {
struct ColorSwatch { const char *hex; const char *name; };
inline constexpr ColorSwatch nodeColors[] = {
    {"#ffffff", QT_TR_NOOP("White")}, {"#ecf0f1", QT_TR_NOOP("Cloud")},
    {"#95a5a6", QT_TR_NOOP("Gray")}, {"#34495e", QT_TR_NOOP("Slate")},
    {"#2c3e50", QT_TR_NOOP("Midnight")}, {"#000000", QT_TR_NOOP("Black")},
    {"#e74c3c", QT_TR_NOOP("Red")}, {"#e67e22", QT_TR_NOOP("Orange")},
    {"#f39c12", QT_TR_NOOP("Amber")}, {"#f1c40f", QT_TR_NOOP("Yellow")},
    {"#2ecc71", QT_TR_NOOP("Green")}, {"#27ae60", QT_TR_NOOP("Forest")},
    {"#1abc9c", QT_TR_NOOP("Teal")}, {"#16a085", QT_TR_NOOP("Jade")},
    {"#3498db", QT_TR_NOOP("Blue")}, {"#2980b9", QT_TR_NOOP("Ocean")},
    {"#9b59b6", QT_TR_NOOP("Purple")}, {"#8e44ad", QT_TR_NOOP("Violet")},
    {"#ffb6c1", QT_TR_NOOP("Pink")}, {"#f4a6a6", QT_TR_NOOP("Rose")},
    {"#ffd3a5", QT_TR_NOOP("Peach")}, {"#a8e6cf", QT_TR_NOOP("Mint")},
    {"#a9d6f5", QT_TR_NOOP("Sky")}
};
// Zero, nullopt and invalid text/fill colors inherit the editor's appearance.
// Both paths parse local style: NodeProperties keeps the parsed branch override;
// only NodePresentation resolves an invalid branch color from its ancestors.
struct NodeStyle {
    qreal fontSize = 0;
    std::optional<bool> bold, italic;
    QColor textColor, backgroundColor, branchColor;
};
struct NodeImage {
    QString url;
    double width = 0, height = 0;
};
struct NodeProperties {
    QString id, topic, hyperlink, note;
    QStringList tags, icons;
    bool root = false;
    std::optional<NodeImage> image;
    NodeStyle style;
};
struct NodeTagPresentation {
    QString value;
    QRectF rectangle;
    std::unique_ptr<QTextDocument> text;
};
struct NodePresentation {
    QString id, topic, hyperlink;
    QStringList icons;
    std::vector<NodeTagPresentation> tags;
    bool root = false, expanded = true, hasChildren = false;
    QRectF rectangle;
    std::optional<NodeImage> image;
    QRectF imageRectangle;
    QImage imagePixels;
    bool imageFailed = false;
    NodeStyle style;
    std::unique_ptr<QTextDocument> text, iconText;
};
enum class LinkDirection { None, Backward, Forward, Both };
struct LinkPresentation {
    QString id, source, target, topic;
    LinkDirection direction = LinkDirection::None;
};
struct TreeEdge { QString source, target; };
struct Presentation {
    std::vector<NodePresentation> nodes;
    std::vector<TreeEdge> treeEdges;
    std::vector<LinkPresentation> links;
    QRectF bounds;
    bool outline = false;
};
// Ephemeral command-dialog data, not a mutable semantic model.
struct NodeChoice {
    QString id, topic, parent;
    QStringList children;
    bool expanded = true;
};
}
#endif
