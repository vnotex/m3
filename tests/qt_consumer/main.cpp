#include <m3/qt/editor.h>

#include <QApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
bool check(bool condition, const char *message) {
    if (!condition) {
        qCritical("%s", message);
    }
    return condition;
}

QJsonObject record(const QJsonArray &records, const QString &id) {
    for (const auto &value : records) {
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("id")).toString() == id) {
            return object;
        }
    }
    return {};
}
}

int main(int argc, char **argv) {
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    }
    QApplication application(argc, argv);
    m3::qt::MindMapEditor imported;
    const QJsonDocument independent = QJsonDocument::fromJson(imported.toJson());
    if (!check(independent.isObject(), "The independent editor did not initialize")) {
        return 1;
    }

    QByteArray retained;
    QString childId;
    QJsonDocument snapshot;
    {
        m3::qt::MindMapEditor source;
        if (!check(source.newDocument(QStringLiteral("Consumer root")), "Could not create a document")) {
            return 1;
        }
        childId = source.addNode(QStringLiteral("root"), QStringLiteral("Child 世界"));
        const QString linkId = source.addLink(QStringLiteral("root"), childId, true,
                                             QStringLiteral("<related>"));
        if (!check(!childId.isEmpty() && !linkId.isEmpty(), "Could not create the tiny graph") ||
            !check(source.renameNode(childId, QStringLiteral("Renamed 世界")), "Could not rename a node")) {
            return 1;
        }
        retained = source.toJson();
        snapshot = QJsonDocument::fromJson(retained);
        if (!check(!retained.isEmpty() && snapshot.isObject(), "Export did not produce a JSON object")) {
            return 1;
        }
        const QJsonObject document = snapshot.object();
        const QJsonArray nodes = document.value(QStringLiteral("nodes")).toArray();
        const QJsonArray links = document.value(QStringLiteral("crossLinks")).toArray();
        const QJsonObject child = record(nodes, childId);
        const QJsonObject root = record(nodes, QStringLiteral("root"));
        const QJsonObject link = record(links, linkId);
        if (!check(document.value(QStringLiteral("rootId")).toString() == QStringLiteral("root") &&
                       nodes.size() == 2 && links.size() == 1 &&
                       root.value(QStringLiteral("children")).toArray() == QJsonArray{childId} &&
                       child.value(QStringLiteral("topic")).toString() == QStringLiteral("Renamed 世界") &&
                       link.value(QStringLiteral("source")).toString() == QStringLiteral("root") &&
                       link.value(QStringLiteral("target")).toString() == childId &&
                       link.value(QStringLiteral("directed")).toBool() &&
                       link.value(QStringLiteral("topic")).toString() == QStringLiteral("<related>"),
                   "Export does not contain the edited graph")) {
            return 1;
        }
        if (!check(QJsonDocument::fromJson(imported.toJson()) == independent,
                   "Editing one widget changed the other widget") ||
            !check(imported.loadJson(retained), "Could not import the exported graph") ||
            !check(QJsonDocument::fromJson(imported.toJson()) == snapshot,
                   "The imported graph did not roundtrip") ||
            !check(source.renameNode(childId, QStringLiteral("Source only")),
                   "Could not edit the originating widget") ||
            !check(QJsonDocument::fromJson(imported.toJson()) == snapshot,
                   "Imported and originating widgets share mutable state")) {
            return 1;
        }
    }

    if (!check(imported.selectNode(childId) &&
                   imported.renameNode(childId, QStringLiteral("After source destruction")),
               "The imported widget did not outlive its source")) {
        return 1;
    }
    const QJsonDocument surviving = QJsonDocument::fromJson(imported.toJson());
    if (!check(record(surviving.object().value(QStringLiteral("nodes")).toArray(), childId)
                       .value(QStringLiteral("topic")).toString() == QStringLiteral("After source destruction"),
               "The surviving widget did not retain its edit") ||
        !check(imported.loadJson(retained) && QJsonDocument::fromJson(imported.toJson()) == snapshot,
               "The retained snapshot did not survive its originating widget")) {
        return 1;
    }
    return 0;
}
