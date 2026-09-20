#ifndef M3_QT_PRESENTATION_H
#define M3_QT_PRESENTATION_H
#include <QColor>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTextDocument>
#include <memory>
#include <optional>
#include <vector>

namespace m3::qt {
// Zero, nullopt and invalid colors inherit the editor's appearance.
struct NodeStyle {
    qreal fontSize = 0;
    std::optional<bool> bold, italic;
    QColor textColor, backgroundColor;
};
struct NodeProperties {
    QString id, topic, hyperlink, note;
    QStringList tags, icons;
    bool root = false;
    NodeStyle style;
};
struct NodePresentation {
    QString id, topic;
    bool root = false, expanded = true, hasChildren = false;
    QRectF rectangle;
    NodeStyle style;
    std::unique_ptr<QTextDocument> text;
};
struct LinkPresentation {
    QString id, source, target, topic;
    bool directed = false;
};
struct TreeEdge { QString source, target; };
struct Presentation {
    std::vector<NodePresentation> nodes;
    std::vector<TreeEdge> treeEdges;
    std::vector<LinkPresentation> links;
    QRectF bounds;
};
// Ephemeral command-dialog data, not a mutable semantic model.
struct NodeChoice {
    QString id, topic, parent;
    QStringList children;
    bool expanded = true;
};
}
#endif
