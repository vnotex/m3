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
    {"#ffffff", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "White")}, {"#ecf0f1", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Cloud")},
    {"#95a5a6", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Gray")}, {"#34495e", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Slate")},
    {"#2c3e50", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Midnight")}, {"#000000", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Black")},
    {"#e74c3c", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Red")}, {"#e67e22", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Orange")},
    {"#f39c12", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Amber")}, {"#f1c40f", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Yellow")},
    {"#2ecc71", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Green")}, {"#27ae60", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Forest")},
    {"#1abc9c", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Teal")}, {"#16a085", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Jade")},
    {"#3498db", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Blue")}, {"#2980b9", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Ocean")},
    {"#9b59b6", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Purple")}, {"#8e44ad", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Violet")},
    {"#ffb6c1", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Pink")}, {"#f4a6a6", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Rose")},
    {"#ffd3a5", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Peach")}, {"#a8e6cf", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Mint")},
    {"#a9d6f5", QT_TRANSLATE_NOOP("m3::qt::NodeColors", "Sky")}
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
