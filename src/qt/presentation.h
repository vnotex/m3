#ifndef M3_QT_PRESENTATION_H
#define M3_QT_PRESENTATION_H
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QTextDocument>
#include <memory>
#include <vector>

namespace m3::qt {
struct NodePresentation {
    QString id, topic;
    bool root = false, expanded = true, hasChildren = false;
    QRectF rectangle;
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
