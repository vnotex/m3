#include "test_support.h"
#include "m3/qt/editor.h"
#ifdef M3_QT_TEST_DEMO
#include "qt_demo_window.h"
#endif
#include <QAction>
#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFocusEvent>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeySequence>
#include <QLineF>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <set>
#include <utility>
#include <vector>

using Editor = m3::qt::MindMapEditor;

static QString qs(const Json &value) {
    const auto &text = value.get_ref<const std::string &>();
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
static std::string utf8(const QString &text) { return text.toUtf8().toStdString(); }
static QByteArray encoded(const Json &value) { return QByteArray::fromStdString(value.dump()); }
static Json exported(const Editor &editor) {
    const QByteArray text = editor.toJson();
    CHECK(!text.isEmpty());
    return Json::parse(text.constData(), text.constData() + text.size());
}
static const Json &record(const Json &doc, const char *collection, const QString &id) {
    const std::string key = utf8(id);
    for (const auto &entry : doc.at(collection)) if (entry.at("id") == key) return entry;
    throw std::runtime_error(std::string("Missing ") + collection + " record " + key);
}
static bool hasRecord(const Json &doc, const char *collection, const QString &id) {
    const std::string key = utf8(id);
    for (const auto &entry : doc.at(collection)) if (entry.at("id") == key) return true;
    return false;
}
static void setTopic(Json &doc, const char *id, const QString &topic) {
    for (auto &entry : doc.at("nodes")) if (entry.at("id") == id) {
        entry["topic"] = utf8(topic);
        return;
    }
    throw std::runtime_error(std::string("Missing fixture node ") + id);
}
static Json editorFixture() {
    Json doc = fixture();
    setTopic(doc, "a", QStringLiteral("Alpha"));
    setTopic(doc, "b", QStringLiteral("Beta"));
    setTopic(doc, "d", QStringLiteral("Delta"));
    return doc;
}
static Json nativeDocument(const Json &input) { return document(load(input)); }
static void pump() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}
static QGraphicsView &graphics(Editor &editor) {
    auto *view = editor.findChild<QGraphicsView *>();
    CHECK(view != nullptr && view->scene() != nullptr);
    return *view;
}
static void showEditor(Editor &editor, QSize size = QSize(1000, 700)) {
    editor.resize(size);
    editor.show();
    editor.activateWindow();
    pump();
    graphics(editor).setFocus(Qt::OtherFocusReason);
    pump();
    CHECK(graphics(editor).viewport()->width() > 0);
    CHECK(graphics(editor).viewport()->height() > 0);
}
static QList<QGraphicsTextItem *> texts(Editor &editor, const QString &text) {
    QList<QGraphicsTextItem *> result;
    for (auto *item : graphics(editor).scene()->items()) {
        if (auto *label = dynamic_cast<QGraphicsTextItem *>(item)) {
            if (label->isVisible() && label->toPlainText() == text) result.append(label);
        }
    }
    return result;
}
static QGraphicsTextItem *textItem(Editor &editor, const QString &text) {
    const auto matches = texts(editor, text);
    CHECK(!matches.isEmpty());
    return matches.front();
}
static QGraphicsItem *ownerItem(QGraphicsTextItem *text) {
    QGraphicsItem *owner = text;
    for (auto *item = static_cast<QGraphicsItem *>(text); item; item = item->parentItem()) {
        owner = item;
        if (item->flags().testFlag(QGraphicsItem::ItemIsSelectable)) return item;
    }
    return owner;
}
static QRectF topicRect(Editor &editor, const QString &topic) {
    return ownerItem(textItem(editor, topic))->sceneBoundingRect();
}
static QRectF emptyNodeRect(Editor &editor) {
    for (auto *text : texts(editor, QString())) {
        auto *owner = ownerItem(text);
        const QRectF rect = owner->sceneBoundingRect();
        if (rect.width() >= 71.0 && rect.height() >= 35.0 &&
            owner->contains(owner->mapFromScene(rect.center())) &&
            rect.adjusted(-1, -1, 1, 1).contains(text->sceneBoundingRect())) return rect;
    }
    throw std::runtime_error("No selectable-sized empty topic node in scene");
}
static bool belongsTo(QGraphicsItem *item, const QGraphicsItem *owner) {
    for (; item; item = item->parentItem()) if (item == owner) return true;
    return false;
}
static QPoint labelPoint(Editor &editor, const QString &topic) {
    auto &view = graphics(editor);
    auto *text = textItem(editor, topic);
    view.ensureVisible(text);
    pump();
    const QRectF rect = text->sceneBoundingRect();
    auto *owner = ownerItem(text);
    for (double fy : {0.5, 0.25, 0.75}) for (double fx : {0.5, 0.25, 0.75}) {
        const QPoint point = view.mapFromScene(QPointF(rect.left() + rect.width() * fx,
                                                      rect.top() + rect.height() * fy));
        if (view.viewport()->rect().contains(point) && belongsTo(view.itemAt(point), owner)) return point;
    }
    // A crossing link can cover the sample points without covering the whole label.
    const QRect visible = view.mapFromScene(rect).boundingRect().intersected(view.viewport()->rect());
    for (int y = visible.top(); y <= visible.bottom(); ++y)
        for (int x = visible.left(); x <= visible.right(); ++x) {
            const QPoint point(x, y);
            if (rect.contains(view.mapToScene(point)) && belongsTo(view.itemAt(point), owner)) return point;
        }
    throw std::runtime_error("Label is not reachable by a viewport click: " + utf8(topic));
}
static void clickLabel(Editor &editor, const QString &topic, bool doubleClick = false) {
    const QPoint point = labelPoint(editor, topic);
    if (doubleClick) QTest::mouseDClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, point);
    else QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, point);
    pump();
}
static QPoint blankPoint(QGraphicsView &view) {
    for (int y = 12; y < view.viewport()->height() - 12; y += 11)
        for (int x = 12; x < view.viewport()->width() - 12; x += 11)
            if (!view.itemAt(QPoint(x, y))) return QPoint(x, y);
    throw std::runtime_error("No blank viewport point");
}
static QRectF renderedBounds(Editor &editor) {
    QRectF bounds;
    bool first = true;
    for (auto *item : graphics(editor).scene()->items()) if (item->isVisible()) {
        const QRectF rect = item->sceneBoundingRect();
        if (rect.isEmpty()) continue;
        bounds = first ? rect : bounds.united(rect);
        first = false;
    }
    CHECK(!first);
    return bounds;
}
static void assertFit(Editor &editor) {
    auto &view = graphics(editor);
    editor.fitToContents();
    pump();
    const QRectF viewport(view.viewport()->rect());
    const QRectF mapped = view.mapFromScene(renderedBounds(editor)).boundingRect();
    CHECK(viewport.adjusted(-3, -3, 3, 3).contains(mapped));
}
static bool finiteRect(const QRectF &r) {
    return std::isfinite(r.x()) && std::isfinite(r.y()) && std::isfinite(r.width()) &&
           std::isfinite(r.height()) && r.width() > 0 && r.height() > 0;
}
static void assertTreeConnectors(Editor &editor, const std::vector<QRectF> &nodes) {
    size_t edges = 0;
    for (auto *item : graphics(editor).scene()->items()) {
        auto *connector = dynamic_cast<QGraphicsPathItem *>(item);
        if (!connector) continue;
        ++edges;
        const QPainterPath path = connector->mapToScene(connector->path());
        CHECK(path.elementCount() >= 2);
        CHECK(path.elementAt(0).type == QPainterPath::MoveToElement);
        if (editor.layoutDirection() == Editor::LayoutDirection::Outline) {
            CHECK(path.elementCount() == 3);
            const auto from = path.elementAt(0), elbow = path.elementAt(1), to = path.elementAt(2);
            CHECK(elbow.type == QPainterPath::LineToElement && to.type == QPainterPath::LineToElement);
            CHECK(from.x == elbow.x && elbow.y == to.y);
            CHECK(from.y < elbow.y && elbow.x < to.x);
            const QPainterPath stroke = connector->mapToScene(connector->shape());
            for (const auto &node : nodes)
                CHECK(!stroke.intersects(node.adjusted(3, 3, -3, -3)));
        } else {
            CHECK(path.elementAt(1).type == QPainterPath::CurveToElement);
        }
    }
    CHECK(edges + 1 == nodes.size());
}
static void rejectUnchanged(Editor &editor, const std::function<bool()> &operation) {
    const Json before = exported(editor);
    const QString nodeId = editor.selectedNodeId(), linkId = editor.selectedLinkId();
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QSignalSpy selected(&editor, &Editor::selectionChanged);
    QSignalSpy errors(&editor, &Editor::errorOccurred);
    CHECK(!operation());
    CHECK(!editor.lastError().isEmpty());
    CHECK(!errors.isEmpty());
    CHECK(errors.back().front().toString() == editor.lastError());
    CHECK(exported(editor) == before);
    CHECK(editor.selectedNodeId() == nodeId && editor.selectedLinkId() == linkId);
    CHECK(changed.isEmpty() && selected.isEmpty());
}
static QAction &editAction(Editor &editor, const char *name) {
    auto *action = editor.findChild<QAction *>(QString::fromLatin1(name));
    CHECK(action != nullptr);
    return *action;
}
static void trigger(Editor &editor, const char *name) {
    auto &action = editAction(editor, name);
    CHECK(action.isEnabled());
    action.trigger();
    pump();
}
static QAction &textAction(QWidget &widget, const QStringList &labels) {
    for (auto *action : widget.findChildren<QAction *>()) {
        QString text = action->text().section(QLatin1Char('\t'), 0, 0);
        text.remove(QLatin1Char('&'));
        text = text.trimmed();
        if (text.endsWith(QStringLiteral("..."))) text.chop(3);
        if (text.endsWith(QChar(0x2026))) text.chop(1);
        for (const auto &label : labels)
            if (text.compare(label, Qt::CaseInsensitive) == 0) return *action;
    }
    throw std::runtime_error("Missing visible action " + utf8(labels.join(QStringLiteral(" / "))));
}
static void shortcut(Editor &editor, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    editor.activateWindow();
    graphics(editor).setFocus(Qt::OtherFocusReason);
    pump();
    QTest::keyClick(graphics(editor).viewport(), key, modifiers);
    pump();
}
static QPlainTextEdit *activeTopicInput(Editor &editor) {
    for (auto *input : graphics(editor).viewport()->findChildren<QPlainTextEdit *>(QStringLiteral("topicEditor")))
        if (input->isVisible()) return input;
    return nullptr;
}
static QPlainTextEdit &topicInput(Editor &editor) {
    auto *input = activeTopicInput(editor);
    CHECK(input != nullptr && input->hasFocus());
    return *input;
}
static void topicKey(Editor &editor, Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(&topicInput(editor), key, modifiers);
    pump();
}
static void wheel(QGraphicsView &view, QPoint point, int delta, Qt::KeyboardModifiers modifiers) {
    QWheelEvent event(QPointF(point), QPointF(view.viewport()->mapToGlobal(point)), QPoint(),
                      QPoint(0, delta), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(view.viewport(), &event);
    pump();
}
static void movePointer(QGraphicsView &view, QPoint position, Qt::MouseButtons buttons) {
    QMouseEvent event(QEvent::MouseMove, QPointF(position), QPointF(view.viewport()->mapToGlobal(position)),
                      Qt::NoButton, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &event);
    pump();
}
static void dragMiddle(QGraphicsView &view, QPoint from, QPoint to) {
    QTest::mousePress(view.viewport(), Qt::MiddleButton, Qt::NoModifier, from);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(view.viewport()->mapToGlobal(to)),
                     Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &move);
    QTest::mouseRelease(view.viewport(), Qt::MiddleButton, Qt::NoModifier, to);
    pump();
}

using DialogStep = std::function<void(QDialog *)>;
static void dialogs(const std::vector<DialogStep> &steps, const std::function<void()> &operation) {
    size_t next = 0;
    std::exception_ptr failure;
    QPointer<QDialog> last;
    QTimer responder, watchdog;
    responder.setInterval(0);
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        failure = std::make_exception_ptr(std::runtime_error("Dialog response timed out"));
        if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
    });
    QObject::connect(&responder, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (failure) { dialog->reject(); return; }
        if (dialog == last) return;
        last = dialog;
        try {
            // A host may report a real I/O error with an acknowledgment box or
            // a status message. Neither is a Save/Discard/Cancel decision.
            if (auto *box = qobject_cast<QMessageBox *>(dialog)) {
                if ((box->icon() == QMessageBox::Warning || box->icon() == QMessageBox::Critical) &&
                    !(box->standardButtons() & (QMessageBox::Save | QMessageBox::Discard |
                                                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel))) {
                    box->accept();
                    return;
                }
            }
            CHECK(next < steps.size());
            const auto response = steps[next++];
            response(dialog);
        } catch (...) {
            failure = std::current_exception();
            dialog->reject();
        }
    });
    // Queued responders run inside the real modal event loop; no timing sleeps.
    QTimer::singleShot(0, &responder, [&] { responder.start(); });
    watchdog.start(5000);
    operation();
    pump();
    responder.stop();
    watchdog.stop();
    if (failure) std::rethrow_exception(failure);
    CHECK(next == steps.size());
}
#ifdef M3_QT_TEST_DEMO
static DialogStep messageResponse(QMessageBox::StandardButton button) {
    return [button](QDialog *dialog) {
        auto *box = qobject_cast<QMessageBox *>(dialog);
        CHECK(box != nullptr && box->button(button) != nullptr);
        box->button(button)->click();
    };
}
#endif
static DialogStep deleteResponse(bool accept) {
    return [accept](QDialog *dialog) {
        auto *box = qobject_cast<QMessageBox *>(dialog);
        CHECK(box != nullptr);
        const auto choices = accept
            ? std::vector<QMessageBox::StandardButton>{QMessageBox::Yes, QMessageBox::Ok, QMessageBox::Discard}
            : std::vector<QMessageBox::StandardButton>{QMessageBox::Cancel, QMessageBox::No};
        for (auto choice : choices) if (auto *button = box->button(choice)) { button->click(); return; }
        throw std::runtime_error("Deletion confirmation has no suitable response");
    };
}
static void chooseId(QComboBox &picker, const QString &id) {
    const QString suffix = QStringLiteral("[") + id + QStringLiteral("]");
    for (int i = 0; i < picker.count(); ++i) if (picker.itemText(i).endsWith(suffix)) {
        picker.setCurrentIndex(i);
        return;
    }
    throw std::runtime_error("Missing picker ID " + utf8(id));
}
static QStringList pickerIds(const QComboBox &picker) {
    QStringList result;
    for (int i = 0; i < picker.count(); ++i) {
        const QString label = picker.itemText(i);
        const qsizetype start = label.lastIndexOf(QLatin1Char('['));
        CHECK(start >= 0 && label.endsWith(QLatin1Char(']')));
        result.append(label.mid(start + 1, label.size() - start - 2));
    }
    return result;
}
static QStringList preorderIds(const Json &doc) {
    QStringList ids;
    for (const auto &entry : doc.at("nodes")) ids.append(qs(entry.at("id")));
    return ids;
}
#ifdef M3_QT_TEST_DEMO
static DialogStep fileResponse(const QString &path, bool accept) {
    return [path, accept](QDialog *dialog) {
        auto *file = qobject_cast<QFileDialog *>(dialog);
        CHECK(file != nullptr);
        if (!accept) { file->reject(); return; }
        file->selectFile(path);
        // QFileDialog's override is protected; invoke the public QDialog API.
        static_cast<QDialog *>(file)->accept();
    };
}
#endif
static QImage paintScene(Editor &editor, const QRectF &source) {
    const QSize size(std::max(1, static_cast<int>(std::ceil(source.width()))),
                     std::max(1, static_cast<int>(std::ceil(source.height()))));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    graphics(editor).scene()->render(&painter, QRectF(QPointF(), QSizeF(size)), source, Qt::IgnoreAspectRatio);
    painter.end();
    return image;
}
static QPoint imagePoint(const QImage &image, const QRectF &source, const QPointF &point) {
    return QPoint(static_cast<int>((point.x() - source.left()) * image.width() / source.width()),
                  static_cast<int>((point.y() - source.top()) * image.height() / source.height()));
}
static bool ink(QRgb pixel) { return qRed(pixel) < 225 || qGreen(pixel) < 225 || qBlue(pixel) < 225; }
static int paintedPixels(const QImage &image, QRect region) {
    region = region.intersected(image.rect());
    int count = 0;
    for (int y = region.top(); y <= region.bottom(); ++y)
        for (int x = region.left(); x <= region.right(); ++x) if (ink(image.pixel(x, y))) ++count;
    return count;
}
static QPoint curvePoint(Editor &editor, const QString &label, const std::vector<QRectF> &nodes,
                         bool outsidePaint = false) {
    auto &view = graphics(editor);
    auto *owner = ownerItem(textItem(editor, label));
    view.ensureVisible(owner);
    pump();
    const QRect scan = view.mapFromScene(owner->sceneBoundingRect()).boundingRect()
                         .intersected(view.viewport()->rect().adjusted(3, 3, -3, -3));
    const QImage image = outsidePaint ? view.viewport()->grab().toImage().convertToFormat(QImage::Format_RGB32) : QImage();
    const qreal ratio = outsidePaint ? image.devicePixelRatio() : 1.0;
    std::vector<QRectF> labels;
    for (auto *item : view.scene()->items()) if (dynamic_cast<QGraphicsTextItem *>(item))
        labels.push_back(item->sceneBoundingRect().adjusted(-4, -4, 4, 4));
    for (int y = scan.top(); y <= scan.bottom(); ++y) for (int x = scan.left(); x <= scan.right(); ++x) {
        const QPoint point(x, y);
        const QPointF scenePoint = view.mapToScene(point);
        if (!belongsTo(view.itemAt(point), owner)) continue;
        bool obstructed = false;
        for (const auto &rect : nodes) if (rect.adjusted(-5, -5, 5, 5).contains(scenePoint)) obstructed = true;
        for (const auto &rect : labels) if (rect.contains(scenePoint)) obstructed = true;
        if (obstructed) continue;
        if (outsidePaint) {
            bool clear = true;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const QPoint sample(qRound((x + dx) * ratio), qRound((y + dy) * ratio));
                if (!image.rect().contains(sample) || ink(image.pixel(sample))) clear = false;
            }
            if (!clear) continue;
        }
        return point;
    }
    throw std::runtime_error("No independently reachable curve point for " + utf8(label));
}
#ifdef M3_QT_TEST_DEMO
static Editor &embedded(DemoWindow &window) {
    auto *editor = qobject_cast<Editor *>(window.centralWidget());
    CHECK(editor != nullptr);
    return *editor;
}
static void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    CHECK(file.write(bytes) == bytes.size());
    CHECK(file.flush());
    file.close();
}
static QByteArray readFile(const QString &path) {
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    CHECK(file.error() == QFileDevice::NoError);
    return bytes;
}
static Json fileDocument(const QString &path) {
    const QByteArray bytes = readFile(path);
    return Json::parse(bytes.constData(), bytes.constData() + bytes.size());
}
static bool sameFile(const QString &a, const QString &b) {
    return QFileInfo(a).absoluteFilePath() == QFileInfo(b).absoluteFilePath();
}
static QAction &hostAction(DemoWindow &window, QKeySequence::StandardKey key) {
    const QKeySequence wanted(key);
    for (auto *action : window.findChildren<QAction *>()) if (action->shortcuts().contains(wanted)) return *action;
    if (key == QKeySequence::Open) return textAction(window, {QStringLiteral("Open")});
    if (key == QKeySequence::SaveAs) return textAction(window, {QStringLiteral("Save As")});
    if (key == QKeySequence::Save) return textAction(window, {QStringLiteral("Save")});
    if (key == QKeySequence::New) return textAction(window, {QStringLiteral("New")});
    throw std::runtime_error("Missing host file action");
}
static void showDemo(DemoWindow &window) {
    window.resize(1000, 700);
    window.show();
    window.activateWindow();
    pump();
}
#endif

static void document_case() {
    Editor editor;
    const Json initial = exported(editor);
    CHECK(initial.at("rootId") == "root");
    CHECK(record(initial, "nodes", QStringLiteral("root")).at("topic") == "Central topic");
    CHECK(editor.selectedNodeId() == QStringLiteral("root") && editor.selectedLinkId().isEmpty());

    const Json input = editorFixture();
    const Json expected = nativeDocument(input);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QSignalSpy selected(&editor, &Editor::selectionChanged);
    CHECK(editor.loadJson(encoded(input)));
    CHECK(changed.size() == 1);
    CHECK(exported(editor) == expected);
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && editor.selectedLinkId().isEmpty());
    CHECK(selected.size() == 1);
    CHECK(selected.back().at(0).toString() == QStringLiteral("r"));
    CHECK(selected.back().at(1).toString().isEmpty());
    Editor second;
    CHECK(second.loadJson(editor.toJson()));
    CHECK(exported(second) == expected);
    CHECK(record(exported(second), "nodes", QStringLiteral("r")).at("tags") == Json({"x", "y", "x"}));
    CHECK(record(exported(second), "nodes", QStringLiteral("r")).at("children") == Json({"a", "b", "c"}));

    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(selected.size() == 2);
    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(selected.size() == 2);
    CHECK(changed.size() == 1);
    std::vector<QByteArray> invalid{
        QByteArray("{"),
        QByteArray(R"({"schemaVersion":1,"rootId":"r","nodes":[{"id":"r","topic":"first","topic":"second"}]})"),
        QByteArray(R"({"schemaVersion":1,"rootId":"r","nodes":[]})"),
        QByteArray()
    };
    QByteArray nul = encoded(input);
    nul.insert(7, '\0');
    invalid.push_back(nul);
    for (const auto &bytes : invalid) rejectUnchanged(editor, [&] { return editor.loadJson(bytes); });
    CHECK(changed.size() == 1 && selected.size() == 2);
    CHECK(exported(second) == expected);
    rejectUnchanged(editor, [&] { return editor.selectNode(QStringLiteral("missing")); });
    rejectUnchanged(editor, [&] { return editor.selectLink(QStringLiteral("missing")); });

    const Json foreign = Json::parse(R"({"nodeData":{"id":"foreign-root","topic":"Mind Elixir 世界","children":[{"id":"foreign-child","topic":"Child","memo":"Preserve foreign memo"}]},"linkData":{"foreign-link":{"id":"foreign-link","from":"foreign-child","to":"foreign-root","label":"Foreign label"}}})");
    CHECK(editor.loadJson(encoded(foreign)));
    CHECK(changed.size() == 2);
    CHECK(editor.lastError().isEmpty());
    CHECK(exported(editor) == nativeDocument(foreign));
    CHECK(exported(editor).at("schemaVersion") == 1 && !exported(editor).contains("nodeData"));
    CHECK(editor.selectedNodeId() == QStringLiteral("foreign-root"));
    CHECK(record(exported(editor), "nodes", QStringLiteral("foreign-child")).at("note") == "Preserve foreign memo");
    CHECK(editor.selectLink(QStringLiteral("foreign-link")));
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId() == QStringLiteral("foreign-link"));
    const auto selectionCount = selected.size();
    CHECK(editor.selectLink(QStringLiteral("foreign-link")));
    CHECK(selected.size() == selectionCount);
    editor.clearSelection();
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
    CHECK(selected.size() == selectionCount + 1);
    editor.clearSelection();
    CHECK(selected.size() == selectionCount + 1 && changed.size() == 2);

    CHECK(editor.newDocument(QString::fromUtf8("新規\nCafé")));
    CHECK(changed.size() == 3);
    CHECK(exported(editor).at("rootId") == "root");
    CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == utf8(QString::fromUtf8("新規\nCafé")));
    CHECK(editor.selectedNodeId() == QStringLiteral("root") && editor.selectedLinkId().isEmpty());
    QString badTopic = QStringLiteral("before");
    badTopic.append(QChar(0));
    badTopic.append(QStringLiteral("after"));
    rejectUnchanged(editor, [&] { return editor.newDocument(badTopic); });
    CHECK(editor.newDocument(QString()));
    CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == "");
    CHECK(changed.size() == 4);
}

static void tree_edits_case() {
    Json input = editorFixture();
    for (auto &entry : input.at("nodes")) if (entry.at("id") == "a") {
        entry["note"] = "Do not overwrite";
        entry["style"] = Json::parse(R"({"custom":{"nested":[true,3,"世界"]}})");
        entry["icons"] = Json({"bookmark"});
    }
    Editor editor;
    CHECK(editor.loadJson(encoded(input)));
    const Json original = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    const QString added = editor.addNode(QStringLiteral("a"), QString::fromUtf8("Added\nκόσμος"));
    CHECK(!added.isEmpty() && !hasRecord(original, "nodes", added));
    CHECK(changed.size() == 1);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("children") == Json({"d", utf8(added)}));
    CHECK(record(exported(editor), "nodes", added).at("topic") == utf8(QString::fromUtf8("Added\nκόσμος")));
    CHECK(editor.selectedNodeId() == added && editor.selectedLinkId().isEmpty());
    CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Alpha renamed")));
    CHECK(changed.size() == 2 && editor.selectedNodeId() == added);
    Json expectedA = record(original, "nodes", QStringLiteral("a"));
    expectedA["children"] = Json({"d", utf8(added)});
    expectedA["topic"] = "Alpha renamed";
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")) == expectedA);
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")) == record(original, "nodes", QStringLiteral("r")));

    rejectUnchanged(editor, [&] { return editor.removeNode(QStringLiteral("r")); });
    rejectUnchanged(editor, [&] { return editor.moveNode(QStringLiteral("r"), QStringLiteral("a")); });
    rejectUnchanged(editor, [&] { return editor.moveNode(QStringLiteral("a"), QStringLiteral("d")); });
    rejectUnchanged(editor, [&] { return editor.moveNode(QStringLiteral("d"), QStringLiteral("b"), 1); });
    rejectUnchanged(editor, [&] { return editor.moveNode(QStringLiteral("c"), QStringLiteral("r"), -2); });
    rejectUnchanged(editor, [&] { return !editor.addNode(QStringLiteral("a"), QStringLiteral("Invalid index"), 99).isEmpty(); });
    rejectUnchanged(editor, [&] { return !editor.addNode(QStringLiteral("a"), QStringLiteral("Negative index"), -2).isEmpty(); });
    rejectUnchanged(editor, [&] { return !editor.addNode(QStringLiteral("absent"), QStringLiteral("Missing parent")).isEmpty(); });
    QString nul = QStringLiteral("Alpha");
    nul.append(QChar(0));
    nul.append(QStringLiteral("suffix"));
    rejectUnchanged(editor, [&] { return editor.renameNode(QStringLiteral("a"), nul); });
    rejectUnchanged(editor, [&] { return !editor.addNode(QStringLiteral("a"), nul).isEmpty(); });
    rejectUnchanged(editor, [&] { return editor.removeNode(nul); });
    CHECK(changed.size() == 2);

    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(editor.moveNode(QStringLiteral("d"), QStringLiteral("b")));
    CHECK(changed.size() == 3 && editor.selectedNodeId() == QStringLiteral("d"));
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("children") == Json({utf8(added)}));
    CHECK(record(exported(editor), "nodes", QStringLiteral("b")).at("children") == Json({"d"}));
    CHECK(editor.moveNode(QStringLiteral("c"), QStringLiteral("r"), 0));
    CHECK(changed.size() == 4);
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"c", "a", "b"}));
    CHECK(editor.removeNode(QStringLiteral("b")));
    CHECK(changed.size() == 5);
    const Json after = exported(editor);
    CHECK(!hasRecord(after, "nodes", QStringLiteral("b")) && !hasRecord(after, "nodes", QStringLiteral("d")));
    CHECK(hasRecord(after, "nodes", added));
    CHECK(record(after, "nodes", QStringLiteral("r")).at("children") == Json({"c", "a"}));
    CHECK(after.at("crossLinks").size() == 1 && after.at("crossLinks").front().at("id") == "l3");
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && editor.selectedLinkId().isEmpty());
    CHECK(editor.lastError().isEmpty());
}

static void collapse_case() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    const Json original = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QSignalSpy selected(&editor, &Editor::selectionChanged);
    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    CHECK(changed.size() == 1 && selected.size() == 2);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.selectedLinkId().isEmpty());
    CHECK(selected.back().at(0).toString() == QStringLiteral("a"));
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded") == false);
    CHECK(record(exported(editor), "nodes", QStringLiteral("d")) == record(original, "nodes", QStringLiteral("d")));
    CHECK(exported(editor).at("crossLinks") == original.at("crossLinks"));
    rejectUnchanged(editor, [&] { return editor.selectNode(QStringLiteral("d")); });
    rejectUnchanged(editor, [&] { return editor.selectLink(QStringLiteral("l2")); });
    CHECK(editor.setExpanded(QStringLiteral("a"), true));
    CHECK(changed.size() == 2 && selected.size() == 2);
    CHECK(exported(editor) == original);
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    CHECK(changed.size() == 3);
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
    CHECK(editor.selectNode(QStringLiteral("a")));
    CHECK(editor.moveNode(QStringLiteral("b"), QStringLiteral("a")));
    CHECK(changed.size() == 4);
    rejectUnchanged(editor, [&] { return editor.selectNode(QStringLiteral("b")); });
    const QString hidden = editor.addNode(QStringLiteral("a"), QStringLiteral("Hidden insertion"));
    CHECK(!hidden.isEmpty() && changed.size() == 5);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("children") == Json({"d", "b", utf8(hidden)}));
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded") == false);
    rejectUnchanged(editor, [&] { return editor.selectNode(hidden); });
    CHECK(editor.setExpanded(QStringLiteral("a"), true));
    CHECK(changed.size() == 6);
    CHECK(editor.selectNode(hidden));
    CHECK(editor.selectNode(QStringLiteral("b")));
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(editor.setExpanded(QStringLiteral("r"), false));
    CHECK(changed.size() == 7);
    CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    for (const QString &id : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d"), hidden})
        rejectUnchanged(editor, [&] { return editor.selectNode(id); });
    const Json collapsed = exported(editor);
    for (const auto &entry : collapsed.at("crossLinks")) {
        const QString id = qs(entry.at("id"));
        rejectUnchanged(editor, [&] { return editor.selectLink(id); });
    }
    CHECK(hasRecord(exported(editor), "nodes", hidden));
    CHECK(exported(editor).at("crossLinks") == original.at("crossLinks"));
    CHECK(editor.setExpanded(QStringLiteral("r"), true));
    CHECK(changed.size() == 8 && editor.selectNode(hidden));
    rejectUnchanged(editor, [&] { return editor.setExpanded(QStringLiteral("absent"), false); });
}

static void graph_edits_case() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    const Json original = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    const QString first = editor.addLink(QStringLiteral("a"), QStringLiteral("b"), false, QStringLiteral("Parallel one"));
    const QString second = editor.addLink(QStringLiteral("a"), QStringLiteral("b"), true, QStringLiteral("Parallel two"));
    const QString self = editor.addLink(QStringLiteral("a"), QStringLiteral("a"), true, QStringLiteral("Self directed"));
    CHECK(!first.isEmpty() && !second.isEmpty() && !self.isEmpty());
    CHECK(first != second && first != self && second != self);
    CHECK(changed.size() == 3);
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId() == self);
    CHECK(record(exported(editor), "crossLinks", first).at("source") == "a");
    CHECK(record(exported(editor), "crossLinks", first).at("target") == "b");
    CHECK(record(exported(editor), "crossLinks", first).at("directed") == false);
    CHECK(record(exported(editor), "crossLinks", second).at("directed") == true);
    CHECK(record(exported(editor), "crossLinks", self).at("source") == "a");
    CHECK(record(exported(editor), "crossLinks", self).at("target") == "a");

    CHECK(editor.selectLink(QStringLiteral("l1")));
    CHECK(editor.updateLink(QStringLiteral("l1"), QStringLiteral("b"), QStringLiteral("a"), false, QStringLiteral("Edited reverse")));
    CHECK(changed.size() == 4 && editor.selectedLinkId() == QStringLiteral("l1"));
    Json expected = record(original, "crossLinks", QStringLiteral("l1"));
    expected["source"] = "b";
    expected["target"] = "a";
    expected["directed"] = false;
    expected["topic"] = "Edited reverse";
    CHECK(record(exported(editor), "crossLinks", QStringLiteral("l1")) == expected);
    CHECK(exported(editor).at("nodes") == original.at("nodes"));
    rejectUnchanged(editor, [&] { return !editor.addLink(QStringLiteral("absent"), QStringLiteral("b"), true).isEmpty(); });
    rejectUnchanged(editor, [&] { return editor.updateLink(QStringLiteral("l1"), QStringLiteral("a"), QStringLiteral("absent"), true, QString()); });
    rejectUnchanged(editor, [&] { return editor.updateLink(QStringLiteral("absent"), QStringLiteral("a"), QStringLiteral("b"), true, QString()); });
    rejectUnchanged(editor, [&] { return editor.removeLink(QStringLiteral("absent")); });
    QString nul = QStringLiteral("bad");
    nul.append(QChar(0));
    rejectUnchanged(editor, [&] { return !editor.addLink(QStringLiteral("a"), QStringLiteral("b"), false, nul).isEmpty(); });
    rejectUnchanged(editor, [&] { return editor.updateLink(first, nul, QStringLiteral("b"), true, QString()); });
    CHECK(changed.size() == 4);

    QSignalSpy selected(&editor, &Editor::selectionChanged);
    CHECK(editor.selectNode(QStringLiteral("a")));
    CHECK(editor.selectedLinkId().isEmpty());
    CHECK(editor.selectLink(first));
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId() == first);
    CHECK(selected.size() == 2);
    CHECK(editor.selectLink(first) && selected.size() == 2);
    CHECK(editor.removeLink(first));
    CHECK(changed.size() == 5 && selected.size() == 3);
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
    CHECK(!hasRecord(exported(editor), "crossLinks", first));
    CHECK(hasRecord(exported(editor), "crossLinks", second) && hasRecord(exported(editor), "crossLinks", self));
    CHECK(record(exported(editor), "crossLinks", QStringLiteral("l1")) == expected);
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    CHECK(changed.size() == 6);
    CHECK(editor.selectedLinkId().isEmpty() && editor.selectedNodeId().isEmpty());
    CHECK(hasRecord(exported(editor), "crossLinks", QStringLiteral("l2")));
    rejectUnchanged(editor, [&] { return editor.selectLink(QStringLiteral("l2")); });
    CHECK(editor.setExpanded(QStringLiteral("a"), true));
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.updateLink(QStringLiteral("l2"), QStringLiteral("c"), QStringLiteral("d"), true, QString()));
    CHECK(changed.size() == 8);
    Json expectedOther = record(original, "crossLinks", QStringLiteral("l2"));
    expectedOther["source"] = "c";
    expectedOther["target"] = "d";
    expectedOther["directed"] = true;
    expectedOther["topic"] = "";
    CHECK(record(exported(editor), "crossLinks", QStringLiteral("l2")) == expectedOther);
    CHECK(editor.removeLink(QStringLiteral("l2")) && changed.size() == 9);
    CHECK(editor.selectedLinkId().isEmpty());
}

static void lifetime_case() {
    Editor survivor;
    CHECK(survivor.newDocument(QStringLiteral("Independent survivor")));
    const Json untouched = exported(survivor);
    QSignalSpy survivorChanges(&survivor, &Editor::documentChanged);
    QByteArray retained;
    Json retainedDocument;
    for (int iteration = 0; iteration < 24; ++iteration) {
        auto parent = std::make_unique<QWidget>();
        auto *editor = new Editor(parent.get());
        QPointer<Editor> observer(editor);
        CHECK(editor->loadJson(encoded(editorFixture())));
        const QString id = editor->addNode(QStringLiteral("a"), QStringLiteral("Owned %1").arg(iteration));
        CHECK(!id.isEmpty());
        CHECK(editor->selectedNodeId() == id);
        retained = editor->toJson();
        retainedDocument = exported(*editor);
        CHECK(editor->newDocument(QStringLiteral("Replacement")));
        CHECK(editor->loadJson(retained));
        CHECK(exported(*editor) == retainedDocument);
        CHECK(editor->selectNode(QStringLiteral("d")));
        CHECK(editor->removeNode(QStringLiteral("a")));
        CHECK(editor->selectedNodeId() == QStringLiteral("r"));
        CHECK(!hasRecord(exported(*editor), "nodes", id));
        CHECK(editor->loadJson(retained));
        // Initial fit/refresh callbacks may already be queued when the owner dies.
        parent->resize(600, 400);
        editor->resize(600, 400);
        parent->show();
        editor->show();
        parent.reset();
        CHECK(observer.isNull());
        pump();
        CHECK(exported(survivor) == untouched && survivorChanges.isEmpty());
    }
    CHECK(!retained.isEmpty());
    CHECK(survivor.loadJson(retained));
    CHECK(exported(survivor) == retainedDocument);
    CHECK(survivorChanges.size() == 1);
    const Json snapshot = exported(survivor);
    {
        Editor other;
        CHECK(other.loadJson(retained));
        CHECK(other.renameNode(QStringLiteral("r"), QStringLiteral("Other editor only")));
        CHECK(other.removeNode(QStringLiteral("a")));
        CHECK(exported(survivor) == snapshot && survivorChanges.size() == 1);
    }
    CHECK(survivor.renameNode(QStringLiteral("r"), QStringLiteral("Still alive")));
    CHECK(survivorChanges.size() == 2);
    Editor restored;
    CHECK(restored.loadJson(retained));
    CHECK(exported(restored) == retainedDocument);
}

static void render_case() {
    Json input = editorFixture();
    const QString wrapped = QString::fromUtf8("Long Unicode 世界 — café naïve Καλημέρα repeated words wrap without clipping\nSecond line: 日本語 and <angle brackets> stay plain");
    setTopic(input, "a", wrapped);
    setTopic(input, "b", QStringLiteral("<b>plain</b>"));
    const QString longTag(80, QLatin1Char('W'));
    const QStringList addedTags{QStringLiteral("two words"), QStringLiteral("<b>tag</b>"),
                               QString::fromUtf8("世界"), longTag};
    for (auto &node : input.at("nodes")) if (node.at("id") == "a")
        node["tags"] = Json::array({"two words", "<b>tag</b>", "世界", std::string(80, 'W')});
    for (auto &link : input.at("crossLinks")) {
        if (link.at("id") == "l2") link["topic"] = "<i>plain link</i>";
        if (link.at("id") == "l3") { link["topic"] = "Self loop"; link["directed"] = true; }
        if (link.at("id") == "l4") { link["source"] = "a"; link["target"] = "b"; link["topic"] = "Parallel route"; }
    }
    input["crossLinks"].push_back({{"id", "reverse"}, {"source", "b"}, {"target", "a"},
                                   {"directed", true}, {"topic", "Reverse route"}});
    Editor editor;
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(15);
    editor.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Window, Qt::white);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::WindowText, QColor(24, 28, 36));
    palette.setColor(QPalette::Text, QColor(24, 28, 36));
    palette.setColor(QPalette::Button, QColor(234, 240, 248));
    palette.setColor(QPalette::ButtonText, QColor(24, 28, 36));
    palette.setColor(QPalette::Highlight, QColor(30, 95, 210));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Mid, QColor(100, 110, 125));
    palette.setColor(QPalette::Dark, QColor(55, 65, 80));
    editor.setPalette(palette);
    CHECK(editor.loadJson(encoded(input)));
    showEditor(editor);
    const Json semantic = exported(editor);
    const QString rootTopic = qs(record(semantic, "nodes", QStringLiteral("r")).at("topic"));
    auto checkTags = [&](const QString &topic, const QStringList &values) {
        auto *label = textItem(editor, topic);
        auto *owner = ownerItem(label);
        QList<QGraphicsTextItem *> badges;
        for (const auto &value : std::set<QString>(values.begin(), values.end()))
            for (auto *badge : texts(editor, value)) if (ownerItem(badge) == owner) badges.append(badge);
        std::sort(badges.begin(), badges.end(), [](const auto *a, const auto *b) {
            const QRectF first = a->sceneBoundingRect(), second = b->sceneBoundingRect();
            return first.top() == second.top() ? first.left() < second.left() : first.top() < second.top();
        });
        CHECK(badges.size() == values.size());
        for (qsizetype i = 0; i < badges.size(); ++i) {
            const QRectF rectangle = badges[i]->sceneBoundingRect();
            CHECK(badges[i]->toPlainText() == values[i]);
            CHECK(rectangle.top() > label->sceneBoundingRect().bottom());
            CHECK(owner->sceneBoundingRect().contains(rectangle));
            for (qsizetype j = 0; j < i; ++j) CHECK(!rectangle.intersects(badges[j]->sceneBoundingRect()));
        }
    };
    auto checkNodes = [&] {
        checkTags(rootTopic, {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("x")});
        checkTags(wrapped, addedTags);
        const QRectF firstTag = textItem(editor, addedTags.front())->sceneBoundingRect();
        const QRectF lastTag = textItem(editor, longTag)->sceneBoundingRect();
        CHECK(lastTag.top() > firstTag.bottom());
        CHECK(lastTag.height() > 2 * firstTag.height());
        std::vector<QRectF> rectangles;
        for (const QString &topic : {rootTopic, wrapped, QStringLiteral("<b>plain</b>"), QStringLiteral("Delta")}) {
            auto *text = textItem(editor, topic);
            const QRectF rectangle = ownerItem(text)->sceneBoundingRect();
            CHECK(finiteRect(rectangle) && rectangle.width() >= 71 && rectangle.height() >= 35);
            CHECK(rectangle.adjusted(-1, -1, 1, 1).contains(text->sceneBoundingRect()));
            CHECK(text->toPlainText() == topic);
            CHECK(text->document()->size().width() <= 242);
            CHECK(text->sceneBoundingRect().height() + 12 <= rectangle.height());
            rectangles.push_back(rectangle);
        }
        rectangles.push_back(emptyNodeRect(editor));
        for (size_t i = 0; i < rectangles.size(); ++i) {
            CHECK(finiteRect(rectangles[i]));
            for (size_t j = i + 1; j < rectangles.size(); ++j)
                CHECK(!rectangles[i].adjusted(1, 1, -1, -1).intersects(rectangles[j].adjusted(1, 1, -1, -1)));
        }
        CHECK(textItem(editor, rootTopic)->font().bold());
        CHECK(editor.selectNode(QStringLiteral("c")));
        return rectangles;
    };
    auto checkLinkLabels = [&](const std::vector<QRectF> &nodes) {
        std::vector<QRectF> labels;
        for (const auto &link : semantic.at("crossLinks")) {
            const QString topic = qs(link.at("topic"));
            if (topic.isEmpty()) continue;
            auto *item = ownerItem(textItem(editor, topic));
            const QPainterPath linkShape = item->mapToScene(item->shape());
            auto endpointRect = [&](const char *key) {
                const QString text = qs(record(semantic, "nodes", qs(link.at(key))).at("topic"));
                return text.isEmpty() ? emptyNodeRect(editor) : topicRect(editor, text);
            };
            const QRectF source = endpointRect("source"), target = endpointRect("target");
            const QRectF label = textItem(editor, topic)->sceneBoundingRect();
            for (const auto &node : nodes) {
                CHECK(!label.intersects(node));
                // The public hit shape includes the curve, arrow, label, and leader.
                // Only attachments may overlap an endpoint's border hit allowance.
                const QRectF interior = node == source || node == target ? node.adjusted(8, 8, -8, -8) : node;
                CHECK(!linkShape.intersects(interior));
            }
            for (const auto &other : labels) CHECK(!label.intersects(other));
            labels.push_back(label);
            clickLabel(editor, topic);
            CHECK(editor.selectedLinkId() == qs(link.at("id")));
        }
        editor.clearSelection();
    };
    auto nodes = checkNodes();
    checkLinkLabels(nodes);
    CHECK(texts(editor, QStringLiteral("plain")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("<i>plain link</i>")).isEmpty());
    CHECK(texts(editor, QStringLiteral("plain link")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("<b>plain</b>")).isEmpty());
    CHECK(topicRect(editor, wrapped).height() > topicRect(editor, QStringLiteral("<b>plain</b>")).height());
    const QRectF initialVisible = graphics(editor).mapFromScene(renderedBounds(editor)).boundingRect();
    CHECK(QRectF(graphics(editor).viewport()->rect()).adjusted(-3, -3, 3, 3).contains(initialVisible));
    const QRectF sceneBounds = graphics(editor).scene()->sceneRect();
    const QRectF contentBounds = renderedBounds(editor);
    CHECK(sceneBounds.left() <= contentBounds.left() - 31 && sceneBounds.right() >= contentBounds.right() + 31);
    CHECK(sceneBounds.top() <= contentBounds.top() - 31 && sceneBounds.bottom() >= contentBounds.bottom() + 31);

    QSignalSpy changed(&editor, &Editor::documentChanged);
    for (auto direction : {Editor::LayoutDirection::Outline, Editor::LayoutDirection::Balanced,
                           Editor::LayoutDirection::Left, Editor::LayoutDirection::Right}) {
        CHECK(editor.setLayoutDirection(direction));
        CHECK(editor.layoutDirection() == direction);
        nodes = checkNodes();
        checkLinkLabels(nodes);
        assertTreeConnectors(editor, nodes);
        const qreal rootX = topicRect(editor, rootTopic).center().x();
        bool left = false, right = false;
        for (const QRectF &rectangle : {topicRect(editor, wrapped), topicRect(editor, QStringLiteral("<b>plain</b>")), emptyNodeRect(editor)}) {
            left = left || rectangle.center().x() < rootX;
            right = right || rectangle.center().x() > rootX;
            if (direction == Editor::LayoutDirection::Left) CHECK(rectangle.right() < topicRect(editor, rootTopic).left());
            if (direction == Editor::LayoutDirection::Right) CHECK(rectangle.left() > topicRect(editor, rootTopic).right());
        }
        if (direction == Editor::LayoutDirection::Balanced) CHECK(left && right);
        CHECK(exported(editor) == semantic && changed.isEmpty());
        assertFit(editor);
    }
    const auto directionBefore = editor.layoutDirection();
    const QRectF rootBefore = topicRect(editor, rootTopic);
    rejectUnchanged(editor, [&] { return editor.setLayoutDirection(static_cast<Editor::LayoutDirection>(91)); });
    CHECK(editor.layoutDirection() == directionBefore && topicRect(editor, rootTopic) == rootBefore);
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Balanced));
    editor.clearSelection();
    assertFit(editor);
    nodes = checkNodes();
    editor.clearSelection();
    const QRectF source = renderedBounds(editor).adjusted(-2, -2, 2, 2);
    const QImage image = paintScene(editor, source);
    for (const auto &rectangle : nodes) {
        const QRect region(imagePoint(image, source, rectangle.topLeft()), imagePoint(image, source, rectangle.bottomRight()));
        CHECK(paintedPixels(image, region) > 20);
    }
    int graphInk = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        if (!ink(image.pixel(x, y))) continue;
        const QPointF point(source.left() + (x + 0.5) * source.width() / image.width(),
                            source.top() + (y + 0.5) * source.height() / image.height());
        bool inNode = false;
        for (const auto &rectangle : nodes) if (rectangle.adjusted(-3, -3, 3, 3).contains(point)) inNode = true;
        if (!inNode) ++graphInk;
    }
    CHECK(graphInk > 80);
    const auto *self = ownerItem(textItem(editor, QStringLiteral("Self loop")));
    CHECK(self->sceneBoundingRect().top() < topicRect(editor, wrapped).top());
    CHECK(self->sceneBoundingRect().right() > topicRect(editor, wrapped).right());
    const QPoint routeOne = curvePoint(editor, QStringLiteral("Related"), nodes);
    QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, routeOne);
    CHECK(editor.selectedLinkId() == QStringLiteral("l1"));
    const QPoint routeTwo = curvePoint(editor, QStringLiteral("Parallel route"), nodes);
    QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, routeTwo);
    CHECK(editor.selectedLinkId() == QStringLiteral("l4"));
    editor.clearSelection();

    const QImage unselected = paintScene(editor, source);
    CHECK(editor.selectNode(QStringLiteral("a")));
    CHECK(paintScene(editor, source) != unselected);
    editor.clearSelection();
    const QImage oldPalette = paintScene(editor, source);
    QPalette changedPalette = palette;
    changedPalette.setColor(QPalette::WindowText, QColor(160, 15, 30));
    changedPalette.setColor(QPalette::Text, QColor(160, 15, 30));
    changedPalette.setColor(QPalette::ButtonText, QColor(160, 15, 30));
    editor.setPalette(changedPalette);
    pump();
    CHECK(paintScene(editor, source) != oldPalette);
    const qreal oldHeight = topicRect(editor, wrapped).height();
    font.setPixelSize(23);
    editor.setFont(font);
    pump();
    CHECK(topicRect(editor, wrapped).height() > oldHeight);
    checkLinkLabels(checkNodes());
    CHECK(exported(editor) == semantic && changed.isEmpty());
    assertFit(editor);
    const QByteArray screenshot = qgetenv("M3_QT_SCREENSHOT");
    if (!screenshot.isEmpty()) CHECK(editor.grab().save(QString::fromLocal8Bit(screenshot)));

    // A directed endpoint must add painted arrow geometry, not merely retain
    // the directed boolean in the document. Compare only a loose endpoint area.
    Editor arrowEditor;
    arrowEditor.setFont(font);
    arrowEditor.setPalette(palette);
    CHECK(arrowEditor.loadJson(QByteArray(R"({"schemaVersion":1,"rootId":"r","nodes":[{"id":"r","topic":"Arrow root","children":["n"]},{"id":"n","topic":"Arrow target"}],"crossLinks":[]})")));
    CHECK(arrowEditor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(arrowEditor);
    arrowEditor.clearSelection();
    const QRectF parent = topicRect(arrowEditor, QStringLiteral("Arrow root"));
    const QRectF child = topicRect(arrowEditor, QStringLiteral("Arrow target"));
    const QRectF treeSource = renderedBounds(arrowEditor).adjusted(-4, -4, 4, 4);
    const QImage treeImage = paintScene(arrowEditor, treeSource);
    const QPoint treeMidpoint = imagePoint(treeImage, treeSource,
        QPointF((parent.right() + child.left()) / 2, parent.center().y()));
    CHECK(paintedPixels(treeImage, QRect(treeMidpoint - QPoint(4, 4), QSize(9, 9))) > 2);
    const QString arrowId = arrowEditor.addLink(QStringLiteral("r"), QStringLiteral("n"), false, QStringLiteral("Arrow label"));
    CHECK(!arrowId.isEmpty());
    arrowEditor.clearSelection();
    const QRectF arrowSource = renderedBounds(arrowEditor).adjusted(-32, -32, 32, 32);
    const QImage undirected = paintScene(arrowEditor, arrowSource);
    const QRectF target = topicRect(arrowEditor, QStringLiteral("Arrow target"));
    CHECK(arrowEditor.updateLink(arrowId, QStringLiteral("r"), QStringLiteral("n"), true, QStringLiteral("Arrow label")));
    const QImage directed = paintScene(arrowEditor, arrowSource);
    const QPoint endpoint = imagePoint(directed, arrowSource, QPointF(target.left(), target.center().y()));
    int arrowDifference = 0;
    const QRect endpointRegion = QRect(endpoint - QPoint(18, 18), QSize(37, 37)).intersected(directed.rect());
    for (int y = endpointRegion.top(); y <= endpointRegion.bottom(); ++y)
        for (int x = endpointRegion.left(); x <= endpointRegion.right(); ++x)
            if (directed.pixel(x, y) != undirected.pixel(x, y)) ++arrowDifference;
    CHECK(arrowDifference > 5);
}

static void routing_case() {
    Json input = Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[
        {"id":"r","topic":"Root","children":["a","b","c","d","e"]},
        {"id":"a","topic":"Start"},
        {"id":"b","topic":"Wide upper obstacle\nwith several lines\nand tags","tags":["large badge","another badge"]},
        {"id":"c","topic":"Loop","children":["f"]},
        {"id":"d","topic":"Wide lower obstacle\nwith several lines"},
        {"id":"e","topic":"End"},{"id":"f","topic":"Child"}],
        "crossLinks":[
        {"id":"across","source":"a","target":"e","directed":true,"topic":"Across siblings"},
        {"id":"return","source":"e","target":"a","directed":true,"topic":"Return"},
        {"id":"loop1","source":"c","target":"c","directed":true,"topic":"Small loop"},
        {"id":"loop2","source":"c","target":"c","directed":true,"topic":"Middle loop"},
        {"id":"loop3","source":"c","target":"c","directed":true,"topic":"Outer loop"},
        {"id":"child","source":"f","target":"a","directed":false,"topic":"Child link"}]})");
    Editor editor;
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(15);
    editor.setFont(font);
    CHECK(editor.loadJson(encoded(input)));
    showEditor(editor);
    auto verify = [&](bool collapsed) {
        editor.clearSelection();
        assertFit(editor);
        const Json doc = exported(editor);
        std::vector<QRectF> nodes;
        for (const auto &node : doc.at("nodes")) {
            if (collapsed && node.at("id") == "f") continue;
            nodes.push_back(topicRect(editor, qs(node.at("topic"))));
        }
        for (const auto &link : doc.at("crossLinks")) {
            const QString label = qs(link.at("topic"));
            if (collapsed && link.at("id") == "child") {
                CHECK(texts(editor, label).isEmpty());
                continue;
            }
            auto *item = ownerItem(textItem(editor, label));
            const QPainterPath shape = item->mapToScene(item->shape());
            const QRectF source = topicRect(editor, qs(record(doc, "nodes", qs(link.at("source"))).at("topic")));
            const QRectF target = topicRect(editor, qs(record(doc, "nodes", qs(link.at("target"))).at("topic")));
            for (const auto &node : nodes) {
                if (shape.intersects(node == source || node == target ? node.adjusted(8, 8, -8, -8) : node))
                    throw std::runtime_error("Link " + utf8(label) + " crosses node at " +
                                             std::to_string(node.x()) + "," + std::to_string(node.y()));
            }
            clickLabel(editor, label);
            CHECK(editor.selectedLinkId() == qs(link.at("id")));
            editor.clearSelection();
            // Crowded self-loops may share an approach corridor; their labels
            // above must still select each link independently.
            if (link.at("source") != link.at("target")) {
                const QPoint curve = curvePoint(editor, label, nodes);
                QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, curve);
                CHECK(editor.selectedLinkId() == qs(link.at("id")));
            }
        }
    };
    for (const auto direction : {Editor::LayoutDirection::Right, Editor::LayoutDirection::Left}) {
        CHECK(editor.setLayoutDirection(direction));
        verify(false);
        CHECK(editor.setExpanded(QStringLiteral("c"), false));
        verify(true);
        CHECK(editor.setExpanded(QStringLiteral("c"), true));
        verify(false);
    }
    CHECK(editor.renameNode(QStringLiteral("d"), QStringLiteral("Resized lower obstacle\nExtra line\nExtra line\nExtra line")));
    verify(false);
}

static void empty_space_panning_case() {
    Editor editor;
    showEditor(editor);
    auto &view = graphics(editor);
    const Json original = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    const auto cursor = view.viewport()->cursor().shape();
    const qreal scale = view.transform().m11();
    const QPointF anchor = topicRect(editor, QStringLiteral("Central topic")).center();
    const QPoint before = view.mapFromScene(anchor);
    const QPoint start = blankPoint(view), delta(85, 55);
    auto move = [&](QPoint point, Qt::MouseButtons buttons) {
        QMouseEvent event(QEvent::MouseMove, QPointF(point), QPointF(view.viewport()->mapToGlobal(point)),
                          Qt::NoButton, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &event);
        pump();
    };
    move(start, Qt::NoButton);
    CHECK(view.viewport()->cursor().shape() == Qt::OpenHandCursor);
    CHECK(view.cursor().shape() == cursor);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
    CHECK(editor.selectedNodeId().isEmpty());
    CHECK(view.viewport()->cursor().shape() == Qt::ClosedHandCursor);
    for (int step = 1; step <= 12; ++step) move(start + delta * step / 12, Qt::LeftButton);
    // A fitted single-node scene has no scroll range: content must still follow the pointer.
    CHECK(QLineF(view.mapFromScene(anchor), before + delta).length() <= 2);
    QTest::mousePress(view.viewport(), Qt::MiddleButton, Qt::NoModifier, start + delta);
    QTest::mouseRelease(view.viewport(), Qt::MiddleButton, Qt::NoModifier, start + delta);
    CHECK(view.viewport()->cursor().shape() == Qt::ClosedHandCursor);
    move(start + 2 * delta, Qt::LeftButton);
    CHECK(QLineF(view.mapFromScene(anchor), before + 2 * delta).length() <= 3);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, start + 2 * delta);
    CHECK(view.viewport()->cursor().shape() == Qt::OpenHandCursor);
    const QPoint after = view.mapFromScene(anchor);
    move(start + 3 * delta, Qt::NoButton);
    CHECK(view.mapFromScene(anchor) == after);
    CHECK(view.transform().m11() == scale);
    CHECK(exported(editor) == original && changed.isEmpty());

    // A second-button release must not interrupt the existing middle-button gesture either.
    const QPoint middleStart = view.mapFromScene(anchor);
    QTest::mousePress(view.viewport(), Qt::MiddleButton, Qt::NoModifier, middleStart);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, middleStart);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, middleStart);
    CHECK(view.viewport()->cursor().shape() == Qt::ClosedHandCursor);
    move(middleStart - delta, Qt::MiddleButton);
    QTest::mouseRelease(view.viewport(), Qt::MiddleButton, Qt::NoModifier, middleStart - delta);
    CHECK(QLineF(view.mapFromScene(anchor), after - delta).length() <= 2);
    CHECK(view.viewport()->cursor().shape() == cursor);

    CHECK(editor.loadJson(encoded(editorFixture())));
    pump();
    const Json fixtureBefore = exported(editor);
    changed.clear();
    auto dragWithoutPan = [&](QPoint point, Qt::MouseButton button, bool node = false) {
        QTest::mousePress(view.viewport(), button, Qt::NoModifier, point);
        // Picking may scroll the selected item into view; dragging must not pan it further.
        const QPointF center = view.mapToScene(view.viewport()->rect().center());
        move(point + delta, button);
        QTest::mouseRelease(view.viewport(), button, Qt::NoModifier, node ? blankPoint(view) : point + delta);
        CHECK(view.mapToScene(view.viewport()->rect().center()) == center);
    };
    const QPoint alphaPoint = labelPoint(editor, QStringLiteral("Alpha"));
    move(alphaPoint, Qt::NoButton);
    CHECK(view.viewport()->cursor().shape() == cursor);
    dragWithoutPan(alphaPoint, Qt::LeftButton, true);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    const QPoint linkPoint = labelPoint(editor, QStringLiteral("Related"));
    move(linkPoint, Qt::NoButton);
    CHECK(view.viewport()->cursor().shape() == cursor);
    dragWithoutPan(linkPoint, Qt::LeftButton);
    CHECK(editor.selectedLinkId() == QStringLiteral("l1"));
    dragWithoutPan(blankPoint(view), Qt::RightButton);
    CHECK(exported(editor) == fixtureBefore && changed.isEmpty());

    // Committing a topic rebuilds the scene; the original empty hit must still start the pan.
    clickLabel(editor, QStringLiteral("Alpha"), true);
    const QString renamed = QStringLiteral("A much wider topic committed before dragging the canvas");
    topicInput(editor).setPlainText(renamed);
    const QPoint editStart = blankPoint(view);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, editStart);
    CHECK(activeTopicInput(editor) == nullptr);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == utf8(renamed));
    const QPointF editedAnchor = topicRect(editor, renamed).center();
    const QPoint editBefore = view.mapFromScene(editedAnchor);
    move(editStart + delta, Qt::LeftButton);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, editStart + delta);
    CHECK(QLineF(view.mapFromScene(editedAnchor), editBefore + delta).length() <= 2);
    CHECK(changed.size() == 1);
}

static void node_drag_case() {
    const QString alpha = QStringLiteral("Alpha"), beta = QStringLiteral("Beta"), delta = QStringLiteral("Delta");
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), d = QStringLiteral("d"), r = QStringLiteral("r");
    auto prepare = [](Editor &editor) {
        CHECK(editor.loadJson(encoded(editorFixture())));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(editor);
        assertFit(editor);
    };
    auto children = [](Json &doc, const QString &id, Json ids) {
        for (auto &entry : doc.at("nodes")) if (entry.at("id") == utf8(id)) {
            entry["children"] = std::move(ids);
            return;
        }
        throw std::runtime_error("Missing drag fixture node");
    };
    auto press = [](Editor &editor, const QString &topic) {
        const QPoint point = labelPoint(editor, topic);
        QTest::mousePress(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, point);
        return point;
    };
    auto release = [](Editor &editor, QPoint point) {
        QTest::mouseRelease(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
    };
    {
        Editor editor;
        prepare(editor);
        CHECK(editor.moveNode(QStringLiteral("c"), b));
        assertFit(editor);
        auto &view = graphics(editor);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        press(editor, alpha);
        const QPointF center = view.mapToScene(view.viewport()->rect().center());
        const QTransform zoom = view.transform();
        const QPoint target = labelPoint(editor, beta);
        const QRectF targetRect = topicRect(editor, beta);
        const QImage normal = paintScene(editor, targetRect);
        movePointer(view, target, Qt::LeftButton);
        CHECK(exported(editor) == expected && changed.isEmpty() && errors.isEmpty());
        CHECK(editor.selectedNodeId() == a && !ownerItem(textItem(editor, beta))->isSelected());
        CHECK(view.mapToScene(view.viewport()->rect().center()) == center && view.transform() == zoom);
        CHECK(view.viewport()->cursor().shape() == Qt::DragMoveCursor);
        CHECK(paintScene(editor, targetRect) != normal);
        release(editor, target);
        children(expected, r, Json::array({"b"}));
        children(expected, b, Json::array({"c", "a"}));
        CHECK(exported(editor) == nativeDocument(expected));
        CHECK(changed.size() == 1 && errors.isEmpty() && editor.selectedNodeId() == a);
        CHECK(!texts(editor, alpha).isEmpty() && !texts(editor, delta).isEmpty());
        CHECK(view.viewport()->cursor().shape() != Qt::DragMoveCursor);
        assertFit(editor);
        press(editor, alpha);
        const QPoint root = labelPoint(editor, qs(record(expected, "nodes", r).at("topic")));
        movePointer(view, root, Qt::LeftButton);
        release(editor, root);
        children(expected, r, Json::array({"b", "a"}));
        children(expected, b, Json::array({"c"}));
        CHECK(exported(editor) == nativeDocument(expected));
        CHECK(changed.size() == 2 && errors.isEmpty() && editor.selectedNodeId() == a);
    }
    // Every ineligible destination is silent, including current-parent drops (no reorder).
    enum class Invalid { RootSource, Descendant, Self, Parent, Blank, Link, Outside };
    for (auto invalid : {Invalid::RootSource, Invalid::Descendant, Invalid::Self, Invalid::Parent,
                         Invalid::Blank, Invalid::Link, Invalid::Outside}) {
        Editor editor;
        prepare(editor);
        auto &view = graphics(editor);
        const Json before = exported(editor);
        const QString source = invalid == Invalid::RootSource ? qs(record(before, "nodes", r).at("topic")) :
                               invalid == Invalid::Descendant || invalid == Invalid::Self ? alpha : delta;
        const QString sourceId = invalid == Invalid::RootSource ? r :
                                 invalid == Invalid::Descendant || invalid == Invalid::Self ? a : d;
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        const QPoint from = press(editor, source);
        // Activate even for a self drop, at the exact viewport-pixel threshold.
        movePointer(view, from + QPoint(QApplication::startDragDistance(), 0), Qt::LeftButton);
        QPoint target;
        switch (invalid) {
        case Invalid::RootSource: target = labelPoint(editor, beta); break;
        case Invalid::Descendant: target = labelPoint(editor, delta); break;
        case Invalid::Self: target = labelPoint(editor, alpha); break;
        case Invalid::Parent: target = labelPoint(editor, alpha); break;
        case Invalid::Blank: target = blankPoint(view); break;
        case Invalid::Link: target = labelPoint(editor, QStringLiteral("Related")); break;
        case Invalid::Outside: target = QPoint(-20, -20); break;
        }
        movePointer(view, target, Qt::LeftButton);
        if (invalid != Invalid::RootSource) CHECK(view.viewport()->cursor().shape() == Qt::ForbiddenCursor);
        CHECK(editor.selectedNodeId() == sourceId);
        release(editor, target);
        CHECK(exported(editor) == before && changed.isEmpty() && errors.isEmpty());
        CHECK(editor.selectedNodeId() == sourceId);
    }
    {
        Editor editor;
        prepare(editor);
        auto &view = graphics(editor);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        press(editor, delta);
        const QPoint target = labelPoint(editor, beta);
        const QPointF center = view.mapToScene(view.viewport()->rect().center());
        // Pending and active drags both own secondary button input.
        QTest::mousePress(view.viewport(), Qt::MiddleButton, Qt::NoModifier, target);
        QTest::mouseRelease(view.viewport(), Qt::MiddleButton, Qt::NoModifier, target);
        movePointer(view, target, Qt::LeftButton);
        QTest::mousePress(view.viewport(), Qt::RightButton, Qt::NoModifier, target);
        QTest::mouseRelease(view.viewport(), Qt::RightButton, Qt::NoModifier, target);
        bool popupOpened = false;
        QTimer dismissPopup;
        QObject::connect(&dismissPopup, &QTimer::timeout, [&] {
            if (auto *popup = QApplication::activePopupWidget()) { popupOpened = true; popup->close(); }
        });
        dismissPopup.start(0);
        QContextMenuEvent menu(QContextMenuEvent::Mouse, target, view.viewport()->mapToGlobal(target));
        QCoreApplication::sendEvent(view.viewport(), &menu);
        pump();
        dismissPopup.stop();
        CHECK(!popupOpened && QApplication::activePopupWidget() == nullptr);
        CHECK(view.viewport()->cursor().shape() == Qt::DragMoveCursor);
        CHECK(view.mapToScene(view.viewport()->rect().center()) == center && editor.selectedNodeId() == d);
        release(editor, target);
        children(expected, a, Json::array());
        children(expected, b, Json::array({"d"}));
        CHECK(exported(editor) == nativeDocument(expected));
        CHECK(changed.size() == 1 && errors.isEmpty());
    }
    for (bool jitter : {false, true}) {
        Editor editor;
        prepare(editor);
        auto &view = graphics(editor);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        const QPoint from = press(editor, alpha);
        if (jitter) movePointer(view, from + QPoint(QApplication::startDragDistance() - 1, 0), Qt::LeftButton);
        CHECK(view.viewport()->cursor().shape() != Qt::DragMoveCursor &&
              view.viewport()->cursor().shape() != Qt::ForbiddenCursor);
        // Release coordinates alone must not turn a click into a drag.
        release(editor, labelPoint(editor, beta));
        CHECK(exported(editor) == before && changed.isEmpty() && errors.isEmpty());
        CHECK(editor.selectedNodeId() == a);
    }
    enum class Cancel { EscapeViewport, EscapeView, NoButtons, FocusView, FocusViewport, Ungrab, Hide, Replace };
    for (auto cancel : {Cancel::EscapeViewport, Cancel::EscapeView, Cancel::NoButtons, Cancel::FocusView,
                        Cancel::FocusViewport, Cancel::Ungrab, Cancel::Hide, Cancel::Replace}) {
        Editor editor;
        prepare(editor);
        auto &view = graphics(editor);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        press(editor, alpha);
        QPoint target = labelPoint(editor, beta);
        const QRectF targetRect = topicRect(editor, beta);
        const QImage normal = paintScene(editor, targetRect);
        movePointer(view, target, Qt::LeftButton);
        CHECK(view.viewport()->cursor().shape() == Qt::DragMoveCursor);
        CHECK(paintScene(editor, targetRect) != normal);
        switch (cancel) {
        case Cancel::EscapeViewport: QTest::keyClick(view.viewport(), Qt::Key_Escape); break;
        case Cancel::EscapeView: QTest::keyClick(&view, Qt::Key_Escape); break;
        case Cancel::NoButtons: movePointer(view, target, Qt::NoButton); break;
        case Cancel::FocusView:
        case Cancel::FocusViewport: {
            QFocusEvent event(QEvent::FocusOut, Qt::OtherFocusReason);
            QCoreApplication::sendEvent(cancel == Cancel::FocusView ? &view : view.viewport(), &event);
            break;
        }
        case Cancel::Ungrab: {
            QEvent event(QEvent::UngrabMouse);
            QCoreApplication::sendEvent(view.viewport(), &event);
            break;
        }
        case Cancel::Hide: editor.hide(); showEditor(editor); break;
        case Cancel::Replace:
            setTopic(expected, "r", QStringLiteral("Replacement root"));
            CHECK(editor.loadJson(encoded(expected)));
            changed.clear();
            target = labelPoint(editor, beta);
            break;
        }
        pump();
        if (cancel != Cancel::Replace) {
            CHECK(paintScene(editor, targetRect) == normal);
            CHECK(editor.selectedNodeId() == a);
        }
        release(editor, target);
        CHECK(exported(editor) == nativeDocument(expected) && changed.isEmpty() && errors.isEmpty());
        CHECK(view.viewport()->cursor().shape() != Qt::DragMoveCursor);
        if (cancel == Cancel::EscapeViewport || cancel == Cancel::EscapeView) {
            QTest::keyClick(view.viewport(), Qt::Key_Escape);
            CHECK(editor.selectedNodeId().isEmpty());
        }
    }
    {
        Editor editor;
        prepare(editor);
        CHECK(editor.setExpanded(b, false));
        assertFit(editor);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        press(editor, alpha);
        const QPoint target = labelPoint(editor, beta);
        movePointer(graphics(editor), target, Qt::LeftButton);
        release(editor, target);
        children(expected, r, Json::array({"b", "c"}));
        children(expected, b, Json::array({"a"}));
        CHECK(exported(editor) == nativeDocument(expected));
        CHECK(changed.size() == 1 && errors.isEmpty() && editor.selectedNodeId().isEmpty());
        CHECK(texts(editor, alpha).isEmpty() && texts(editor, delta).isEmpty());
        CHECK(editor.setExpanded(b, true));
        CHECK(!texts(editor, alpha).isEmpty() && !texts(editor, delta).isEmpty());
    }
    {
        Editor editor;
        prepare(editor);
        auto &view = graphics(editor);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        clickLabel(editor, alpha, true);
        const QString draft = QStringLiteral("A much wider Alpha draft committed at the start of a node drag");
        topicInput(editor).setPlainText(draft);
        const QPoint from = labelPoint(editor, delta);
        CHECK(!topicInput(editor).geometry().contains(from));
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
        CHECK(activeTopicInput(editor) == nullptr && editor.selectedNodeId() == d);
        CHECK(changed.size() == 1);
        const QPoint target = labelPoint(editor, beta);
        movePointer(view, target, Qt::LeftButton);
        release(editor, target);
        setTopic(expected, "a", draft);
        children(expected, a, Json::array());
        children(expected, b, Json::array({"d"}));
        CHECK(exported(editor) == nativeDocument(expected));
        CHECK(changed.size() == 2 && errors.isEmpty() && activeTopicInput(editor) == nullptr);
    }
    {
        Editor editor;
        prepare(editor);
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Left));
        assertFit(editor);
        trigger(editor, "zoomOut");
        auto &view = graphics(editor);
        const QTransform zoom = view.transform();
        CHECK(zoom.m11() != 1);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        press(editor, delta);
        const QPoint target = labelPoint(editor, beta);
        movePointer(view, target, Qt::LeftButton);
        release(editor, target);
        children(expected, a, Json::array());
        children(expected, b, Json::array({"d"}));
        CHECK(exported(editor) == nativeDocument(expected) && view.transform() == zoom);
        CHECK(changed.size() == 1 && errors.isEmpty());
    }
    {
        Editor editor;
        prepare(editor);
        auto &view = graphics(editor);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        press(editor, alpha);
        const QPoint target = labelPoint(editor, beta);
        const QRectF targetRect = topicRect(editor, beta);
        const QImage normal = paintScene(editor, targetRect);
        movePointer(view, target, Qt::LeftButton);
        CHECK(paintScene(editor, targetRect) != normal);
        // Eligibility must be recomputed on release, even without a final mouse move.
        release(editor, blankPoint(view));
        CHECK(exported(editor) == before && changed.isEmpty() && errors.isEmpty());
        CHECK(paintScene(editor, targetRect) == normal && editor.selectedNodeId() == a);
        CHECK(view.viewport()->cursor().shape() == Qt::OpenHandCursor);
    }
}

static void hyperlinks_case() {
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), alpha = QStringLiteral("Alpha");
    const QString rawUrl = QString::fromUtf8("opaque:世界/<b>café</b>?q=\"a&b\"#片 段");
    Json linkedInput = editorFixture();
    for (auto &entry : linkedInput.at("nodes")) if (entry.at("id") == "a") entry["hyperLink"] = utf8(rawUrl);
    auto prepare = [&](Editor &editor, bool linked = true) {
        CHECK(editor.loadJson(encoded(linked ? linkedInput : editorFixture())));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(editor, QSize(1100, 900));
        assertFit(editor);
    };
    auto indicator = [](Editor &editor, const QString &topic) -> QGraphicsItem * {
        for (auto *child : ownerItem(textItem(editor, topic))->childItems())
            if (child->isVisible() && child->cursor().shape() == Qt::PointingHandCursor && !child->toolTip().isEmpty())
                return child;
        return nullptr;
    };
    auto linkPoint = [&](Editor &editor, const QString &topic) {
        auto &view = graphics(editor);
        auto *item = indicator(editor, topic);
        CHECK(item != nullptr);
        view.ensureVisible(item);
        pump();
        const QPoint point = view.mapFromScene(item->sceneBoundingRect().center());
        CHECK(view.viewport()->rect().contains(point) && belongsTo(view.itemAt(point), item));
        return point;
    };
    auto urlInput = [](Editor &editor) {
        auto *input = editor.findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
        CHECK(input != nullptr && input->isVisible() && input->isEnabled());
        return input;
    };
    auto accessibleUrl = [&](Editor &editor, const QString &value) {
        // Assistive edits exercise the real panel without canceling the held mouse gesture through a focus change.
        auto *accessible = QAccessible::queryAccessibleInterface(urlInput(editor));
        CHECK(accessible != nullptr);
        accessible->setText(QAccessible::Value, value);
        pump();
    };
    {
        Editor editor;
        prepare(editor, false);
        auto &view = graphics(editor);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        const QString rootTopic = qs(record(exported(editor), "nodes", QStringLiteral("r")).at("topic"));
        CHECK(indicator(editor, rootTopic) != nullptr); // The shared root already has an opaque URL.
        CHECK(indicator(editor, alpha) == nullptr && indicator(editor, QStringLiteral("Beta")) == nullptr);
        CHECK(indicator(editor, QStringLiteral("Delta")) == nullptr);

        clickLabel(editor, alpha);
        auto *url = urlInput(editor);
        auto *scroll = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"))->findChild<QScrollArea *>();
        CHECK(scroll != nullptr);
        scroll->ensureWidgetVisible(url);
        url->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(url, Qt::Key_A, Qt::ControlModifier);
        QApplication::clipboard()->setText(rawUrl);
        QTest::keyClick(url, Qt::Key_V, Qt::ControlModifier);
        pump();
        CHECK(record(exported(editor), "nodes", a).at("hyperLink") == utf8(rawUrl));
        CHECK(changed.size() == 1 && activated.isEmpty());
        auto *item = indicator(editor, alpha);
        CHECK(item != nullptr && !item->sceneBoundingRect().intersects(textItem(editor, alpha)->sceneBoundingRect()));
        QTextDocument tooltip;
        tooltip.setHtml(item->toolTip());
        CHECK(tooltip.toPlainText() == rawUrl);

        clickLabel(editor, QStringLiteral("Beta"));
        const QPoint point = linkPoint(editor, alpha);
        const Json before = exported(editor);
        const QTransform zoom = view.transform();
        const QPointF center = view.mapToScene(view.viewport()->rect().center());
        changed.clear(); selected.clear();
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.isEmpty() && changed.isEmpty() && selected.isEmpty());
        CHECK(exported(editor) == before && editor.selectedNodeId() == b);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
        CHECK(activated.size() == 1 && activated.front().at(0).toString() == a && activated.front().at(1).toString() == rawUrl);
        CHECK(exported(editor) == before && changed.isEmpty() && selected.isEmpty());
        CHECK(editor.selectedNodeId() == b && editor.selectedLinkId().isEmpty() && activeTopicInput(editor) == nullptr);
        CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == center);

        QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier, point);
        QTest::mouseClick(view.viewport(), Qt::MiddleButton, Qt::NoModifier, point);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, labelPoint(editor, alpha));
        CHECK(activated.size() == 1);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        movePointer(view, point + QPoint(QApplication::startDragDistance() + 1, 0), Qt::LeftButton);
        movePointer(view, point, Qt::LeftButton);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
        CHECK(activated.size() == 1 && exported(editor) == before && changed.isEmpty());

        CHECK(editor.selectNode(b));
        selected.clear();
        // Qt delivers a second press before the double-click event, then a final release.
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.size() == 2);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
        CHECK(activated.size() == 2 && activeTopicInput(editor) == nullptr);
        CHECK(exported(editor) == before && changed.isEmpty() && selected.isEmpty() && editor.selectedNodeId() == b);
        CHECK(view.transform() == zoom);

        clickLabel(editor, alpha);
        CHECK(editor.selectedNodeId() == a && activated.size() == 2);
        clickLabel(editor, alpha, true);
        CHECK(activeTopicInput(editor) != nullptr && activated.size() == 2);
        topicKey(editor, Qt::Key_Escape);
        const QRectF nodeRect = topicRect(editor, alpha);
        const QPoint expansion = view.mapFromScene(QPointF(nodeRect.right() - 15, nodeRect.center().y()));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, expansion);
        pump();
        CHECK(!record(exported(editor), "nodes", a).at("expanded").get<bool>());
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty() && indicator(editor, alpha) != nullptr);
        CHECK(activated.size() == 2 && activeTopicInput(editor) == nullptr);
    }
    {
        Editor editor;
        prepare(editor);
        CHECK(editor.selectNode(a));
        auto &view = graphics(editor);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated);
        QPoint point = linkPoint(editor, alpha);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        const QString editedUrl = QString::fromUtf8("custom:更新?x=<tag>&y=é");
        accessibleUrl(editor, editedUrl);
        CHECK(record(exported(editor), "nodes", a).at("hyperLink") == utf8(editedUrl));
        point = linkPoint(editor, alpha);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.size() == 1 && activated.front().at(0).toString() == a && activated.front().at(1).toString() == editedUrl);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        accessibleUrl(editor, QString());
        CHECK(record(exported(editor), "nodes", a).at("hyperLink") == "" && indicator(editor, alpha) == nullptr);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.size() == 1);

        accessibleUrl(editor, rawUrl);
        point = linkPoint(editor, alpha);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        // Identical IDs and URLs in a fresh scene must not inherit the old scene's press.
        CHECK(editor.loadJson(encoded(linkedInput)));
        pump();
        point = linkPoint(editor, alpha);
        const Json replaced = exported(editor);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.size() == 1 && exported(editor) == replaced);

        // These events end a gesture even if release later returns to the same indicator.
        for (QEvent::Type cancel : {QEvent::KeyPress, QEvent::FocusOut, QEvent::UngrabMouse, QEvent::Hide}) {
            view.setFocus(Qt::OtherFocusReason);
            pump();
            point = linkPoint(editor, alpha);
            QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            if (cancel == QEvent::KeyPress) QTest::keyClick(view.viewport(), Qt::Key_Escape);
            else if (cancel == QEvent::FocusOut) {
                QFocusEvent event(QEvent::FocusOut, Qt::OtherFocusReason);
                QCoreApplication::sendEvent(&view, &event);
            } else if (cancel == QEvent::Hide) {
                editor.hide();
                showEditor(editor, QSize(1100, 900));
            } else {
                QEvent event(cancel);
                QCoreApplication::sendEvent(view.viewport(), &event);
            }
            point = linkPoint(editor, alpha);
            QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            CHECK(activated.size() == 1 && exported(editor) == replaced && activeTopicInput(editor) == nullptr);
        }
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.size() == 2 && activated.back().at(1).toString() == rawUrl);
    }
    {
        Editor editor;
        prepare(editor);
        auto &view = graphics(editor);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated);
        clickLabel(editor, QStringLiteral("Beta"), true);
        topicInput(editor).setPlainText(QStringLiteral("Zeta"));
        const QPoint point = linkPoint(editor, alpha);
        CHECK(!topicInput(editor).geometry().contains(point));
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activeTopicInput(editor) == nullptr && record(exported(editor), "nodes", b).at("topic") == "Zeta");
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint(editor, alpha));
        CHECK(activated.size() == 1 && activated.front().at(0).toString() == a && activated.front().at(1).toString() == rawUrl);

        bool replaced = false;
        QObject::connect(&editor, &Editor::nodeLinkActivated, &editor, [&](const QString &id, const QString &url) {
            CHECK(id == a && url == rawUrl);
            CHECK(editor.newDocument(QStringLiteral("Reentrant replacement")));
            replaced = true;
            CHECK(id == a && url == rawUrl);
        });
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint(editor, alpha));
        pump();
        CHECK(replaced && activated.size() == 2);
        CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == "Reentrant replacement");
        CHECK(indicator(editor, QStringLiteral("Reentrant replacement")) == nullptr);
    }
    {
        auto editor = std::make_unique<Editor>();
        QPointer<Editor> alive(editor.get());
        prepare(*editor);
        QSignalSpy activated(editor.get(), &Editor::nodeLinkActivated);
        bool delivered = false;
        QObject::connect(editor.get(), &Editor::nodeLinkActivated, [&](const QString &id, const QString &url) {
            CHECK(id == a && url == rawUrl);
            delivered = true;
            editor.reset();
            CHECK(id == a && url == rawUrl);
        });
        auto *viewport = graphics(*editor).viewport();
        const QPoint point = linkPoint(*editor, alpha);
        const QPoint global = viewport->mapToGlobal(point);
        QTest::mousePress(viewport, Qt::LeftButton, Qt::NoModifier, point);
        CHECK(!delivered);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), QPointF(global),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &release);
        pump();
        CHECK(delivered && alive.isNull() && activated.size() == 1);
        CHECK(activated.front().at(0).toString() == a && activated.front().at(1).toString() == rawUrl);
    }

    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString path = directory.filePath(QString::fromUtf8("café #1%.txt"));
    QFile file(path);
    CHECK(file.open(QIODevice::WriteOnly));
    file.close();
    const QString missingPath = directory.filePath(QStringLiteral("missing #2%.txt"));
    const QString fileUrl = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
    const QString missingUrl = QUrl::fromLocalFile(missingPath).toString(QUrl::FullyEncoded);
    QMimeData localFile;
    localFile.setUrls({QUrl::fromLocalFile(path)});
    const Qt::DropActions actions = Qt::CopyAction | Qt::MoveAction;
    auto enterFile = [](Editor &editor, const QMimeData &mime, Qt::DropActions allowed, const QPoint &point, bool accepted = true) {
        QDragEnterEvent event(point, allowed, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(graphics(editor).viewport(), &event);
        CHECK(event.isAccepted() == accepted);
        if (accepted) CHECK(event.dropAction() == Qt::CopyAction);
    };
    auto moveFile = [](Editor &editor, const QMimeData &mime, Qt::DropActions allowed, const QPoint &point, bool accepted = true) {
        QDragMoveEvent event(point, allowed, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(graphics(editor).viewport(), &event);
        CHECK(event.isAccepted() == accepted);
        if (accepted) CHECK(event.dropAction() == Qt::CopyAction);
    };
    auto dropFile = [](Editor &editor, const QMimeData &mime, Qt::DropActions allowed, const QPoint &point, bool accepted = true) {
        QDropEvent event(QPointF(point), allowed, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(graphics(editor).viewport(), &event);
        CHECK(event.isAccepted() == accepted);
        if (accepted) CHECK(event.dropAction() == Qt::CopyAction);
        pump();
    };
    auto withUrl = [](Json doc, const QString &id, const QString &url) {
        for (auto &entry : doc.at("nodes")) if (entry.at("id") == utf8(id)) entry["hyperLink"] = utf8(url);
        return doc;
    };
    class RelativeEditor : public Editor {
    public:
        QString base;
        mutable QStringList received;
        bool veto = false;
    protected:
        QString resolveDroppedFileUrl(const QString &filePath) const override {
            received.append(filePath);
            return veto ? QString() : QDir(base).relativeFilePath(filePath);
        }
    };
    {
        Editor editor;
        prepare(editor, false);
        CHECK(editor.selectNode(b));
        auto &view = graphics(editor);
        const Json before = exported(editor);
        const QTransform transform = view.transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated), errors(&editor, &Editor::errorOccurred);
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, localFile, actions, labelPoint(editor, alpha));
        CHECK(exported(editor) == before && editor.selectedNodeId() == b);
        CHECK(changed.isEmpty() && selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
        dropFile(editor, localFile, actions, labelPoint(editor, alpha));
        CHECK(exported(editor) == withUrl(before, a, fileUrl));
        CHECK(editor.selectedNodeId() == b && editor.selectedLinkId().isEmpty());
        CHECK(changed.size() == 1 && selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
        CHECK(view.transform() == transform);
        CHECK(editor.selectNode(a));
        CHECK(urlInput(editor)->text() == fileUrl);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint(editor, alpha));
        CHECK(activated.size() == 1 && activated.back().at(0).toString() == a && activated.back().at(1).toString() == fileUrl);
        selected.clear();
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, localFile, actions, labelPoint(editor, alpha));
        dropFile(editor, localFile, actions, labelPoint(editor, alpha));
        CHECK(changed.size() == 1 && exported(editor) == withUrl(before, a, fileUrl));
        QMimeData missingFile;
        missingFile.setUrls({QUrl::fromLocalFile(missingPath)});
        enterFile(editor, missingFile, actions, blankPoint(view));
        moveFile(editor, missingFile, actions, linkPoint(editor, alpha));
        dropFile(editor, missingFile, actions, linkPoint(editor, alpha));
        CHECK(exported(editor) == withUrl(before, a, missingUrl) && urlInput(editor)->text() == missingUrl);
        CHECK(changed.size() == 2 && selected.isEmpty() && errors.isEmpty() && activated.size() == 1);
        CHECK(editor.selectedNodeId() == a && view.transform() == transform);
    }
    {
        RelativeEditor editor;
        editor.base = directory.path();
        prepare(editor, false);
        CHECK(editor.selectNode(a));
        // Loading, ordinary field edits, and activation must bypass the resolver.
        accessibleUrl(editor, rawUrl);
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint(editor, alpha));
        CHECK(editor.received.isEmpty());
        CHECK(editor.selectNode(b));
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated), errors(&editor, &Editor::errorOccurred);
        enterFile(editor, localFile, actions, blankPoint(graphics(editor)));
        moveFile(editor, localFile, actions, labelPoint(editor, alpha));
        CHECK(editor.received.isEmpty() && exported(editor) == before);
        dropFile(editor, localFile, actions, labelPoint(editor, alpha));
        const QString relative = QDir(directory.path()).relativeFilePath(path);
        CHECK(editor.received == QStringList{path});
        CHECK(exported(editor) == withUrl(before, a, relative));
        CHECK(changed.size() == 1 && selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint(editor, alpha));
        CHECK(activated.size() == 1 && activated.back().at(0).toString() == a && activated.back().at(1).toString() == relative);
        CHECK(editor.received.size() == 1);
        editor.veto = true;
        const Json resolved = exported(editor);
        changed.clear();
        enterFile(editor, localFile, actions, blankPoint(graphics(editor)));
        moveFile(editor, localFile, actions, labelPoint(editor, alpha));
        CHECK(editor.received.size() == 1);
        dropFile(editor, localFile, actions, labelPoint(editor, alpha));
        CHECK(editor.received == (QStringList{path, path}));
        CHECK(exported(editor) == resolved && editor.selectedNodeId() == b);
        CHECK(changed.isEmpty() && selected.isEmpty() && errors.isEmpty() && activated.size() == 1);
    }
    {
        RelativeEditor editor;
        editor.base = directory.path();
        prepare(editor, false);
        CHECK(editor.selectNode(b));
        auto &view = graphics(editor);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated), errors(&editor, &Editor::errorOccurred);
        QMimeData textOnly, remote, empty, multiple, invalid, relative, nul, emptyPath;
        textOnly.setText(path);
        remote.setUrls({QUrl(QStringLiteral("https://example.test/file.txt"))});
        empty.setUrls({});
        multiple.setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(missingPath)});
        invalid.setUrls({QUrl(QStringLiteral("file://[invalid"))});
        relative.setUrls({QUrl::fromLocalFile(QStringLiteral("relative.txt"))});
        nul.setUrls({QUrl::fromLocalFile(path + QChar::Null)});
        emptyPath.setUrls({QUrl(QStringLiteral("file:"))});
        for (const QMimeData *mime : {&textOnly, &remote, &empty, &multiple, &invalid, &relative, &nul, &emptyPath}) {
            enterFile(editor, *mime, actions, labelPoint(editor, alpha), false);
            CHECK(editor.received.isEmpty() && exported(editor) == before);
        }
        enterFile(editor, localFile, Qt::MoveAction, labelPoint(editor, alpha), false);
        const std::vector<std::function<QPoint()>> rejectedPoints{
            [&] { return blankPoint(view); },
            [&] { return labelPoint(editor, QStringLiteral("Related")); },
            [&] { return QPoint(-10, view.viewport()->height() / 2); }
        };
        for (const auto &point : rejectedPoints) {
            enterFile(editor, localFile, actions, blankPoint(view));
            moveFile(editor, localFile, actions, point(), false);
            dropFile(editor, localFile, actions, point(), false);
            CHECK(editor.received.isEmpty() && exported(editor) == before);
        }
        // The final point and payload/actions are independently checked at drop time.
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, localFile, actions, labelPoint(editor, alpha));
        dropFile(editor, localFile, actions, blankPoint(view), false);
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, localFile, Qt::MoveAction, labelPoint(editor, alpha), false);
        dropFile(editor, localFile, Qt::MoveAction, labelPoint(editor, alpha), false);
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, remote, actions, labelPoint(editor, alpha), false);
        dropFile(editor, remote, actions, labelPoint(editor, alpha), false);
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, localFile, actions, labelPoint(editor, alpha));
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(view.viewport(), &leave);
        CHECK(editor.received.isEmpty() && exported(editor) == before && editor.selectedNodeId() == b);
        CHECK(changed.isEmpty() && selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
        const QString root = QStringLiteral("r"), relativeUrl = QDir(directory.path()).relativeFilePath(path);
        const QString rootTopic = qs(record(before, "nodes", root).at("topic"));
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, localFile, actions, labelPoint(editor, rootTopic));
        dropFile(editor, localFile, actions, labelPoint(editor, rootTopic));
        CHECK(exported(editor) == withUrl(before, root, relativeUrl));
        CHECK(changed.size() == 1 && editor.received.size() == 1);
        CHECK(editor.setExpanded(a, false));
        const Json collapsed = exported(editor);
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
        changed.clear();
        enterFile(editor, localFile, actions, blankPoint(view));
        moveFile(editor, localFile, actions, labelPoint(editor, alpha));
        dropFile(editor, localFile, actions, labelPoint(editor, alpha));
        CHECK(exported(editor) == withUrl(collapsed, a, relativeUrl));
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
        CHECK(changed.size() == 1 && editor.received.size() == 2 && editor.selectedNodeId() == b);
        CHECK(selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
    }
    {
        Editor editor;
        prepare(editor, false);
        clickLabel(editor, QStringLiteral("Beta"), true);
        const QString draft = QStringLiteral("Beta draft not yet accepted");
        topicInput(editor).setPlainText(draft);
        const QPoint point = labelPoint(editor, alpha);
        CHECK(!topicInput(editor).geometry().contains(point));
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated), errors(&editor, &Editor::errorOccurred);
        enterFile(editor, localFile, actions, blankPoint(graphics(editor)));
        moveFile(editor, localFile, actions, point);
        dropFile(editor, localFile, actions, point);
        CHECK(exported(editor) == withUrl(before, a, fileUrl));
        CHECK(topicInput(editor).toPlainText() == draft && editor.selectedNodeId() == b);
        CHECK(changed.size() == 1 && selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
        topicKey(editor, Qt::Key_Return);
        Json accepted = withUrl(before, a, fileUrl);
        setTopic(accepted, "b", draft);
        CHECK(exported(editor) == accepted && activeTopicInput(editor) == nullptr);
        CHECK(changed.size() == 2 && selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
    }
}

static void emoji_category_popup_case() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor, QSize(1100, 900));
    CHECK(editor.selectNode(QStringLiteral("a")));
    auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
    auto *icons = panel->findChild<QLineEdit *>(QStringLiteral("nodeIcons"));
    auto *url = panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
    auto *scroll = panel->findChild<QScrollArea *>();
    CHECK(icons && url && scroll);
    scroll->ensureWidgetVisible(icons);
    icons->setFocus();
    pump();
    auto *popup = icons->findChild<QWidget *>(QStringLiteral("emojiPopup"));
    CHECK(popup && popup->isVisible());
    CHECK(QTest::qWaitFor([] { return QToolTip::isVisible(); }));
    const QString navigationTip = QToolTip::text();
    auto navigationTipVisible = [&] { return QToolTip::isVisible() && QToolTip::text() == navigationTip; };
    QTest::keyClick(icons, Qt::Key_Escape);
    CHECK(QTest::qWaitFor([&] { return !navigationTipVisible(); }));
    CHECK(!popup->isVisible() && icons->hasFocus());
    QTest::mouseClick(icons, Qt::LeftButton);
    pump();
    CHECK(popup->isVisible() && !navigationTipVisible());
    {
        Editor other;
        showEditor(other);
        auto *otherIcons = other.findChild<QLineEdit *>(QStringLiteral("nodeIcons"));
        auto *otherPanel = other.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        otherPanel->findChild<QScrollArea *>()->ensureWidgetVisible(otherIcons);
        otherIcons->setFocus();
        pump();
        auto *otherPopup = otherIcons->findChild<QWidget *>(QStringLiteral("emojiPopup"));
        CHECK(otherPopup && otherPopup->isVisible() && !navigationTipVisible());
    }
    editor.activateWindow();
    icons->setFocus();
    pump();
    CHECK(popup->isVisible() && !navigationTipVisible());
    const Json before = exported(editor);
    // Exercise the native-window stage that QWidget-only mouse tests bypass.
    const QPoint border = popup->rect().topLeft();
    QMouseEvent windowPress(QEvent::MouseButtonPress, QPointF(border), QPointF(popup->mapToGlobal(border)),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(popup->windowHandle(), &windowPress);
    QMouseEvent windowRelease(QEvent::MouseButtonRelease, QPointF(border), QPointF(popup->mapToGlobal(border)),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(popup->windowHandle(), &windowRelease);
    pump();
    CHECK(popup->isVisible());
    // Native tool activation can reach the owner before button/focus state settles.
    QEvent deactivate(QEvent::WindowDeactivate);
    QApplication::sendEvent(&editor, &deactivate);
    pump();
    CHECK(popup->isVisible());
    auto *categories = icons->findChild<QComboBox *>(QStringLiteral("emojiCategories"));
    auto *choices = icons->findChild<QListView *>(QStringLiteral("emojiChoices"));
    CHECK(categories && choices);
    auto checkFilledRow = [&] {
        choices->scrollToTop();
        pump();
        CHECK(choices->model()->rowCount() > 1);
        const QRect first = choices->visualRect(choices->model()->index(0, 0));
        QRect previous;
        int columns = 0;
        for (int row = 0; row < choices->model()->rowCount(); ++row) {
            const QRect cell = choices->visualRect(choices->model()->index(row, 0));
            if (cell.top() != first.top()) break;
            CHECK(choices->viewport()->rect().contains(cell));
            if (columns) CHECK(previous.right() + 1 == cell.left());
            previous = cell;
            ++columns;
        }
        CHECK(columns > 0 && columns < choices->model()->rowCount());
        const int gutter = choices->verticalScrollBar()->isVisible() ? 0 : choices->verticalScrollBar()->sizeHint().width();
        const int unused = choices->viewport()->rect().right() - previous.right();
        // Only the native scrollbar gutter and integer division remainder may remain.
        CHECK(unused <= gutter + 2 * choices->frameWidth() + columns);
        CHECK(choices->horizontalScrollBar()->maximum() == 0);
    };
    checkFilledRow();
    const QSize originalPopupSize = popup->size();
    for (int width : {337, 517}) {
        popup->resize(width, originalPopupSize.height());
        checkFilledRow();
    }
    const QFont originalFont = icons->font();
    QFont largerFont = originalFont;
    largerFont.setPointSizeF(qMax(qreal(12), originalFont.pointSizeF() + 4));
    icons->setFont(largerFont);
    checkFilledRow();
    icons->setFont(originalFont);
    popup->resize(originalPopupSize);
    const QString originalIcons = icons->text();
    icons->setText(QStringLiteral("heart"));
    checkFilledRow();
    icons->setText(QStringLiteral("tree"));
    checkFilledRow();
    CHECK(!choices->verticalScrollBar()->isVisible());
    icons->setText(QStringLiteral("rocket"));
    pump();
    CHECK(!choices->verticalScrollBar()->isVisible() && choices->horizontalScrollBar()->maximum() == 0);
    icons->setText(QStringLiteral("no-such-emoji-xyz"));
    pump();
    CHECK(choices->model()->rowCount() == 0 && choices->horizontalScrollBar()->maximum() == 0);
    icons->setText(originalIcons);
    checkFilledRow();
    CHECK(exported(editor) == before);
    {
        QAction hostShortcut(&editor);
        hostShortcut.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
        hostShortcut.setShortcutContext(Qt::WindowShortcut);
        editor.addAction(&hostShortcut);
        QSignalSpy hostTriggered(&hostShortcut, &QAction::triggered);
        icons->setText(QStringLiteral("heart"));
        icons->setSelection(1, 2);
        const QString text = icons->text(), selectedText = icons->selectedText();
        const int caret = icons->cursorPosition(), selectionStart = icons->selectionStart();
        const Json navigationDocument = exported(editor);
        auto navigate = [&](Qt::Key key) {
            QTest::keyClick(icons, key, Qt::ControlModifier);
            pump();
            CHECK(popup->isVisible() && icons->hasFocus());
            CHECK(icons->text() == text && icons->cursorPosition() == caret &&
                  icons->selectionStart() == selectionStart && icons->selectedText() == selectedText);
            CHECK(exported(editor) == navigationDocument && hostTriggered.isEmpty());
            return choices->currentIndex();
        };
        for (int width : {337, 517}) {
            popup->resize(width, originalPopupSize.height());
            pump();
            const QModelIndex first = choices->model()->index(0, 0), right = choices->model()->index(1, 0);
            choices->setCurrentIndex(first);
            choices->scrollToTop();
            const QRect firstCell = choices->visualRect(first), rightCell = choices->visualRect(right);
            CHECK(rightCell.top() == firstCell.top() && rightCell.left() > firstCell.right());
            int belowRow = 1;
            while (belowRow < choices->model()->rowCount() &&
                   choices->visualRect(choices->model()->index(belowRow, 0)).top() == firstCell.top()) ++belowRow;
            CHECK(belowRow < choices->model()->rowCount());
            const QModelIndex below = choices->model()->index(belowRow, 0);
            CHECK(choices->visualRect(below).left() == firstCell.left());
            CHECK(navigate(Qt::Key_L) == right);
            CHECK(navigate(Qt::Key_H) == first);
            CHECK(navigate(Qt::Key_J) == below);
            CHECK(navigate(Qt::Key_K) == first);
            CHECK(navigate(Qt::Key_H) == first && navigate(Qt::Key_K) == first);
            const QModelIndex last = choices->model()->index(choices->model()->rowCount() - 1, 0);
            choices->setCurrentIndex(last);
            CHECK(navigate(Qt::Key_J) == last && navigate(Qt::Key_L) == last);
            CHECK(choices->viewport()->rect().contains(choices->visualRect(last)));
        }
        choices->setLayoutDirection(Qt::RightToLeft);
        const QModelIndex rtlFirst = choices->model()->index(0, 0);
        choices->setCurrentIndex(rtlFirst);
        choices->scrollToTop();
        pump();
        const QRect rtlFirstCell = choices->visualRect(rtlFirst);
        CHECK(choices->visualRect(navigate(Qt::Key_H)).right() < rtlFirstCell.left());
        CHECK(navigate(Qt::Key_L) == rtlFirst);
        choices->setLayoutDirection(Qt::LeftToRight);
        icons->setText(QStringLiteral("no-such-emoji-xyz"));
        pump();
        const Json emptySearchDocument = exported(editor);
        for (auto key : {Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L}) QTest::keyClick(icons, key, Qt::ControlModifier);
        pump();
        CHECK(icons->text() == QStringLiteral("no-such-emoji-xyz") && exported(editor) == emptySearchDocument);
        CHECK(!choices->currentIndex().isValid() && popup->isVisible() && hostTriggered.isEmpty());
        QTest::keyClick(icons, Qt::Key_Escape);
        CHECK(!popup->isVisible() && icons->hasFocus());
        QTest::keyClick(icons, Qt::Key_L, Qt::ControlModifier);
        CHECK(hostTriggered.size() == 1);
        icons->setText(originalIcons);
        popup->resize(originalPopupSize);
        pump();
        CHECK(exported(editor) == before);
    }
    auto hasEmoji = [&](const QString &name) {
        for (int row = 0; row < choices->model()->rowCount(); ++row)
            if (choices->model()->index(row, 0).data().toString() == name) return true;
        return false;
    };
    CHECK(hasEmoji(QStringLiteral("red apple")) && hasEmoji(QStringLiteral("grinning face")));
    QTest::mouseClick(categories, Qt::LeftButton);
    pump();
    CHECK(popup->isVisible() && categories->view()->isVisible());
    const int food = categories->findText(QStringLiteral("Food & Drink"));
    CHECK(food >= 0);
    const QModelIndex foodIndex = categories->model()->index(food, 0);
    categories->view()->scrollTo(foodIndex);
    pump();
    const QPoint foodPoint = categories->view()->visualRect(foodIndex).center();
    QTest::mouseMove(categories->view()->viewport(), foodPoint);
    QTest::mouseClick(categories->view()->viewport(), Qt::LeftButton, Qt::NoModifier, foodPoint);
    pump();
    CHECK(popup->isVisible());
    CHECK(!categories->view()->isVisible());
    CHECK(icons->hasFocus());
    CHECK(categories->currentIndex() == food);
    CHECK(hasEmoji(QStringLiteral("red apple")) && !hasEmoji(QStringLiteral("grinning face")));
    CHECK(exported(editor) == before);
    QTest::keyClicks(icons, "apple");
    pump();
    CHECK(popup->isVisible() && hasEmoji(QStringLiteral("red apple")) && !hasEmoji(QStringLiteral("banana")));
    QTest::mouseClick(categories, Qt::LeftButton);
    pump();
    QTest::keyClick(categories->view(), Qt::Key_Escape);
    pump();
    CHECK(popup->isVisible() && !categories->view()->isVisible() && icons->hasFocus());
    QTest::keyClick(icons, Qt::Key_Tab);
    pump();
    CHECK(!popup->isVisible() && !icons->hasFocus());
    icons->setFocus();
    pump();
    CHECK(popup->isVisible());
    QTest::mouseClick(url, Qt::LeftButton);
    pump();
    CHECK(!popup->isVisible());
}

static void properties_case() {
    emoji_category_popup_case();
    Editor editor;
    Json input = editorFixture();
    for (auto &entry : input.at("nodes")) if (entry.at("id") == "a")
        entry["style"] = Json{{"custom", {{"integer", UINT64_C(9007199254740993)}, {"nested", Json::array({true, "opaque"})}}}};
    CHECK(editor.loadJson(encoded(input)));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor, QSize(1100, 900));
    assertFit(editor);
    auto &view = graphics(editor);
    auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
    CHECK(panel != nullptr && panel->isVisible() && !view.isAncestorOf(panel));
    auto *scroll = panel->findChild<QScrollArea *>();
    auto *tags = panel->findChild<QLineEdit *>(QStringLiteral("nodeTags"));
    auto *icons = panel->findChild<QLineEdit *>(QStringLiteral("nodeIcons"));
    auto *url = panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
    auto *note = panel->findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
    auto *size = panel->findChild<QComboBox *>(QStringLiteral("nodeFontSize"));
    CHECK(scroll && tags && icons && url && note && size);
    auto button = [&](const char *name) {
        auto *result = panel->findChild<QAbstractButton *>(QString::fromLatin1(name));
        CHECK(result != nullptr);
        return result;
    };
    auto reveal = [&](QWidget *widget) {
        if (scroll->widget()->isAncestorOf(widget)) scroll->ensureWidgetVisible(widget);
        pump();
    };
    auto click = [&](const char *name) {
        auto *control = button(name);
        reveal(control);
        QTest::mouseClick(control, Qt::LeftButton);
        pump();
    };
    auto paste = [&](QWidget *widget, const QString &text) {
        reveal(widget);
        widget->setFocus(Qt::OtherFocusReason);
        QTest::keyClick(widget, Qt::Key_A, Qt::ControlModifier);
        QApplication::clipboard()->setText(text);
        QTest::keyClick(widget, Qt::Key_V, Qt::ControlModifier);
        pump();
    };
    auto jsonNode = [](Json &doc, const char *id) -> Json & {
        for (auto &entry : doc.at("nodes")) if (entry.at("id") == id) return entry;
        throw std::runtime_error("Missing property fixture node");
    };
    auto anchored = [&] {
        const QRect canvas(view.viewport()->mapTo(&editor, QPoint()), view.viewport()->size());
        CHECK(canvas.contains(panel->geometry()));
        CHECK(panel->geometry().right() > canvas.center().x());
        CHECK(panel->geometry().top() < canvas.top() + 24);
    };
    Json expected = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
    const QTransform zoom = view.transform();
    anchored();
    editor.clearSelection();
    CHECK(!panel->isVisible());
    CHECK(editor.selectLink(QStringLiteral("l1")) && !panel->isVisible());
    CHECK(editor.selectNode(QStringLiteral("a")) && panel->isVisible());
    CHECK(exported(editor) == expected && changed.isEmpty());
    CHECK(view.transform() == zoom);

    auto badgeCount = [&](const QString &value) {
        auto *owner = ownerItem(textItem(editor, QStringLiteral("Alpha")));
        const auto matches = texts(editor, value);
        return std::count_if(matches.begin(), matches.end(), [&](auto *badge) { return ownerItem(badge) == owner; });
    };
    // Each edit changes only its native field; arbitrary style/image/link data survive.
    paste(tags, QString::fromUtf8("todo, 世界, todo"));
    jsonNode(expected, "a")["tags"] = Json::array({"todo", "世界", "todo"});
    CHECK(exported(editor) == expected && changed.size() == 1);
    CHECK(badgeCount(QStringLiteral("todo")) == 2 && badgeCount(QString::fromUtf8("世界")) == 1);
    QTest::keyClicks(tags, ", ");
    CHECK(tags->text().endsWith(QStringLiteral(", ")) && tags->cursorPosition() == tags->text().size());
    CHECK(exported(editor) == expected && changed.size() == 1);
    tags->setCursorPosition(0);
    QTest::keyClicks(tags, "x");
    jsonNode(expected, "a")["tags"][0] = "xtodo";
    CHECK(tags->cursorPosition() == 1 && tags->text().endsWith(QStringLiteral(", ")));
    CHECK(exported(editor) == expected && changed.size() == 2);
    CHECK(badgeCount(QStringLiteral("xtodo")) == 1 && badgeCount(QStringLiteral("todo")) == 1);
    CHECK(badgeCount(QString::fromUtf8("世界")) == 1);
    paste(icons, QStringLiteral("star, flag, star"));
    jsonNode(expected, "a")["icons"] = Json::array({"star", "flag", "star"});
    paste(url, QString::fromUtf8("opaque:世界?q=1"));
    jsonNode(expected, "a")["hyperLink"] = "opaque:世界?q=1";
    paste(note, QString::fromUtf8("Memo café\nSecond line 世界"));
    jsonNode(expected, "a")["note"] = "Memo café\nSecond line 世界";
    CHECK(exported(editor) == expected && changed.size() == 5 && errors.isEmpty());
    // Assistive technology sets values without emitting keyboard-only edit signals.
    auto *accessibleUrl = QAccessible::queryAccessibleInterface(url);
    CHECK(accessibleUrl != nullptr);
    accessibleUrl->setText(QAccessible::Value, QStringLiteral("opaque:accessible"));
    jsonNode(expected, "a")["hyperLink"] = "opaque:accessible";
    CHECK(exported(editor) == expected && changed.size() == 6);

    // Panel focus is outside the map shortcut scope; typing cannot create/delete/reorder nodes.
    reveal(icons);
    icons->setFocus();
    QTest::keyClick(icons, Qt::Key_Home);
    CHECK(icons->cursorPosition() == 0);
    QTest::keyClick(icons, Qt::Key_Right);
    QTest::keyClick(icons, Qt::Key_Delete);
    QTest::keyClick(icons, Qt::Key_Space);
    QTest::keyClick(icons, Qt::Key_Return);
    QTest::keyClick(icons, Qt::Key_Tab);
    CHECK(!icons->hasFocus() && editor.selectedNodeId() == QStringLiteral("a") && panel->isVisible());
    CHECK(exported(editor).at("nodes").size() == expected.at("nodes").size());
    for (const auto &entry : expected.at("nodes"))
        CHECK(record(exported(editor), "nodes", qs(entry.at("id"))).at("children") == entry.at("children"));
    reveal(note);
    note->setFocus();
    QTest::keyClick(note, Qt::Key_End, Qt::ControlModifier);
    QTest::keyClick(note, Qt::Key_Return);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("note") == "Memo café\nSecond line 世界\n");
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    expected = exported(editor);

    // Typography participates in measurement; colors paint without disturbing selection or zoom.
    const QRectF normalRect = topicRect(editor, QStringLiteral("Alpha"));
    reveal(size);
    size->setFocus();
    const int large = size->findData(24);
    CHECK(large >= 0);
    for (int step = 0; size->currentIndex() != large && step < size->count(); ++step)
        QTest::keyClick(size, size->currentIndex() < large ? Qt::Key_Down : Qt::Key_Up);
    CHECK(size->currentIndex() == large);
    CHECK(textItem(editor, QStringLiteral("Alpha"))->font().pixelSize() == 24);
    CHECK(topicRect(editor, QStringLiteral("Alpha")).height() > normalRect.height());
    auto *accessibleBold = QAccessible::queryAccessibleInterface(button("nodeBold"));
    CHECK(accessibleBold && accessibleBold->actionInterface());
    accessibleBold->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    CHECK(textItem(editor, QStringLiteral("Alpha"))->font().bold());
    if (button("nodeItalic")->isChecked()) click("nodeItalic");
    click("nodeItalic");
    CHECK(textItem(editor, QStringLiteral("Alpha"))->font().italic());
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("style").at("fontStyle") == "italic");
    click("nodeTextColor");
    click("nodeColor_2980b9");
    CHECK(textItem(editor, QStringLiteral("Alpha"))->defaultTextColor() == QColor(QStringLiteral("#2980b9")));
    const QRectF paintedRect = topicRect(editor, QStringLiteral("Alpha"));
    const QImage beforeFill = paintScene(editor, paintedRect);
    click("nodeFillColor");
    click("nodeColor_e74c3c");
    CHECK(paintScene(editor, paintedRect) != beforeFill);
    const auto styled = exported(editor);
    CHECK(record(styled, "nodes", QStringLiteral("a")).at("style").at("background") == "#e74c3c");
    CHECK(record(styled, "nodes", QStringLiteral("a")).at("style").at("custom") ==
          record(expected, "nodes", QStringLiteral("a")).at("style").at("custom"));
    Editor roundtrip;
    CHECK(roundtrip.loadJson(editor.toJson()) && exported(roundtrip) == styled);
    CHECK(textItem(roundtrip, QStringLiteral("Alpha"))->font().italic());
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && view.transform() == zoom);
    const auto beforeReset = changed.size();
    auto *reset = button("nodeResetAppearance");
    reveal(reset);
    reset->setFocus(Qt::OtherFocusReason);
    QTest::keyClick(reset, Qt::Key_Space);
    pump();
    CHECK(exported(editor) == expected && changed.size() == beforeReset + 1);
    CHECK(topicRect(editor, QStringLiteral("Alpha")).size() == normalRect.size());
    CHECK(textItem(editor, QStringLiteral("Alpha"))->font().italic() == editor.font().italic());
    click("nodeResetAppearance");
    CHECK(changed.size() == beforeReset + 1);
    CHECK(editor.selectNode(QStringLiteral("r")));
    const QString rootTopic = qs(record(expected, "nodes", QStringLiteral("r")).at("topic"));
    CHECK(textItem(editor, rootTopic)->font().bold());
    click("nodeBold");
    CHECK(!textItem(editor, rootTopic)->font().bold());
    click("nodeResetAppearance");
    CHECK(textItem(editor, rootTopic)->font().bold() && exported(editor) == expected);

    // Unset style inherits the editor font; explicit normal and reset must remain distinct.
    const QFont originalFont = editor.font();
    QFont inheritedItalic = originalFont;
    inheritedItalic.setItalic(true);
    const auto beforeFontChange = changed.size();
    editor.setFont(inheritedItalic);
    pump();
    CHECK(textItem(editor, rootTopic)->font().italic() && button("nodeItalic")->isChecked());
    CHECK(exported(editor) == expected && changed.size() == beforeFontChange);
    click("nodeItalic");
    CHECK(!textItem(editor, rootTopic)->font().italic());
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("style").at("fontStyle") == "normal");
    click("nodeResetAppearance");
    CHECK(textItem(editor, rootTopic)->font().italic() && button("nodeItalic")->isChecked());
    CHECK(exported(editor) == expected && changed.size() == beforeFontChange + 2);
    editor.setFont(originalFont);
    pump();
    CHECK(textItem(editor, rootTopic)->font().italic() == originalFont.italic());
    CHECK(button("nodeItalic")->isChecked() == originalFont.italic());
    CHECK(exported(editor) == expected && changed.size() == beforeFontChange + 2);

    // Same-ID replacement must refresh focused inputs even without selectionChanged.
    reveal(note);
    note->setFocus();
    Json replacement = expected;
    jsonNode(replacement, "r")["note"] = "External replacement";
    jsonNode(replacement, "r")["style"]["fontSize"] = "18px";
    jsonNode(replacement, "r")["style"]["fontWeight"] = 700;
    CHECK(editor.loadJson(encoded(replacement)));
    CHECK(note->toPlainText() == QStringLiteral("External replacement"));
    CHECK(textItem(editor, rootTopic)->font().pixelSize() == 18 && textItem(editor, rootTopic)->font().bold());
    CHECK(exported(editor) == nativeDocument(replacement));
    changed.clear();
    const Json beforeResize = exported(editor);
    editor.resize(760, 440);
    pump();
    anchored();
    CHECK(scroll->verticalScrollBar()->maximum() > 0);
    reveal(note);
    CHECK(note->isVisible() && panel->isVisible());
    CHECK(exported(editor) == beforeResize && changed.isEmpty());
    auto *toggle = button("nodePropertiesToggle");
    auto compact = [&] {
        pump();
        anchored();
        CHECK(panel->isVisible() && toggle->isVisible() && scroll->isHidden());
        CHECK(panel->width() <= toggle->width() + 8 && panel->height() <= toggle->height() + 8);
        for (auto *control : panel->findChildren<QAbstractButton *>())
            CHECK(control == toggle || !control->isVisible());
        for (auto *label : panel->findChildren<QLabel *>()) CHECK(!label->isVisible());
    };
    QSignalSpy selected(&editor, &Editor::selectionChanged);
    const QTransform beforeToggleZoom = view.transform();
    const QPointF beforeToggleCenter = view.mapToScene(view.viewport()->rect().center());
    click("nodePropertiesToggle");
    compact();
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && selected.isEmpty());
    CHECK(exported(editor) == beforeResize && changed.isEmpty());
    CHECK(view.transform() == beforeToggleZoom && view.mapToScene(view.viewport()->rect().center()) == beforeToggleCenter);
    editor.resize(1100, 900);
    compact();
    clickLabel(editor, QStringLiteral("Delta"));
    CHECK(editor.selectedNodeId() == QStringLiteral("d"));
    compact();
    clickLabel(editor, QStringLiteral("Alpha"));
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    compact();
    editor.clearSelection();
    CHECK(!panel->isVisible());
    CHECK(editor.selectLink(QStringLiteral("l1")) && !panel->isVisible());
    CHECK(editor.selectNode(QStringLiteral("d")));
    compact();
    CHECK(exported(editor) == beforeResize && changed.isEmpty());
    CHECK(editor.newDocument(QStringLiteral("Temporary document")));
    compact();
    CHECK(editor.loadJson(encoded(beforeResize)));
    compact();
    CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    changed.clear(); selected.clear();
    const QTransform expandZoom = view.transform();
    const QPointF expandCenter = view.mapToScene(view.viewport()->rect().center());
    toggle->setFocus();
    QTest::keyClick(toggle, Qt::Key_Space);
    pump();
    CHECK(panel->isVisible() && scroll->isVisible() && note->isVisible());
    CHECK(note->toPlainText() == QStringLiteral("External replacement"));
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && selected.isEmpty());
    CHECK(exported(editor) == beforeResize && changed.isEmpty());
    CHECK(view.transform() == expandZoom && view.mapToScene(view.viewport()->rect().center()) == expandCenter);
    // Escape collapses only the panel, returns focus to the selected node, and never expands it.
    editor.activateWindow();
    pump();
    for (QWidget *control : {static_cast<QWidget *>(tags), static_cast<QWidget *>(note), static_cast<QWidget *>(toggle)}) {
        reveal(control);
        control->setFocus();
        QTest::keyClick(control, Qt::Key_Escape);
        compact();
        CHECK(view.hasFocus());
        toggle->setFocus();
        QTest::keyClick(toggle, Qt::Key_Escape);
        compact();
        CHECK(view.hasFocus());
        CHECK(editor.selectedNodeId() == QStringLiteral("r") && selected.isEmpty());
        CHECK(exported(editor) == beforeResize && changed.isEmpty());
        CHECK(view.transform() == expandZoom && view.mapToScene(view.viewport()->rect().center()) == expandCenter);
        toggle->setFocus();
        QTest::keyClick(toggle, Qt::Key_Space);
        pump();
        CHECK(scroll->isVisible() && note->isVisible());
    }
    // A dropdown or emoji picker owns the first Escape; the panel owns the next.
    reveal(size);
    size->setFocus();
    QTest::keyClick(size, Qt::Key_Down, Qt::AltModifier);
    pump();
    CHECK(size->view()->isVisible());
    QTest::keyClick(size->view(), Qt::Key_Escape);
    pump();
    CHECK(!size->view()->isVisible() && scroll->isVisible());
    QTest::keyClick(size, Qt::Key_Escape);
    compact();
    CHECK(view.hasFocus());
    toggle->setFocus();
    QTest::keyClick(toggle, Qt::Key_Space);
    reveal(icons);
    icons->setFocus();
    pump();
    auto *picker = icons->findChild<QWidget *>(QStringLiteral("emojiPopup"));
    CHECK(picker && picker->isVisible());
    QTest::keyClick(icons, Qt::Key_Escape);
    CHECK(!picker->isVisible() && scroll->isVisible() && icons->hasFocus());
    QTest::keyClick(icons, Qt::Key_Escape);
    compact();
    CHECK(view.hasFocus());
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && selected.isEmpty());
    CHECK(exported(editor) == beforeResize && changed.isEmpty());
    // The next node shortcut works without moving focus back to the canvas.
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_F2);
    pump();
    CHECK(topicInput(editor).toPlainText() == rootTopic);
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && selected.isEmpty());
    topicKey(editor, Qt::Key_Escape);
    CHECK(activeTopicInput(editor) == nullptr && view.hasFocus());
    compact();
    CHECK(exported(editor) == beforeResize && changed.isEmpty());
    toggle->setFocus();
    QTest::keyClick(toggle, Qt::Key_Space);
    pump();
    // Canvas Escape retains its existing selection-clearing behavior.
    shortcut(editor, Qt::Key_Escape);
    CHECK(editor.selectedNodeId().isEmpty() && !panel->isVisible());
    CHECK(editor.selectNode(QStringLiteral("r")) && scroll->isVisible());
    CHECK(editor.selectNode(QStringLiteral("d")) && panel->isVisible());
    CHECK(note->toPlainText().isEmpty());
    CHECK(editor.setExpanded(QStringLiteral("b"), false));
    CHECK(editor.moveNode(QStringLiteral("d"), QStringLiteral("b")));
    CHECK(editor.selectedNodeId().isEmpty() && !panel->isVisible());
    CHECK(editor.selectNode(QStringLiteral("a")));
    CHECK(editor.removeNode(QStringLiteral("a")));
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && panel->isVisible());
    CHECK(note->toPlainText() == QStringLiteral("External replacement"));

    // Clicking a property field commits the old inline draft once before applying its own edit.
    CHECK(editor.loadJson(encoded(editorFixture())));
    assertFit(editor);
    CHECK(editor.selectNode(QStringLiteral("a")));
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QStringLiteral("Renamed from inline editor #inline"));
    changed.clear();
    reveal(tags);
    QTest::mouseClick(tags, Qt::LeftButton);
    pump();
    CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Renamed from inline editor");
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("tags") == Json::array({"inline"}));
    CHECK(tags->text() == QStringLiteral("inline"));
    QTest::keyClick(tags, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(tags, "new");
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Renamed from inline editor");
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("tags") == Json::array({"new"}));
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && errors.isEmpty());

    // Rebuilds must preserve one fixed camera anchor across all edits, even at half-pixel midpoints.
    for (int parity : {0, 1}) for (bool zoomed : {false, true}) {
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor, QSize(1100, 900));
        if (scroll->isHidden()) click("nodePropertiesToggle");
        editor.resize(editor.width() + (view.viewport()->width() % 2 != parity),
                      editor.height() + (view.viewport()->height() % 2 != parity));
        pump();
        CHECK(view.viewport()->width() % 2 == parity && view.viewport()->height() % 2 == parity);
        CHECK(editor.selectNode(QStringLiteral("a")));
        trigger(editor, "resetZoom");
        if (zoomed) trigger(editor, "zoomIn");
        reveal(note);
        note->setFocus(Qt::OtherFocusReason);
        pump();
        CHECK(note->hasFocus() && note->toPlainText().isEmpty());
        CHECK(std::abs(view.transform().m11() - (zoomed ? 1.2 : 1.0)) < 1e-6);
        changed.clear();

        const QTransform initialTransform = view.transform();
        const QPointF anchor = view.mapToScene(QPoint(0, 0));
        const QPointF initialPosition = view.viewportTransform().map(anchor);
        const QRectF initialRect = topicRect(editor, QStringLiteral("Alpha"));
        auto cameraUnchanged = [&] {
            const QPointF displacement = view.viewportTransform().map(anchor) - initialPosition;
            CHECK(std::abs(displacement.x()) <= 1e-6);
            CHECK(std::abs(displacement.y()) <= 1e-6);
            CHECK(view.transform() == initialTransform);
            CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.selectedLinkId().isEmpty());
        };
        for (int i = 0; i < 50; ++i) {
            QTest::keyClick(note, Qt::Key_A);
            pump();
            cameraUnchanged();
            CHECK(topicRect(editor, QStringLiteral("Alpha")) == initialRect);
            CHECK(changed.size() == i + 1);
        }
        const QString expectedNote(50, QLatin1Char('a'));
        CHECK(note->toPlainText() == expectedNote);
        CHECK(qs(record(exported(editor), "nodes", QStringLiteral("a")).at("note")) == expectedNote);

        click("nodeTextColor");
        cameraUnchanged();
        CHECK(topicRect(editor, QStringLiteral("Alpha")) == initialRect);
        for (int i = 0; i < 20; ++i) {
            click(i % 2 == 0 ? "nodeColor_2980b9" : "nodeColor_e74c3c");
            cameraUnchanged();
            CHECK(topicRect(editor, QStringLiteral("Alpha")) == initialRect);
            const QString color = i % 2 == 0 ? QStringLiteral("#2980b9") : QStringLiteral("#e74c3c");
            CHECK(textItem(editor, QStringLiteral("Alpha"))->defaultTextColor() == QColor(color));
            CHECK(qs(record(exported(editor), "nodes", QStringLiteral("a")).at("style").at("color")) == color);
        }
        for (int i = 0; i < 10; ++i) {
            click("nodeBold");
            cameraUnchanged();
            // Typography may reflow layout, but it must not translate the camera.
            CHECK(textItem(editor, QStringLiteral("Alpha"))->font().bold() == button("nodeBold")->isChecked());
        }
        CHECK(errors.isEmpty());
    }
    // Auto removes only the active color override, never typography or opaque data.
    {
        Editor colorsEditor;
        Json colorsInput = editorFixture();
        jsonNode(colorsInput, "a")["style"] = Json{
            {"fontSize", 24}, {"fontWeight", "bold"}, {"fontStyle", "italic"},
            {"color", "#2980b9"}, {"background", "#e74c3c"},
            {"custom", {{"integer", UINT64_C(9007199254740993)}, {"nested", Json::array({true, "opaque"})}}}};
        jsonNode(colorsInput, "b")["style"]["color"] = "#d7bde2";
        CHECK(colorsEditor.loadJson(encoded(colorsInput)));
        showEditor(colorsEditor, QSize(1100, 900));
        CHECK(colorsEditor.selectNode(QStringLiteral("a")));
        auto *colorsPanel = colorsEditor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        CHECK(colorsPanel != nullptr);
        auto *colorsScroll = colorsPanel->findChild<QScrollArea *>();
        CHECK(colorsScroll != nullptr);
        auto colorButton = [&](const char *name) {
            auto *control = colorsPanel->findChild<QAbstractButton *>(QString::fromLatin1(name));
            CHECK(control != nullptr);
            colorsScroll->ensureWidgetVisible(control);
            pump();
            return control;
        };
        auto colorClick = [&](const char *name) {
            QTest::mouseClick(colorButton(name), Qt::LeftButton);
            pump();
        };
        auto autoSpace = [&] {
            auto *control = colorButton("nodeDefaultColor");
            control->setFocus(Qt::OtherFocusReason);
            QTest::keyClick(control, Qt::Key_Space);
            pump();
        };
        auto *automatic = colorButton("nodeDefaultColor");
        Json colorsExpected = exported(colorsEditor);
        QSignalSpy colorChanges(&colorsEditor, &Editor::documentChanged);
        colorClick("nodeTextColor");
        CHECK(!automatic->isChecked());
        colorClick("nodeDefaultColor");
        jsonNode(colorsExpected, "a")["style"].erase("color");
        CHECK(exported(colorsEditor) == colorsExpected && colorChanges.size() == 1);
        CHECK(automatic->isChecked());
        autoSpace();
        CHECK(exported(colorsEditor) == colorsExpected && colorChanges.size() == 1);
        CHECK(automatic->isChecked());

        colorClick("nodeFillColor");
        CHECK(!automatic->isChecked());
        autoSpace();
        jsonNode(colorsExpected, "a")["style"].erase("background");
        CHECK(exported(colorsEditor) == colorsExpected && colorChanges.size() == 2);
        CHECK(automatic->isChecked());
        colorClick("nodeTextColor");
        CHECK(automatic->isChecked());

        colorClick("nodeColor_ffffff");
        jsonNode(colorsExpected, "a")["style"]["color"] = "#ffffff";
        CHECK(record(exported(colorsEditor), "nodes", QStringLiteral("a")).at("style").at("color") == "#ffffff");
        CHECK(exported(colorsEditor) == colorsExpected && colorChanges.size() == 3);
        CHECK(!automatic->isChecked());
        autoSpace();
        jsonNode(colorsExpected, "a")["style"].erase("color");
        CHECK(exported(colorsEditor) == colorsExpected && colorChanges.size() == 4);
        CHECK(automatic->isChecked());

        CHECK(colorsEditor.selectNode(QStringLiteral("b")));
        CHECK(!automatic->isChecked());
        CHECK(exported(colorsEditor) == colorsExpected && colorChanges.size() == 4);
        CHECK(colorsEditor.selectNode(QStringLiteral("a")));
        CHECK(automatic->isChecked());
        CHECK(exported(colorsEditor) == colorsExpected && colorChanges.size() == 4);
    }
    // Clearing the last badges restores the untagged shape without moving the camera.
    {
        Editor tagged, untagged;
        Json emptyTags = editorFixture();
        jsonNode(emptyTags, "r")["tags"] = Json::array();
        CHECK(tagged.loadJson(encoded(editorFixture())) && untagged.loadJson(encoded(emptyTags)));
        untagged.setFont(tagged.font());
        CHECK(tagged.setLayoutDirection(Editor::LayoutDirection::Right));
        CHECK(untagged.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(untagged, QSize(1100, 900));
        showEditor(tagged, QSize(1100, 900));
        CHECK(tagged.selectNode(QStringLiteral("r")));
        auto *field = tagged.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        auto *tagPanel = tagged.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        CHECK(field && tagPanel);
        auto *tagScroll = tagPanel->findChild<QScrollArea *>();
        CHECK(tagScroll);
        tagScroll->ensureWidgetVisible(field);
        field->setFocus();
        pump();
        CHECK(field->isVisible() && field->hasFocus());
        Json cleared = exported(tagged);
        const QString topic = qs(jsonNode(cleared, "r").at("topic"));
        const QSizeF taggedSize = topicRect(tagged, topic).size();
        auto &canvas = graphics(tagged);
        const QTransform transform = canvas.transform();
        const QPointF center = canvas.mapToScene(canvas.viewport()->rect().center());
        QSignalSpy edits(&tagged, &Editor::documentChanged), selections(&tagged, &Editor::selectionChanged);
        QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(field, Qt::Key_Delete);
        pump();
        jsonNode(cleared, "r")["tags"] = Json::array();
        CHECK(exported(tagged) == cleared && edits.size() == 1);
        CHECK(tagged.selectedNodeId() == QStringLiteral("r") && tagged.selectedLinkId().isEmpty() && selections.isEmpty());
        CHECK(canvas.transform() == transform && canvas.mapToScene(canvas.viewport()->rect().center()) == center);
        auto *topicLabel = textItem(tagged, topic);
        auto *owner = ownerItem(topicLabel);
        for (auto *item : owner->childItems()) if (auto *text = dynamic_cast<QGraphicsTextItem *>(item))
            CHECK(text == topicLabel || text->sceneBoundingRect().bottom() <= topicLabel->sceneBoundingRect().top());
        CHECK(topicRect(tagged, topic).size() == topicRect(untagged, topic).size());
        CHECK(topicRect(tagged, topic).height() < taggedSize.height());
    }
}

static void focus_root_case() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor);
    trigger(editor, "resetZoom");
    trigger(editor, "zoomIn");
    auto &view = graphics(editor);
    const QTransform zoom = view.transform();
    const Json original = exported(editor);
    const QString rootTopic = qs(record(original, "nodes", QStringLiteral("r")).at("topic"));
    QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
    QToolButton *focusButton = nullptr;
    for (auto *button : editor.findChildren<QToolButton *>())
        if (button->defaultAction() == &editAction(editor, "selectRoot")) focusButton = button;
    CHECK(focusButton != nullptr && focusButton->isVisible());
    auto rootCentered = [&] {
        return QLineF(view.mapFromScene(topicRect(editor, rootTopic).center()),
                      view.viewport()->rect().center()).length() <= 2.0;
    };
    auto checkFocused = [&] {
        CHECK(editor.selectedNodeId() == QStringLiteral("r") && editor.selectedLinkId().isEmpty());
        CHECK(rootCentered() && view.transform() == zoom && view.hasFocus());
    };

    editor.clearSelection();
    editor.findChild<QComboBox *>()->setFocus();
    CHECK(!view.hasFocus());
    QTest::mouseClick(focusButton, Qt::LeftButton);
    pump();
    checkFocused();
    CHECK(editor.selectLink(QStringLiteral("l1")));
    QTest::mouseClick(focusButton, Qt::LeftButton);
    pump();
    checkFocused();

    const auto selectionCount = selected.size();
    const QPoint center = view.viewport()->rect().center();
    dragMiddle(view, center, center + QPoint(160, 90));
    CHECK(!rootCentered());
    shortcut(editor, Qt::Key_Home);
    checkFocused();
    CHECK(selected.size() == selectionCount && changed.isEmpty() && exported(editor) == original);

    // The public command must commit before selecting/recentering the root.
    CHECK(editor.selectNode(QStringLiteral("a")));
    shortcut(editor, Qt::Key_F2);
    const QString draft = QStringLiteral("Draft preserved by focusRoot\nSecond line");
    topicInput(editor).setPlainText(draft);
    CHECK(editor.focusRoot());
    pump();
    checkFocused();
    CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
    Json expected = original;
    setTopic(expected, "a", draft);
    CHECK(exported(editor) == expected);
}

static void navigation_case() {
    focus_root_case();
    empty_space_panning_case();
    Json input = editorFixture();
    for (int i = 0; i < 10; ++i) {
        const std::string id = "scroll-" + std::to_string(i);
        input["nodes"].push_back({{"id", id}, {"topic", "Scroll branch " + std::to_string(i)}});
        input["nodes"][0]["children"].push_back(id);
    }
    Editor editor;
    QPalette palette = editor.palette();
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Window, Qt::white);
    editor.setPalette(palette);
    CHECK(editor.loadJson(encoded(input)));
    showEditor(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QSignalSpy selected(&editor, &Editor::selectionChanged);
    const Json original = exported(editor);
    auto &view = graphics(editor);
    clickLabel(editor, QStringLiteral("Alpha"));
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.selectedLinkId().isEmpty());
    clickLabel(editor, QStringLiteral("Related"));
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId() == QStringLiteral("l1"));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, blankPoint(view));
    pump();
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
    CHECK(selected.size() == 3 && changed.isEmpty());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, blankPoint(view));
    CHECK(selected.size() == 3);

    auto &reset = textAction(editor, {QStringLiteral("100%"), QStringLiteral("Reset zoom")});
    auto &zoomIn = textAction(editor, {QStringLiteral("Zoom +"), QStringLiteral("Zoom in"), QStringLiteral("+")});
    auto &zoomOut = textAction(editor, {QStringLiteral("Zoom -"), QStringLiteral("Zoom −"), QStringLiteral("Zoom out"), QStringLiteral("-")});
    auto &fit = textAction(editor, {QStringLiteral("Fit"), QStringLiteral("Fit to contents")});
    reset.trigger();
    pump();
    CHECK(std::abs(view.transform().m11() - 1.0) < 1e-6);

    // Repeated 100% resets are idempotent, including the even-size midpoint boundary.
    editor.resize(editor.width() + view.viewport()->width() % 2,
                  editor.height() + view.viewport()->height() % 2);
    pump();
    CHECK(view.viewport()->width() % 2 == 0 && view.viewport()->height() % 2 == 0);
    const QTransform resetTransform = view.transform();
    const QPointF resetAnchor = view.mapToScene(QPoint(0, 0));
    const QPointF resetPosition = view.viewportTransform().map(resetAnchor);
    for (int i = 0; i < 50; ++i) {
        trigger(editor, "resetZoom");
        const QPointF displacement = view.viewportTransform().map(resetAnchor) - resetPosition;
        CHECK(std::abs(displacement.x()) <= 1e-6);
        CHECK(std::abs(displacement.y()) <= 1e-6);
        CHECK(view.transform() == resetTransform);
    }

    zoomIn.trigger();
    CHECK(std::abs(view.transform().m11() - 1.2) < 1e-6);
    zoomOut.trigger();
    CHECK(std::abs(view.transform().m11() - 1.0) < 1e-6);
    for (int i = 0; i < 4; ++i) zoomIn.trigger();
    pump();
    const QPoint cursor(view.viewport()->width() * 3 / 5, view.viewport()->height() * 2 / 5);
    const QPointF anchor = view.mapToScene(cursor);
    const qreal scale = view.transform().m11();
    wheel(view, cursor, 120, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - scale * 1.2) < 1e-6);
    CHECK(QLineF(anchor, view.mapToScene(cursor)).length() < 3.0);
    wheel(view, cursor, -120, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - scale) < 1e-6);
    wheel(view, cursor, 120 * 50, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 4.0) < 1e-6);
    const QPointF beforePan = view.mapToScene(view.viewport()->rect().center());
    const QPoint center = view.viewport()->rect().center();
    dragMiddle(view, center, center + QPoint(65, 42));
    CHECK(QLineF(beforePan, view.mapToScene(center)).length() > 5);
    CHECK(std::abs(view.transform().m11() - 4.0) < 1e-6);
    CHECK(view.verticalScrollBar()->maximum() > view.verticalScrollBar()->minimum());
    view.verticalScrollBar()->setValue((view.verticalScrollBar()->minimum() + view.verticalScrollBar()->maximum()) / 2);
    const int beforeScroll = view.verticalScrollBar()->value();
    wheel(view, center, -120, Qt::NoModifier);
    CHECK(view.verticalScrollBar()->value() != beforeScroll);
    CHECK(std::abs(view.transform().m11() - 4.0) < 1e-6);
    wheel(view, center, -120 * 50, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 0.1) < 1e-6);
    reset.trigger();
    pump();
    std::vector<QRectF> nodes;
    for (const auto &entry : original.at("nodes")) {
        const QString topic = qs(entry.at("topic"));
        nodes.push_back(topic.isEmpty() ? emptyNodeRect(editor) : topicRect(editor, topic));
    }
    editor.clearSelection();
    const QPoint wideHit = curvePoint(editor, QStringLiteral("Other"), nodes, true);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, wideHit);
    pump();
    CHECK(editor.selectedLinkId() == QStringLiteral("l2"));
    CHECK(exported(editor) == original && changed.isEmpty());

    zoomIn.trigger();
    zoomIn.trigger();
    pump();
    const qreal editScale = view.transform().m11();
    const QPointF editCenter = view.mapToScene(view.viewport()->rect().center());
    CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Alpha renamed without fitting")));
    CHECK(changed.size() == 1);
    CHECK(std::abs(view.transform().m11() - editScale) < 1e-6);
    CHECK(QLineF(editCenter, view.mapToScene(view.viewport()->rect().center())).length() < 3.0);
    editor.resize(1120, 760);
    pump();
    CHECK(std::abs(view.transform().m11() - editScale) < 1e-6);
    CHECK(QLineF(editCenter, view.mapToScene(view.viewport()->rect().center())).length() < 4.0);
    fit.trigger();
    pump();
    const QRectF viewport(view.viewport()->rect());
    CHECK(viewport.adjusted(-3, -3, 3, 3).contains(view.mapFromScene(renderedBounds(editor)).boundingRect()));
    CHECK(changed.size() == 1);

    Json large{{"schemaVersion", 1}, {"rootId", "large"},
               {"nodes", Json::array({{{"id", "large"}, {"topic", "Large root"}, {"children", Json::array()}}})},
               {"crossLinks", Json::array()}};
    for (int i = 0; i < 180; ++i) {
        const std::string id = "large-" + std::to_string(i);
        large["nodes"][0]["children"].push_back(id);
        large["nodes"].push_back({{"id", id}, {"topic", "Tall map branch " + std::to_string(i)}});
    }
    CHECK(editor.loadJson(encoded(large)));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    assertFit(editor);
    CHECK(view.transform().m11() < 0.1);
    const auto largeChanges = changed.size();
    wheel(view, view.viewport()->rect().center(), 120, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 0.1) < 1e-6);
    CHECK(changed.size() == largeChanges);

    // Reveal full node bounds without changing zoom or manufacturing selection changes.
    wheel(view, view.viewport()->rect().center(), 120 * 50, Qt::ControlModifier);
    const qreal selectionScale = view.transform().m11();
    const Json beforeVisibility = exported(editor);
    const auto selectionCount = selected.size();
    auto mappedNode = [&](const QString &topic) {
        return view.mapFromScene(topicRect(editor, topic)).boundingRect();
    };
    auto fullyVisible = [&](const QString &topic) {
        return view.viewport()->rect().adjusted(-1, -1, 1, 1).contains(mappedNode(topic));
    };
    auto partiallyHide = [&](const QRectF &bounds) {
        const QRect mapped = view.mapFromScene(bounds).boundingRect();
        CHECK(mapped.width() < view.viewport()->width() && mapped.height() < view.viewport()->height());
        const QPoint desired(view.viewport()->width() - mapped.width() / 2,
                             view.viewport()->height() - mapped.height() / 2);
        view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->value() + mapped.left() - desired.x());
        view.verticalScrollBar()->setValue(view.verticalScrollBar()->value() + mapped.top() - desired.y());
        pump();
        const QRect clipped = view.mapFromScene(bounds).boundingRect();
        CHECK(view.viewport()->rect().intersects(clipped));
        CHECK(!view.viewport()->rect().adjusted(-1, -1, 1, 1).contains(clipped));
    };
    const QString lastTopic = QStringLiteral("Tall map branch 179");
    partiallyHide(topicRect(editor, lastTopic));
    CHECK(editor.selectNode(QStringLiteral("large-179")));
    pump();
    CHECK(fullyVisible(lastTopic) && selected.size() == selectionCount + 1);
    partiallyHide(topicRect(editor, lastTopic));
    CHECK(editor.selectNode(QStringLiteral("large-179")));
    pump();
    CHECK(fullyVisible(lastTopic) && selected.size() == selectionCount + 1);

    const QString middleTopic = QStringLiteral("Tall map branch 90");
    partiallyHide(topicRect(editor, middleTopic));
    const QPoint clippedHit = mappedNode(middleTopic).intersected(view.viewport()->rect()).center();
    CHECK(belongsTo(view.itemAt(clippedHit), ownerItem(textItem(editor, middleTopic))));
    // Do not use clickLabel: its own ensureVisible would hide the missing product scroll.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, clippedHit);
    pump();
    CHECK(editor.selectedNodeId() == QStringLiteral("large-90") && fullyVisible(middleTopic));
    const QString nextTopic = QStringLiteral("Tall map branch 91");
    CHECK(!fullyVisible(nextTopic));
    shortcut(editor, Qt::Key_Down);
    CHECK(editor.selectedNodeId() == QStringLiteral("large-91") && fullyVisible(nextTopic));
    CHECK(selected.size() == selectionCount + 3 && exported(editor) == beforeVisibility && changed.size() == largeChanges);
    CHECK(std::abs(view.transform().m11() - selectionScale) < 1e-6);

    const QRectF oldBounds = topicRect(editor, nextTopic);
    shortcut(editor, Qt::Key_F2);
    partiallyHide(oldBounds);
    const QString grown = QStringLiteral("Committed branch with a wider topic\nSecond line\nThird line");
    topicInput(editor).setPlainText(grown);
    topicKey(editor, Qt::Key_Return);
    CHECK(record(exported(editor), "nodes", QStringLiteral("large-91")).at("topic") == utf8(grown));
    CHECK(fullyVisible(grown) && changed.size() == largeChanges + 1 && selected.size() == selectionCount + 3);
    CHECK(std::abs(view.transform().m11() - selectionScale) < 1e-6);

    const Json committed = exported(editor);
    const QRectF committedBounds = topicRect(editor, grown);
    shortcut(editor, Qt::Key_F2);
    partiallyHide(committedBounds);
    topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
    CHECK(fullyVisible(grown) && exported(editor) == committed && changed.size() == largeChanges + 1);
    CHECK(std::abs(view.transform().m11() - selectionScale) < 1e-6);
}

static void inline_tags_case() {
    Editor editor;
    showEditor(editor);
    const QString root = QStringLiteral("r");
    QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
    auto reset = [&] {
        CHECK(editor.loadJson(encoded(editorFixture())));
        CHECK(editor.selectNode(root));
        pump();
        changed.clear();
        errors.clear();
        return exported(editor);
    };
    {
        Json expected = reset();
        const Json original = expected;
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("  Roadmap  #todo   ##literal #x  done  "));
        CHECK(exported(editor) == original && changed.isEmpty());
        Json observedRoot;
        const auto observer = QObject::connect(&editor, &Editor::documentChanged, &editor, [&] {
            observedRoot = record(exported(editor), "nodes", root);
        });
        topicKey(editor, Qt::Key_Return);
        QObject::disconnect(observer);
        const QString topic = QStringLiteral("Roadmap #literal done");
        setTopic(expected, "r", topic);
        for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r")
            entry["tags"] = Json::array({"x", "y", "x", "todo", "x"});
        CHECK(exported(editor) == expected && changed.size() == 1 && errors.isEmpty());
        CHECK(observedRoot == record(expected, "nodes", root));
        auto *owner = ownerItem(textItem(editor, topic));
        auto badgeCount = [&](const QString &tag) {
            const auto matches = texts(editor, tag);
            return std::count_if(matches.begin(), matches.end(), [&](auto *badge) { return ownerItem(badge) == owner; });
        };
        CHECK(badgeCount(QStringLiteral("x")) == 3 && badgeCount(QStringLiteral("y")) == 1);
        CHECK(badgeCount(QStringLiteral("todo")) == 1);
        auto *tags = editor.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        CHECK(tags && tags->text() == QStringLiteral("x, y, x, todo, x"));
    }
    {
        // These vectors distinguish escape precedence, Unicode words, and line-preserving cleanup.
        struct ParseCase { QString draft, topic; Json appended; };
        const ParseCase cases[] = {
            {QStringLiteral("##tag ###next ####keep # #! #one##two"),
             QStringLiteral("#tag # ##keep # #! #two"), Json::array({"next", "one"})},
            {QStringLiteral("  ##tag  "), QStringLiteral("  #tag  "), Json::array()},
            {QString::fromUtf8(u8"#todo, (#世界) #release-1 #under_score #cafe\u0301 part#embedded #one#two #\U00010400"),
             QStringLiteral(", () part"),
             Json::array({"todo", u8"世界", "release-1", "under_score", u8"cafe\u0301", "embedded", "one", "two", u8"\U00010400"})},
            {QStringLiteral("  First  #one \n\tsecond #two  "), QStringLiteral("First\nsecond"), Json::array({"one", "two"})},
            {QStringLiteral("#one\n#two"), QStringLiteral("\n"), Json::array({"one", "two"})},
            {QStringLiteral(" #solo "), QString(), Json::array({"solo"})}
        };
        for (const auto &test : cases) {
            Json expected = reset();
            shortcut(editor, Qt::Key_F2);
            topicInput(editor).setPlainText(test.draft);
            topicKey(editor, Qt::Key_Return);
            setTopic(expected, "r", test.topic);
            for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r")
                for (const auto &tag : test.appended) entry["tags"].push_back(tag);
            CHECK(exported(editor) == expected && changed.size() == 1 && errors.isEmpty());
        }
    }
    {
        Json expected = reset();
        CHECK(editor.renameNode(root, QStringLiteral("Roadmap #literal")));
        setTopic(expected, "r", QStringLiteral("Roadmap #literal"));
        changed.clear();
        shortcut(editor, Qt::Key_F2);
        const QString baseline = QStringLiteral("Roadmap ##literal");
        CHECK(topicInput(editor).toPlainText() == baseline);
        topicKey(editor, Qt::Key_Return);
        CHECK(exported(editor) == expected && changed.isEmpty());

        shortcut(editor, Qt::Key_F2);
        topicInput(editor).moveCursor(QTextCursor::End);
        topicInput(editor).insertPlainText(QStringLiteral(" revised #next"));
        CHECK(editor.renameNode(QStringLiteral("b"), QStringLiteral("Unrelated mutation")));
        setTopic(expected, "b", QStringLiteral("Unrelated mutation"));
        pump();
        CHECK(topicInput(editor).toPlainText() == baseline + QStringLiteral(" revised #next"));
        CHECK(exported(editor) == expected && changed.size() == 1);
        topicKey(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(topicInput(editor).toPlainText() == baseline);
        changed.clear();
        topicKey(editor, Qt::Key_Return);
        CHECK(exported(editor) == expected && changed.isEmpty());

        shortcut(editor, Qt::Key_F2);
        topicInput(editor).moveCursor(QTextCursor::End);
        topicInput(editor).insertPlainText(QStringLiteral(" revised #next"));
        topicKey(editor, Qt::Key_Return);
        setTopic(expected, "r", QStringLiteral("Roadmap #literal revised"));
        for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r")
            entry["tags"].push_back("next");
        CHECK(exported(editor) == expected && changed.size() == 1 && errors.isEmpty());
    }
    {
        Json expected = reset();
        CHECK(editor.renameNode(root, QStringLiteral("#")));
        setTopic(expected, "r", QStringLiteral("#"));
        changed.clear();
        shortcut(editor, Qt::Key_F2);
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("##"));
        topicInput(editor).setPlainText(QStringLiteral("#"));
        topicKey(editor, Qt::Key_Return);
        CHECK(exported(editor) == expected && changed.isEmpty() && errors.isEmpty());
    }
    {
        Json expected = reset();
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Current #added"));
        auto *tags = editor.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        CHECK(tags != nullptr);
        tags->setText(QStringLiteral("fresh"));
        pump();
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("Current #added"));
        for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r")
            entry["tags"] = Json::array({"fresh"});
        CHECK(exported(editor) == expected && changed.size() == 1);
        changed.clear();
        topicKey(editor, Qt::Key_Return);
        setTopic(expected, "r", QStringLiteral("Current"));
        for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r")
            entry["tags"].push_back("added");
        CHECK(exported(editor) == expected && changed.size() == 1 && errors.isEmpty());
    }
    {
        const Json original = reset();
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Discard #drop ##keep"));
        topicKey(editor, Qt::Key_Escape);
        CHECK(activeTopicInput(editor) == nullptr);
        CHECK(exported(editor) == original && changed.isEmpty() && errors.isEmpty());
    }
    {
        Json expected = reset();
        const QString literal = QStringLiteral("Literal #tag ##pair");
        CHECK(editor.renameNode(root, literal));
        setTopic(expected, "r", literal);
        CHECK(exported(editor) == expected && changed.size() == 1 && errors.isEmpty());
        changed.clear();
        shortcut(editor, Qt::Key_F2);
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("Literal ##tag ####pair"));
        topicKey(editor, Qt::Key_Return);
        CHECK(exported(editor) == expected && changed.isEmpty());

        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Invalid") + QChar(QChar::Null) + QStringLiteral("#tag"));
        topicKey(editor, Qt::Key_Return);
        CHECK(exported(editor) == expected && changed.isEmpty());
        CHECK(errors.size() == 1 && editor.lastError() == QStringLiteral("Text cannot contain NUL characters"));
        CHECK(errors.front().front().toString() == editor.lastError());
    }
}

static void inline_edit_case() {
    inline_tags_case();
    {
        Editor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(QStringLiteral("a")));
        clickLabel(editor, QStringLiteral("y"));
        CHECK(editor.selectedNodeId() == QStringLiteral("r") && graphics(editor).hasFocus());
        Json expected = exported(editor);
        const Json tags = record(expected, "nodes", QStringLiteral("r")).at("tags");
        const QString topic = qs(record(expected, "nodes", QStringLiteral("r")).at("topic"));
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QTest::keyClick(graphics(editor).viewport(), Qt::Key_F2);
        pump();
        CHECK(topicInput(editor).toPlainText() == topic);
        const QString renamed = QStringLiteral("Renamed tagged root");
        topicInput(editor).setPlainText(renamed);
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        setTopic(expected, "r", renamed);
        CHECK(exported(editor) == expected && changed.size() == 1);
        CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("tags") == tags);
        CHECK(ownerItem(textItem(editor, QStringLiteral("y"))) == ownerItem(textItem(editor, renamed)));
    }
    {
        Editor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(editor);
        CHECK(editor.selectNode(QStringLiteral("a")));
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const Json original = exported(editor);
        shortcut(editor, Qt::Key_F2);
        const QString draft = QStringLiteral("A much longer parent topic that moves its child\nSecond line");
        topicInput(editor).setPlainText(draft);
        const QPoint target = labelPoint(editor, QStringLiteral("Delta"));
        CHECK(!topicInput(editor).geometry().contains(target));
        CHECK(exported(editor) == original && changed.isEmpty());
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, target);
        pump();
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == utf8(draft));
        CHECK(editor.selectedNodeId() == QStringLiteral("d") && changed.size() == 1);
        CHECK((labelPoint(editor, QStringLiteral("Delta")) - target).manhattanLength() > 10);
        const QRectF parent = topicRect(editor, draft);
        const QPoint circle = graphics(editor).mapFromScene(QPointF(parent.right() - 15, parent.center().y()));
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, circle);
        pump();
        CHECK(!record(exported(editor), "nodes", QStringLiteral("a")).at("expanded").get<bool>());
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 2);
        CHECK(editor.selectNode(QStringLiteral("a")));
        const Json unchanged = exported(editor);
        shortcut(editor, Qt::Key_F2);
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        CHECK(exported(editor) == unchanged && changed.size() == 2);
    }
    {
        Editor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(QStringLiteral("a")));
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const Json original = exported(editor);
        shortcut(editor, Qt::Key_F2);
        auto &input = topicInput(editor);
        QTest::keyClicks(&input, "ab");
        QTest::keyClick(&input, Qt::Key_Left);
        QTest::keyClick(&input, Qt::Key_Delete);
        QTest::keyClick(&input, Qt::Key_Space);
        QTest::keyClick(&input, Qt::Key_Return, Qt::ShiftModifier);
        QTest::keyClick(&input, Qt::Key_Tab);
        QTest::keyClicks(&input, "z");
        QTest::keyClick(&input, Qt::Key_Up);
        QTest::keyClick(&input, Qt::Key_Down);
        CHECK(input.toPlainText() == QStringLiteral("a \n\tz"));
        CHECK(exported(editor) == original && editor.selectedNodeId() == QStringLiteral("a") && changed.isEmpty());
        QToolButton *zoomButton = nullptr;
        for (auto *button : editor.findChildren<QToolButton *>())
            if (button->defaultAction() == &editAction(editor, "zoomIn")) zoomButton = button;
        CHECK(zoomButton != nullptr);
        zoomButton->setFocusPolicy(Qt::NoFocus);
        const qreal scale = graphics(editor).transform().m11();
        QTest::mouseClick(zoomButton, Qt::LeftButton);
        pump();
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "a \n\tz");
        CHECK(changed.size() == 1 && graphics(editor).transform().m11() > scale);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Before layout"));
        auto *layout = editor.findChild<QComboBox *>();
        CHECK(layout != nullptr);
        layout->setCurrentIndex(layout->findData(int(Editor::LayoutDirection::Left)));
        pump();
        CHECK(editor.layoutDirection() == Editor::LayoutDirection::Left);
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Before layout");
        CHECK(changed.size() == 2);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Before programmatic action"));
        trigger(editor, "toggleExpanded");
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Before programmatic action");
        CHECK(!record(exported(editor), "nodes", QStringLiteral("a")).at("expanded").get<bool>() && changed.size() == 4);
        shortcut(editor, Qt::Key_Home);
        CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    }
    {
        m3::qt::EditorConfig config;
        config.shortcuts.acceptTopic = {QKeySequence(QStringLiteral("Return, Ctrl+S")), QKeySequence(Qt::ALT | Qt::Key_Return)};
        config.shortcuts.toggleExpanded.clear();
        config.shortcuts.selectRoot = {QKeySequence(Qt::CTRL | Qt::Key_H), QKeySequence(Qt::ALT | Qt::Key_H)};
        Editor editor(config);
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(QStringLiteral("a")));
        QSignalSpy changed(&editor, &Editor::documentChanged);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Multi-key acceptance"));
        topicKey(editor, Qt::Key_Return);
        CHECK(changed.isEmpty());
        topicKey(editor, Qt::Key_S, Qt::ControlModifier);
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Multi-key acceptance" && changed.size() == 1);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Second acceptance binding"));
        topicKey(editor, Qt::Key_Return, Qt::AltModifier);
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Second acceptance binding" && changed.size() == 2);
        shortcut(editor, Qt::Key_Space);
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded").get<bool>() && changed.size() == 2);
        shortcut(editor, Qt::Key_H, Qt::AltModifier);
        CHECK(editor.selectedNodeId() == QStringLiteral("r"));
        CHECK(editor.selectNode(QStringLiteral("a")));
        shortcut(editor, Qt::Key_H, Qt::ControlModifier);
        CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    }
    {
        m3::qt::EditorConfig config;
        config.shortcuts.acceptTopic.clear();
        Editor editor(config);
        showEditor(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Click accepts without a binding"));
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        CHECK(changed.isEmpty());
        const QString draft = topicInput(editor).toPlainText();
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, blankPoint(graphics(editor)));
        pump();
        CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == utf8(draft));
        CHECK(changed.size() == 1 && editor.selectedNodeId().isEmpty());
    }
    {
        Editor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(QStringLiteral("a")));
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const Json original = exported(editor);
        shortcut(editor, Qt::Key_F2);
        auto &input = topicInput(editor);
        input.setPlainText(QStringLiteral("Retained draft"));
        input.moveCursor(QTextCursor::End);
        input.insertPlainText(QStringLiteral("!"));
        auto cursor = input.textCursor();
        cursor.setPosition(2);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        input.setTextCursor(cursor);
        CHECK(!editor.loadJson(QByteArray("{invalid")));
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("Retained draft!"));
        CHECK(exported(editor) == original && changed.isEmpty());
        QFont font = editor.font(); font.setPointSize(font.pointSize() + 2);
        editor.setFont(font);
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Left));
        CHECK(editor.renameNode(QStringLiteral("b"), QStringLiteral("Unrelated mutation")));
        pump();
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("Retained draft!"));
        CHECK(topicInput(editor).textCursor().position() == 5 && topicInput(editor).textCursor().anchor() == 2);
        topicKey(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("Retained draft"));
        CHECK(changed.size() == 1 && editor.selectedNodeId() == QStringLiteral("a"));
        CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Authoritative external rename")));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 2);
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Authoritative external rename");
        CHECK(editor.newDocument());
        changed.clear();
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Must not leak into reused root"));
        CHECK(editor.newDocument());
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
        const Json replacement = exported(editor);
        CHECK(record(replacement, "nodes", QStringLiteral("root")).at("topic") == "Central topic");
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Must not leak into identical load"));
        CHECK(editor.loadJson(encoded(replacement)));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 2 && exported(editor) == replacement);
        CHECK(editor.loadJson(encoded(editorFixture())));
        CHECK(editor.selectNode(QStringLiteral("a")));
        changed.clear();
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Cancel on selection"));
        CHECK(editor.selectNode(QStringLiteral("b")));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.isEmpty());
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Alpha");
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Cancel on link selection"));
        CHECK(editor.selectLink(QStringLiteral("l1")));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.isEmpty());
        CHECK(editor.selectNode(QStringLiteral("a")));
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Cancel on deletion"));
        CHECK(editor.removeNode(QStringLiteral("a")));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
        CHECK(!hasRecord(exported(editor), "nodes", QStringLiteral("a")));
        CHECK(editor.loadJson(encoded(editorFixture())));
        CHECK(editor.selectNode(QStringLiteral("d")));
        changed.clear();
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Cancel when hidden by collapse"));
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
        CHECK(record(exported(editor), "nodes", QStringLiteral("d")).at("topic") == "Delta");
    }
    {
        QWidget host;
        QVBoxLayout layout(&host);
        Editor first(&host), second(&host);
        layout.addWidget(&first); layout.addWidget(&second);
        CHECK(first.loadJson(encoded(editorFixture())));
        CHECK(second.loadJson(encoded(editorFixture())));
        host.resize(1100, 1000); host.show();
        showEditor(first);
        const Json secondOriginal = exported(second);
        CHECK(first.selectNode(QStringLiteral("a")));
        QSignalSpy firstChanges(&first, &Editor::documentChanged), secondChanges(&second, &Editor::documentChanged);
        shortcut(first, Qt::Key_F2);
        topicInput(first).setPlainText(QStringLiteral("Only first editor"));
        clickLabel(second, QStringLiteral("Beta"));
        CHECK(record(exported(first), "nodes", QStringLiteral("a")).at("topic") == "Only first editor");
        CHECK(firstChanges.size() == 1 && exported(second) == secondOriginal && secondChanges.isEmpty());
        CHECK(second.selectedNodeId() == QStringLiteral("b"));
        shortcut(second, Qt::Key_F2);
        topicInput(second).setPlainText(QStringLiteral("Canceled second draft"));
        topicKey(second, Qt::Key_Escape);
        CHECK(exported(second) == secondOriginal && secondChanges.isEmpty() && firstChanges.size() == 1);
    }
    {
        int changes = 0;
        auto editor = std::make_unique<Editor>();
        showEditor(*editor);
        QObject::connect(editor.get(), &Editor::documentChanged, qApp, [&] { ++changes; });
        shortcut(*editor, Qt::Key_F2);
        topicInput(*editor).setPlainText(QStringLiteral("Destruction is not acceptance"));
        editor.reset();
        pump();
        CHECK(changes == 0);
    }
    {
        class SnapshotHost final : public QWidget {
        public:
            std::function<void()> observeClose;
        protected:
            void closeEvent(QCloseEvent *event) override { observeClose(); event->ignore(); }
        } host;
        QVBoxLayout layout(&host);
        Editor editor(&host);
        layout.addWidget(&editor);
        host.resize(1000, 700); host.show();
        showEditor(editor);
        QAction save; // Window association, not QObject parenting, owns its shortcut.
        save.setShortcut(QKeySequence::Save);
        save.setShortcutContext(Qt::WindowShortcut);
        host.addAction(&save);
        Json saved, closed;
        QObject::connect(&save, &QAction::triggered, &host, [&] { saved = exported(editor); });
        host.observeClose = [&] { closed = exported(editor); };
        QSignalSpy changed(&editor, &Editor::documentChanged);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Host save sees draft"));
        topicKey(editor, Qt::Key_S, Qt::ControlModifier);
        CHECK(record(saved, "nodes", QStringLiteral("root")).at("topic") == "Host save sees draft");
        CHECK(changed.size() == 1);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Host close sees draft"));
        host.close();
        pump();
        CHECK(record(closed, "nodes", QStringLiteral("root")).at("topic") == "Host close sees draft");
        CHECK(changed.size() == 2 && activeTopicInput(editor) == nullptr);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Host popup sees draft"));
        QMenu popup(&host);
        auto *observe = popup.addAction(QStringLiteral("Observe saved topic"));
        QObject::connect(observe, &QAction::triggered, &host, [&] { saved = exported(editor); });
        popup.popup(host.mapToGlobal(QPoint(30, 30)));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 3);
        popup.setActiveAction(observe);
        QTest::keyClick(&popup, Qt::Key_Return);
        pump();
        CHECK(record(saved, "nodes", QStringLiteral("root")).at("topic") == "Host popup sees draft");
    }
    {
        Editor editor;
        showEditor(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).clear();
        topicKey(editor, Qt::Key_Enter, Qt::ControlModifier);
        CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == "" && changed.size() == 1);
        const QString unicode = QString::fromUtf8(" café\t\n世界 🌍 ");
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(unicode);
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == utf8(unicode) && changed.size() == 2);
        const Json saved = exported(editor);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Canceled"));
        topicKey(editor, Qt::Key_Escape);
        CHECK(exported(editor) == saved && changed.size() == 2 && editor.selectedNodeId() == QStringLiteral("root"));
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Invalid") + QChar(0) + QStringLiteral("topic"));
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        CHECK(exported(editor) == saved && changed.size() == 2 && errors.size() == 1);
        CHECK(!editor.lastError().isEmpty() && activeTopicInput(editor) == nullptr);
        shortcut(editor, Qt::Key_F2);
        auto &input = topicInput(editor);
        const QString pasted = QString::fromUtf8("Native paste 世界");
        QApplication::clipboard()->setText(pasted);
        std::exception_ptr menuFailure;
        bool pastedFromMenu = false;
        QTimer::singleShot(0, &editor, [&] {
            QPointer<QMenu> menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            try {
                CHECK(menu != nullptr);
                // Deliver popup exposure/activation before clicking its native action.
                pump();
                CHECK(activeTopicInput(editor) != nullptr && exported(editor) == saved);
                auto &paste = textAction(*menu, {QStringLiteral("Paste")});
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(&paste).center());
                pastedFromMenu = true;
            } catch (...) { menuFailure = std::current_exception(); }
            if (menu) menu->close();
        });
        const QPoint point = input.viewport()->rect().center();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, input.viewport()->mapToGlobal(point));
        QCoreApplication::sendEvent(input.viewport(), &context);
        pump();
        if (menuFailure) std::rethrow_exception(menuFailure);
        CHECK(pastedFromMenu && topicInput(editor).toPlainText() == pasted);
        CHECK(exported(editor) == saved && changed.size() == 2);
        QInputMethodEvent preedit(QString::fromUtf8("未確定"), {});
        QCoreApplication::sendEvent(&topicInput(editor), &preedit);
        CHECK(exported(editor) == saved && changed.size() == 2);
        QInputMethodEvent commit;
        commit.setCommitString(QString::fromUtf8("文"));
        QCoreApplication::sendEvent(&topicInput(editor), &commit);
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == utf8(pasted + QString::fromUtf8("文")));
        CHECK(changed.size() == 3);
    }
}

static void configuration_case() {
    auto configured = [] {
        m3::qt::EditorConfig config;
        config.shortcuts.addChild = {QKeySequence(Qt::CTRL | Qt::Key_J), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J)};
        config.shortcuts.editSelection = {QKeySequence(Qt::CTRL | Qt::Key_R)};
        config.shortcuts.acceptTopic = {QKeySequence(Qt::ALT | Qt::Key_Return)};
        config.shortcuts.deleteSelection = {QKeySequence(Qt::CTRL | Qt::Key_D)};
        config.shortcuts.toggleExpanded.clear();
        config.confirmSubtreeDeletion = false;
        return std::make_unique<Editor>(config);
    }();
    Editor &editor = *configured;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    const Json before = exported(editor);
    shortcut(editor, Qt::Key_J, Qt::ControlModifier);
    const QString child = editor.selectedNodeId();
    CHECK(record(exported(editor), "nodes", child).at("topic") == "" && changed.size() == 1);
    CHECK(QApplication::activeModalWidget() == nullptr);
    topicInput(editor).setPlainText(QStringLiteral("Custom shortcut"));
    topicKey(editor, Qt::Key_Return, Qt::AltModifier);
    CHECK(record(exported(editor), "nodes", child).at("topic") == "Custom shortcut");
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children").back() == utf8(child));
    CHECK(changed.size() == 2);
    shortcut(editor, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    topicInput(editor).setPlainText(QStringLiteral("Second binding"));
    topicKey(editor, Qt::Key_Return, Qt::AltModifier);
    const QString grandchild = editor.selectedNodeId();
    CHECK(record(exported(editor), "nodes", child).at("children") == Json({utf8(grandchild)}));
    shortcut(editor, Qt::Key_R, Qt::ControlModifier);
    topicInput(editor).setPlainText(QStringLiteral("Rebound rename"));
    topicKey(editor, Qt::Key_Return, Qt::AltModifier);
    CHECK(activeTopicInput(editor) == nullptr);
    CHECK(record(exported(editor), "nodes", grandchild).at("topic") == "Rebound rename");
    const Json renamed = exported(editor);
    dialogs({}, [&] { shortcut(editor, Qt::Key_F2); shortcut(editor, Qt::Key_Insert); });
    CHECK(exported(editor) == renamed && changed.size() == 5);
    CHECK(editor.selectNode(QStringLiteral("a")));
    shortcut(editor, Qt::Key_Space);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded") == true);
    CHECK(changed.size() == 5);
    trigger(editor, "toggleExpanded");
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded") == false);
    CHECK(changed.size() == 6);
    CHECK(editor.selectNode(child));
    dialogs({}, [&] { shortcut(editor, Qt::Key_D, Qt::ControlModifier); });
    CHECK(!hasRecord(exported(editor), "nodes", child) && !hasRecord(exported(editor), "nodes", grandchild));
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && changed.size() == 7);
    // Deletion confirmation policy never permits removing the root.
    const Json retained = exported(editor);
    dialogs({}, [&] { shortcut(editor, Qt::Key_D, Qt::ControlModifier); });
    CHECK(exported(editor) == retained && changed.size() == 7);
    Editor independent;
    CHECK(independent.loadJson(encoded(editorFixture())));
    showEditor(independent);
    dialogs({}, [&] { shortcut(independent, Qt::Key_J, Qt::ControlModifier); });
    CHECK(exported(independent) == before);
    shortcut(independent, Qt::Key_Insert);
    topicInput(independent).setPlainText(QStringLiteral("Default binding"));
    topicKey(independent, Qt::Key_Return);
    CHECK(record(exported(independent), "nodes", independent.selectedNodeId()).at("topic") == "Default binding");
    CHECK(exported(editor) == retained && changed.size() == 7);
}

static void shortcuts_case() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    shortcut(editor, Qt::Key_Tab);
    CHECK(QApplication::activeModalWidget() == nullptr);
    CHECK(record(exported(editor), "nodes", editor.selectedNodeId()).at("topic") == "" && changed.size() == 1);
    auto &createdInput = topicInput(editor);
    QTest::keyClicks(&createdInput, "Tab child");
    QTest::keyClick(&createdInput, Qt::Key_Enter, Qt::ShiftModifier | Qt::KeypadModifier);
    QTest::keyClicks(&createdInput, "Second line");
    CHECK(changed.size() == 1 && createdInput.toPlainText() == QStringLiteral("Tab child\nSecond line"));
    topicKey(editor, Qt::Key_Return);
    CHECK(activeTopicInput(editor) == nullptr && changed.size() == 2);
    const QString child = editor.selectedNodeId();
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"a", "b", "c", utf8(child)}));
    CHECK(record(exported(editor), "nodes", child).at("topic") == "Tab child\nSecond line");
    shortcut(editor, Qt::Key_Return);
    topicInput(editor).setPlainText(QStringLiteral("Following sibling"));
    topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
    const QString after = editor.selectedNodeId();
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"a", "b", "c", utf8(child), utf8(after)}));
    shortcut(editor, Qt::Key_Return, Qt::ShiftModifier);
    topicInput(editor).setPlainText(QStringLiteral("Preceding sibling"));
    topicKey(editor, Qt::Key_Enter, Qt::KeypadModifier);
    const QString before = editor.selectedNodeId();
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"a", "b", "c", utf8(child), utf8(before), utf8(after)}));
    CHECK(changed.size() == 6);
    CHECK(editor.selectNode(QStringLiteral("r")));
    shortcut(editor, Qt::Key_Enter);
    topicInput(editor).setPlainText(QStringLiteral("Root Enter child"));
    topicKey(editor, Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
    const QString rootChild = editor.selectedNodeId();
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children").back() == utf8(rootChild));
    CHECK(changed.size() == 8);
    const Json inserted = exported(editor);
    const QString selected = editor.selectedNodeId();
    shortcut(editor, Qt::Key_F2);
    auto &text = topicInput(editor);
    QTest::keyClicks(&text, "Typing");
    QTest::keyClick(&text, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClick(&text, Qt::Key_Tab);
    QTest::keyClicks(&text, "inside topic");
    CHECK(text.toPlainText() == QStringLiteral("Typing\n\tinside topic"));
    CHECK(exported(editor) == inserted && changed.size() == 8);
    topicKey(editor, Qt::Key_Escape);
    CHECK(activeTopicInput(editor) == nullptr);
    CHECK(exported(editor) == inserted && editor.selectedNodeId() == selected && changed.size() == 8);
    CHECK(editor.selectNode(QStringLiteral("a")));
    shortcut(editor, Qt::Key_Right);
    CHECK(editor.selectedNodeId() == QStringLiteral("d"));
    shortcut(editor, Qt::Key_Left);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    shortcut(editor, Qt::Key_Up);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    shortcut(editor, Qt::Key_Down);
    CHECK(editor.selectedNodeId() == QStringLiteral("b"));
    shortcut(editor, Qt::Key_Up);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    shortcut(editor, Qt::Key_Left);
    CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    shortcut(editor, Qt::Key_Right);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    shortcut(editor, Qt::Key_Space);
    CHECK(changed.size() == 9);
    shortcut(editor, Qt::Key_Right);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.lastError().isEmpty());
    shortcut(editor, Qt::Key_Space);
    CHECK(exported(editor) == inserted && changed.size() == 10);
    CHECK(editor.selectLink(QStringLiteral("l1")));
    shortcut(editor, Qt::Key_Home);
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && editor.selectedLinkId().isEmpty());
    shortcut(editor, Qt::Key_Escape);
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
    dialogs({}, [&] { shortcut(editor, Qt::Key_Return); shortcut(editor, Qt::Key_Tab); });
    CHECK(exported(editor) == inserted && changed.size() == 10);
    CHECK(editor.selectNode(QStringLiteral("a")));
    QComboBox *layout = nullptr;
    for (auto *combo : editor.findChildren<QComboBox *>())
        if (combo->findText(QStringLiteral("Balanced")) >= 0) layout = combo;
    CHECK(layout != nullptr);
    dialogs({}, [&] {
        layout->setFocus(); pump();
        QTest::keyClick(layout, Qt::Key_Down);
        QTest::keyClick(layout, Qt::Key_Tab);
    });
    CHECK(editor.layoutDirection() == Editor::LayoutDirection::Right);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && changed.size() == 10);
    auto &view = graphics(editor);
    shortcut(editor, Qt::Key_0, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 1.0) < 1e-6);
    shortcut(editor, Qt::Key_Equal, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 1.2) < 1e-6);
    shortcut(editor, Qt::Key_Minus, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 1.0) < 1e-6);
    CHECK(exported(editor) == inserted && changed.size() == 10);
}

static void controls_case() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QComboBox *layout = nullptr;
    for (auto *picker : editor.findChildren<QComboBox *>())
        if (picker->findText(QStringLiteral("Balanced")) >= 0 &&
            picker->findText(QStringLiteral("Right")) >= 0 && picker->findText(QStringLiteral("Left")) >= 0) layout = picker;
    CHECK(layout != nullptr && layout->findText(QStringLiteral("Outline")) >= 0);
    const Json beforeLayout = exported(editor);
    const QString rootTopic = qs(record(beforeLayout, "nodes", QStringLiteral("r")).at("topic"));
    CHECK(editor.selectNode(QStringLiteral("a")));
    QSignalSpy layoutSelection(&editor, &Editor::selectionChanged);
    const std::vector<std::pair<QString, int>> initialRows{
        {rootTopic, 0}, {QStringLiteral("Alpha"), 1}, {QStringLiteral("Delta"), 2},
        {QStringLiteral("Beta"), 1}, {QString(), 1}
    };
    auto checkOutline = [&](const std::vector<std::pair<QString, int>> &rows) {
        CHECK(editor.layoutDirection() == Editor::LayoutDirection::Outline);
        std::vector<QRectF> rectangles;
        for (const auto &row : rows)
            rectangles.push_back(row.first.isEmpty() ? emptyNodeRect(editor) : topicRect(editor, row.first));
        CHECK(std::abs(rectangles.front().center().x()) < 0.01);
        CHECK(std::abs(rectangles.front().center().y()) < 0.01);
        for (size_t i = 0; i < rows.size(); ++i) {
            CHECK(std::abs(rectangles[i].left() - rectangles.front().left() - 32 * rows[i].second) < 0.01);
            if (i > 0) CHECK(rectangles[i].top() > rectangles[i - 1].bottom());
            if (i > 1) CHECK(std::abs((rectangles[i].top() - rectangles[i - 1].bottom()) -
                                     (rectangles[1].top() - rectangles[0].bottom())) < 0.01);
        }
        assertTreeConnectors(editor, rectangles);
        return rectangles;
    };
    layout->setCurrentIndex(layout->findText(QStringLiteral("Outline")));
    const auto outlineNodes = checkOutline(initialRows);
    const auto widths = std::minmax_element(outlineNodes.begin(), outlineNodes.end(),
        [](const QRectF &a, const QRectF &b) { return a.width() < b.width(); });
    CHECK(widths.second->width() > widths.first->width() + 1);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.selectedLinkId().isEmpty());
    CHECK(exported(editor) == beforeLayout && changed.isEmpty() && layoutSelection.isEmpty());
    for (auto direction : {Editor::LayoutDirection::Left, Editor::LayoutDirection::Right,
                           Editor::LayoutDirection::Balanced}) {
        layout->setCurrentIndex(layout->findData(int(direction)));
        CHECK(editor.layoutDirection() == direction);
        if (direction == Editor::LayoutDirection::Left)
            CHECK(topicRect(editor, QStringLiteral("Alpha")).center().x() < topicRect(editor, rootTopic).center().x());
        if (direction == Editor::LayoutDirection::Right)
            CHECK(topicRect(editor, QStringLiteral("Alpha")).center().x() > topicRect(editor, rootTopic).center().x());
        assertTreeConnectors(editor, {topicRect(editor, rootTopic), topicRect(editor, QStringLiteral("Alpha")),
                                     topicRect(editor, QStringLiteral("Delta")), topicRect(editor, QStringLiteral("Beta")),
                                     emptyNodeRect(editor)});
    }
    layout->setCurrentIndex(layout->findText(QStringLiteral("Outline")));
    CHECK(checkOutline(initialRows) == outlineNodes);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.selectedLinkId().isEmpty());
    CHECK(exported(editor) == beforeLayout && changed.isEmpty() && layoutSelection.isEmpty());
    CHECK(editor.selectNode(QStringLiteral("r")));
    CHECK(editAction(editor, "addChild").isEnabled());
    CHECK(editAction(editor, "editSelection").isEnabled());
    CHECK(editAction(editor, "toggleExpanded").isEnabled());
    CHECK(editAction(editor, "addLink").isEnabled());
    CHECK(!editAction(editor, "deleteSelection").isEnabled());
    CHECK(!editAction(editor, "moveNode").isEnabled());
    CHECK(!editAction(editor, "moveUp").isEnabled());
    CHECK(!editAction(editor, "moveDown").isEnabled());
    CHECK(editor.selectNode(QStringLiteral("a")));
    CHECK(editAction(editor, "deleteSelection").isEnabled());
    CHECK(editAction(editor, "moveNode").isEnabled());
    CHECK(!editAction(editor, "moveUp").isEnabled() && editAction(editor, "moveDown").isEnabled());
    trigger(editor, "addChild");
    CHECK(changed.size() == 1 && record(exported(editor), "nodes", editor.selectedNodeId()).at("topic") == "");
    topicInput(editor).setPlainText(QString::fromUtf8("UI child\n世界"));
    topicKey(editor, Qt::Key_Return);
    CHECK(changed.size() == 2);
    CHECK(!editAction(editor, "toggleExpanded").isEnabled());
    const QString added = editor.selectedNodeId();
    CHECK(!added.isEmpty() && added != QStringLiteral("a"));
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("children") == Json({"d", utf8(added)}));
    CHECK(record(exported(editor), "nodes", added).at("topic") == utf8(QString::fromUtf8("UI child\n世界")));
    CHECK(!texts(editor, QString::fromUtf8("UI child\n世界")).isEmpty());
    checkOutline({{rootTopic, 0}, {QStringLiteral("Alpha"), 1}, {QStringLiteral("Delta"), 2},
                  {QString::fromUtf8("UI child\n世界"), 2}, {QStringLiteral("Beta"), 1}, {QString(), 1}});
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QStringLiteral("Renamed\nfrom F2"));
    topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
    CHECK(record(exported(editor), "nodes", added).at("topic") == "Renamed\nfrom F2");
    CHECK(changed.size() == 3 && editor.selectedNodeId() == added);
    const Json renamed = exported(editor);
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QStringLiteral("Canceled rename"));
    topicKey(editor, Qt::Key_Escape);
    CHECK(exported(editor) == renamed && changed.size() == 3);
    clickLabel(editor, QStringLiteral("Renamed\nfrom F2"), true);
    topicInput(editor).setPlainText(QStringLiteral("Double click topic"));
    topicKey(editor, Qt::Key_Enter, Qt::ControlModifier);
    CHECK(record(exported(editor), "nodes", added).at("topic") == "Double click topic");
    CHECK(changed.size() == 4);
    const std::vector<std::pair<QString, int>> expandedRows{
        {rootTopic, 0}, {QStringLiteral("Alpha"), 1}, {QStringLiteral("Delta"), 2},
        {QStringLiteral("Double click topic"), 2}, {QStringLiteral("Beta"), 1}, {QString(), 1}
    };
    const auto expandedNodes = checkOutline(expandedRows);

    CHECK(editor.selectNode(QStringLiteral("a")));
    shortcut(editor, Qt::Key_Space);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded") == false);
    CHECK(texts(editor, QStringLiteral("Double click topic")).isEmpty());
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
    const auto compactNodes = checkOutline({{rootTopic, 0}, {QStringLiteral("Alpha"), 1},
                                           {QStringLiteral("Beta"), 1}, {QString(), 1}});
    CHECK(compactNodes[2].top() < expandedNodes[4].top());
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    shortcut(editor, Qt::Key_Space);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded") == true);
    CHECK(!texts(editor, QStringLiteral("Double click topic")).isEmpty());
    CHECK(checkOutline(expandedRows) == expandedNodes);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    CHECK(changed.size() == 6);

    CHECK(editor.selectNode(QStringLiteral("d")));
    dialogs({[&](QDialog *dialog) {
        auto *parent = dialog->findChild<QComboBox *>(QStringLiteral("newParent"));
        auto *index = dialog->findChild<QSpinBox *>(QStringLiteral("insertIndex"));
        CHECK(parent != nullptr && index != nullptr);
        CHECK(!pickerIds(*parent).contains(QStringLiteral("d")));
        chooseId(*parent, QStringLiteral("b"));
        CHECK(index->minimum() == 0 && index->maximum() == 0);
        index->setValue(0);
        dialog->accept();
    }}, [&] { shortcut(editor, Qt::Key_M, Qt::ControlModifier); });
    CHECK(record(exported(editor), "nodes", QStringLiteral("b")).at("children") == Json({"d"}));
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("children") == Json({utf8(added)}));
    CHECK(editor.selectedNodeId() == QStringLiteral("d") && changed.size() == 7);
    CHECK(editor.selectNode(QStringLiteral("c")));
    dialogs({[&](QDialog *dialog) {
        auto *parent = dialog->findChild<QComboBox *>(QStringLiteral("newParent"));
        auto *index = dialog->findChild<QSpinBox *>(QStringLiteral("insertIndex"));
        CHECK(parent != nullptr && index != nullptr);
        chooseId(*parent, QStringLiteral("r"));
        CHECK(index->maximum() == 2); // The source was removed before insertion.
        index->setValue(0);
        dialog->accept();
    }}, [&] { trigger(editor, "moveNode"); });
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"c", "a", "b"}));
    CHECK(!editAction(editor, "moveUp").isEnabled() && editAction(editor, "moveDown").isEnabled());
    shortcut(editor, Qt::Key_Down, Qt::ControlModifier);
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"a", "c", "b"}));
    shortcut(editor, Qt::Key_Down, Qt::ControlModifier);
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"a", "b", "c"}));
    CHECK(!editAction(editor, "moveDown").isEnabled());
    const auto atBoundary = changed.size();
    shortcut(editor, Qt::Key_Down, Qt::ControlModifier);
    CHECK(changed.size() == atBoundary);
    shortcut(editor, Qt::Key_Up, Qt::ControlModifier);
    CHECK(record(exported(editor), "nodes", QStringLiteral("r")).at("children") == Json({"a", "c", "b"}));
    CHECK(editor.selectedNodeId() == QStringLiteral("c"));
    CHECK(editor.selectNode(QStringLiteral("a")));
    const Json beforeMoveCancel = exported(editor);
    const auto moveCount = changed.size();
    dialogs({[&](QDialog *dialog) {
        auto *parent = dialog->findChild<QComboBox *>(QStringLiteral("newParent"));
        auto *index = dialog->findChild<QSpinBox *>(QStringLiteral("insertIndex"));
        CHECK(parent != nullptr && index != nullptr);
        QStringList eligible = preorderIds(beforeMoveCancel);
        eligible.removeAll(QStringLiteral("a"));
        eligible.removeAll(added);
        CHECK(pickerIds(*parent) == eligible);
        CHECK(!pickerIds(*parent).contains(QStringLiteral("a")) && !pickerIds(*parent).contains(added));
        dialog->reject();
    }}, [&] { trigger(editor, "moveNode"); });
    CHECK(exported(editor) == beforeMoveCancel && changed.size() == moveCount);

    shortcut(editor, Qt::Key_Space); // Keep a hidden endpoint in the picker.
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("expanded") == false);
    CHECK(editor.selectNode(QStringLiteral("b")));
    const Json beforeLink = exported(editor);
    dialogs({[&](QDialog *dialog) {
        auto *source = dialog->findChild<QComboBox *>(QStringLiteral("sourceNode"));
        auto *target = dialog->findChild<QComboBox *>(QStringLiteral("targetNode"));
        auto *topic = dialog->findChild<QLineEdit *>(QStringLiteral("linkTopic"));
        auto *directed = dialog->findChild<QCheckBox *>(QStringLiteral("directed"));
        CHECK(source != nullptr && target != nullptr && topic != nullptr && directed != nullptr);
        CHECK(pickerIds(*source) == preorderIds(beforeLink) && pickerIds(*target) == preorderIds(beforeLink));
        CHECK(source->currentText().endsWith(QStringLiteral("[b]")));
        CHECK(pickerIds(*target).contains(added));
        chooseId(*source, QStringLiteral("b"));
        chooseId(*target, QStringLiteral("b"));
        topic->setText(QStringLiteral("UI self link"));
        directed->setChecked(true);
        dialog->accept();
    }}, [&] { shortcut(editor, Qt::Key_L, Qt::ControlModifier); });
    const QString uiLink = editor.selectedLinkId();
    CHECK(!uiLink.isEmpty() && !hasRecord(beforeLink, "crossLinks", uiLink));
    CHECK(editor.selectedNodeId().isEmpty());
    CHECK(record(exported(editor), "crossLinks", uiLink).at("source") == "b");
    CHECK(record(exported(editor), "crossLinks", uiLink).at("target") == "b");
    CHECK(record(exported(editor), "crossLinks", uiLink).at("directed") == true);
    const Json beforeLinkCancel = exported(editor);
    const auto linkCount = changed.size();
    dialogs({[&](QDialog *dialog) {
        auto *topic = dialog->findChild<QLineEdit *>(QStringLiteral("linkTopic"));
        CHECK(topic != nullptr && topic->text() == QStringLiteral("UI self link"));
        topic->setText(QStringLiteral("Canceled link"));
        dialog->reject();
    }}, [&] { shortcut(editor, Qt::Key_F2); });
    CHECK(exported(editor) == beforeLinkCancel && changed.size() == linkCount);
    dialogs({[&](QDialog *dialog) {
        auto *source = dialog->findChild<QComboBox *>(QStringLiteral("sourceNode"));
        auto *target = dialog->findChild<QComboBox *>(QStringLiteral("targetNode"));
        auto *topic = dialog->findChild<QLineEdit *>(QStringLiteral("linkTopic"));
        auto *directed = dialog->findChild<QCheckBox *>(QStringLiteral("directed"));
        CHECK(source != nullptr && target != nullptr && topic != nullptr && directed != nullptr);
        CHECK(source->currentText().endsWith(QStringLiteral("[b]")) && target->currentText().endsWith(QStringLiteral("[b]")));
        CHECK(topic->text() == QStringLiteral("UI self link") && directed->isChecked());
        chooseId(*target, added);
        topic->setText(QStringLiteral("UI hidden endpoint"));
        directed->setChecked(false);
        dialog->accept();
    }}, [&] { clickLabel(editor, QStringLiteral("UI self link"), true); });
    CHECK(record(exported(editor), "crossLinks", uiLink).at("target") == utf8(added));
    CHECK(record(exported(editor), "crossLinks", uiLink).at("topic") == "UI hidden endpoint");
    CHECK(record(exported(editor), "crossLinks", uiLink).at("directed") == false);
    CHECK(editor.selectedLinkId().isEmpty() && editor.selectedNodeId().isEmpty());
    CHECK(texts(editor, QStringLiteral("UI hidden endpoint")).isEmpty());
    CHECK(!editAction(editor, "editSelection").isEnabled() && !editAction(editor, "deleteSelection").isEnabled());
    CHECK(editor.selectNode(QStringLiteral("a")));
    shortcut(editor, Qt::Key_Space);
    CHECK(!texts(editor, QStringLiteral("UI hidden endpoint")).isEmpty());
    CHECK(editor.selectLink(uiLink));
    shortcut(editor, Qt::Key_Delete);
    CHECK(!hasRecord(exported(editor), "crossLinks", uiLink));
    CHECK(editor.selectedLinkId().isEmpty());
    CHECK(editor.selectNode(added));
    const Json beforeDeleteCancel = exported(editor);
    const auto deleteCount = changed.size();
    dialogs({deleteResponse(false)}, [&] { shortcut(editor, Qt::Key_Delete); });
    CHECK(exported(editor) == beforeDeleteCancel && changed.size() == deleteCount && editor.selectedNodeId() == added);
    dialogs({deleteResponse(true)}, [&] { trigger(editor, "deleteSelection"); });
    CHECK(!hasRecord(exported(editor), "nodes", added));
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && changed.size() == deleteCount + 1);

    Editor other;
    CHECK(other.loadJson(encoded(editorFixture())));
    showEditor(other, QSize(720, 500));
    const Json otherBefore = exported(other);
    QSignalSpy otherChanges(&other, &Editor::documentChanged);
    CHECK(editor.selectNode(QStringLiteral("b")));
    shortcut(editor, Qt::Key_Insert);
    topicInput(editor).setPlainText(QStringLiteral("Only the focused editor"));
    topicKey(editor, Qt::Key_Return);
    CHECK(record(exported(editor), "nodes", editor.selectedNodeId()).at("topic") == "Only the focused editor");
    CHECK(exported(other) == otherBefore && otherChanges.isEmpty());
    CHECK(other.selectedNodeId() == QStringLiteral("r"));

    Editor collapsed;
    CHECK(collapsed.loadJson(encoded(editorFixture())));
    showEditor(collapsed);
    CHECK(collapsed.selectNode(QStringLiteral("a")));
    trigger(collapsed, "toggleExpanded");
    QSignalSpy collapsedChanges(&collapsed, &Editor::documentChanged);
    shortcut(collapsed, Qt::Key_Insert);
    const QString newChild = collapsed.selectedNodeId();
    CHECK(record(exported(collapsed), "nodes", newChild).at("topic") == "" && collapsedChanges.size() == 2);
    topicInput(collapsed).setPlainText(QStringLiteral("Added while collapsed"));
    topicKey(collapsed, Qt::Key_Return);
    const Json collapsedDoc = exported(collapsed);
    CHECK(record(collapsedDoc, "nodes", QStringLiteral("a")).at("children").back() == utf8(newChild));
    CHECK(record(collapsedDoc, "nodes", newChild).at("topic") == "Added while collapsed");
    CHECK(record(collapsedDoc, "nodes", QStringLiteral("a")).at("expanded") == true);
    CHECK(collapsed.selectedNodeId() == newChild && collapsedChanges.size() == 3);
    CHECK(!texts(collapsed, QStringLiteral("Added while collapsed")).isEmpty());
    const QRectF affordance = topicRect(collapsed, QStringLiteral("Alpha"));
    for (qreal x : {affordance.right() - 9, affordance.left() + 9}) {
        if (!record(exported(collapsed), "nodes", QStringLiteral("a")).at("expanded").get<bool>()) break;
        const QPoint point = graphics(collapsed).mapFromScene(QPointF(x, affordance.center().y()));
        QTest::mouseClick(graphics(collapsed).viewport(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
    }
    CHECK(record(exported(collapsed), "nodes", QStringLiteral("a")).at("expanded") == false);
    CHECK(texts(collapsed, QStringLiteral("Added while collapsed")).isEmpty());
    rejectUnchanged(collapsed, [&] { return collapsed.removeNode(QStringLiteral("r")); });
    const QString shownError = collapsed.lastError();
    bool errorVisible = false;
    for (auto *label : collapsed.findChildren<QLabel *>())
        if (label->isVisible() && label->text().contains(shownError)) errorVisible = true;
    CHECK(errorVisible && QApplication::activeModalWidget() == nullptr);
    CHECK(collapsed.renameNode(QStringLiteral("r"), QStringLiteral("Recovered after failure")));
    CHECK(collapsed.lastError().isEmpty());
    for (auto *label : collapsed.findChildren<QLabel *>())
        CHECK(!label->isVisible() || !label->text().contains(shownError));

    Editor canceled;
    showEditor(canceled);
    QSignalSpy creationChanges(&canceled, &Editor::documentChanged);
    trigger(canceled, "addChild");
    const QString blank = canceled.selectedNodeId();
    const Json created = exported(canceled);
    CHECK(record(created, "nodes", blank).at("topic") == "" && creationChanges.size() == 1);
    topicInput(canceled).setPlainText(QStringLiteral("Discard only this draft"));
    topicKey(canceled, Qt::Key_Escape);
    CHECK(exported(canceled) == created && canceled.selectedNodeId() == blank && creationChanges.size() == 1);
}

#ifdef M3_QT_TEST_DEMO
static void demo_files_case() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("input.json"));
    const QString savedPath = directory.filePath(QStringLiteral("saved.json"));
    const QString malformedPath = directory.filePath(QStringLiteral("malformed.json"));
    const QString unrelatedPath = directory.filePath(QStringLiteral("unrelated.txt"));
    const Json fixtureDoc = nativeDocument(editorFixture());
    writeFile(inputPath, encoded(editorFixture()));
    writeFile(malformedPath, QByteArray("{not valid JSON"));
    const QByteArray sentinel("Keep this unrelated file exactly as it is.\n");
    writeFile(unrelatedPath, sentinel);
    DemoWindow window;
    showDemo(window);
    auto &editor = embedded(window);
    const Json sample = Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[{"id":"r","topic":"M3 Qt editor","children":["a","b","c"]},{"id":"a","topic":"Planning 世界","children":["d"],"note":"Preserve this note","style":{"custom":{"weight":2}}},{"id":"b","topic":"Implementation"},{"id":"c","topic":""},{"id":"d","topic":"Tests & verification"}],"crossLinks":[{"id":"l1","source":"a","target":"b","directed":true,"topic":"Depends on"},{"id":"l2","source":"d","target":"c","directed":false,"topic":"Related"},{"id":"l3","source":"a","target":"a","directed":true,"topic":"Review"},{"id":"l4","source":"a","target":"b","directed":true,"topic":"Feedback"}]})");
    CHECK(exported(editor) == nativeDocument(sample));
    CHECK(!window.isWindowModified() && window.currentFilePath().isEmpty());
    CHECK(window.windowTitle().startsWith(QStringLiteral("M3 Qt editor demo")));
    CHECK(window.windowTitle().contains(QStringLiteral("Untitled")));
    CHECK(!texts(editor, QString::fromUtf8("Planning 世界")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Review")).isEmpty());
    CHECK(QRectF(graphics(editor).viewport()->rect()).adjusted(-3, -3, 3, 3)
              .contains(graphics(editor).mapFromScene(renderedBounds(editor)).boundingRect()));
    CHECK(window.openFile(inputPath));
    CHECK(sameFile(window.currentFilePath(), inputPath));
    CHECK(exported(editor) == fixtureDoc && !window.isWindowModified());
    CHECK(window.windowTitle().contains(QFileInfo(inputPath).fileName()));
    CHECK(!texts(editor, QStringLiteral("Alpha")).isEmpty());
    CHECK(editor.renameNode(QStringLiteral("a"), QString::fromUtf8("Saved 世界\nSecond line")));
    CHECK(window.isWindowModified());
    const Json savedDoc = exported(editor);
    CHECK(window.saveFile(savedPath));
    CHECK(!window.isWindowModified() && sameFile(window.currentFilePath(), savedPath));
    CHECK(fileDocument(savedPath) == savedDoc);
    CHECK(readFile(inputPath) == encoded(editorFixture()));
    CHECK(record(fileDocument(savedPath), "nodes", QStringLiteral("r")).at("style") ==
          record(fixtureDoc, "nodes", QStringLiteral("r")).at("style"));
    CHECK(record(fileDocument(savedPath), "crossLinks", QStringLiteral("l1")) ==
          record(fixtureDoc, "crossLinks", QStringLiteral("l1")));
    DemoWindow reopened;
    CHECK(reopened.openFile(savedPath));
    showDemo(reopened);
    CHECK(exported(embedded(reopened)) == savedDoc && !reopened.isWindowModified());
    CHECK(sameFile(reopened.currentFilePath(), savedPath));
    CHECK(!texts(embedded(reopened), QString::fromUtf8("Saved 世界\nSecond line")).isEmpty());
    CHECK(editor.renameNode(QStringLiteral("d"), QStringLiteral("Unsaved live edit")));
    CHECK(window.isWindowModified());
    const Json live = exported(editor);
    const QString filename = window.currentFilePath();
    const QByteArray goodFile = readFile(savedPath);
    for (const QString &badPath : {directory.filePath(QStringLiteral("missing.json")), malformedPath}) {
        bool result = true;
        dialogs({messageResponse(QMessageBox::Discard)}, [&] { result = window.openFile(badPath); });
        CHECK(!result);
        CHECK(exported(editor) == live && window.currentFilePath() == filename && window.isWindowModified());
        CHECK(!texts(editor, QStringLiteral("Unsaved live edit")).isEmpty());
        CHECK(readFile(savedPath) == goodFile);
    }
    bool result = true;
    dialogs({}, [&] { result = window.saveFile(directory.path()); });
    CHECK(!result);
    CHECK(exported(editor) == live && window.currentFilePath() == filename && window.isWindowModified());
    CHECK(readFile(savedPath) == goodFile && readFile(unrelatedPath) == sentinel);
    dialogs({}, [&] { result = window.saveFile(QString()); });
    CHECK(!result && exported(editor) == live && window.currentFilePath() == filename && window.isWindowModified());
    const Json beforeEmpty = exported(embedded(reopened));
    const QString reopenedName = reopened.currentFilePath();
    dialogs({}, [&] { result = reopened.openFile(QString()); });
    CHECK(!result && exported(embedded(reopened)) == beforeEmpty && reopened.currentFilePath() == reopenedName);
    CHECK(!reopened.isWindowModified());
    CHECK(readFile(unrelatedPath) == sentinel);
}
#endif

#ifdef M3_QT_TEST_DEMO
static void demo_prompts_case() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("original.json"));
    const QString targetPath = directory.filePath(QStringLiteral("target.json"));
    const QString promptedPath = directory.filePath(QStringLiteral("prompt-save.json"));
    const Json initial = nativeDocument(editorFixture());
    Json target = editorFixture();
    setTopic(target, "r", QStringLiteral("Target document"));
    writeFile(inputPath, encoded(initial));
    writeFile(targetPath, encoded(target));
    DemoWindow window;
    CHECK(window.openFile(inputPath));
    showDemo(window);
    auto &editor = embedded(window);
    CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Pending cancel")));
    const Json pending = exported(editor);
    const QString oldPath = window.currentFilePath();
    bool result = true;
    dialogs({messageResponse(QMessageBox::Cancel)}, [&] { result = window.newFile(); });
    CHECK(!result && exported(editor) == pending && window.currentFilePath() == oldPath && window.isWindowModified());
    dialogs({messageResponse(QMessageBox::Cancel)}, [&] { result = window.openFile(targetPath); });
    CHECK(!result && exported(editor) == pending && window.currentFilePath() == oldPath && window.isWindowModified());
    dialogs({messageResponse(QMessageBox::Cancel)}, [&] { result = window.close(); });
    CHECK(!result && window.isVisible());
    CHECK(exported(editor) == pending && window.currentFilePath() == oldPath && window.isWindowModified());
    CHECK(fileDocument(inputPath) == initial);
    dialogs({messageResponse(QMessageBox::Discard)}, [&] { result = window.newFile(); });
    CHECK(result && !window.isWindowModified() && window.currentFilePath().isEmpty());
    CHECK(exported(editor).at("rootId") == "root");
    CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == "Central topic");
    CHECK(fileDocument(inputPath) == initial);

    CHECK(editor.renameNode(QStringLiteral("root"), QStringLiteral("Discard before open")));
    dialogs({messageResponse(QMessageBox::Discard)}, [&] { result = window.openFile(inputPath); });
    CHECK(result && exported(editor) == initial && sameFile(window.currentFilePath(), inputPath));
    CHECK(!window.isWindowModified());
    CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Save before new")));
    const Json saveBeforeNew = exported(editor);
    dialogs({messageResponse(QMessageBox::Save)}, [&] { result = window.newFile(); });
    CHECK(result && !window.isWindowModified() && window.currentFilePath().isEmpty());
    CHECK(fileDocument(inputPath) == saveBeforeNew);
    CHECK(exported(editor).at("rootId") == "root");

    CHECK(editor.renameNode(QStringLiteral("root"), QStringLiteral("Save dialog cancellation")));
    const Json beforeFileCancel = exported(editor);
    dialogs({messageResponse(QMessageBox::Save), fileResponse(QString(), false)}, [&] { result = window.newFile(); });
    CHECK(!result && exported(editor) == beforeFileCancel && window.currentFilePath().isEmpty() && window.isWindowModified());
    CHECK(!QFileInfo::exists(promptedPath));
    dialogs({fileResponse(QString(), false)}, [&] { hostAction(window, QKeySequence::Open).trigger(); });
    CHECK(exported(editor) == beforeFileCancel && window.currentFilePath().isEmpty() && window.isWindowModified());
    dialogs({fileResponse(QString(), false)}, [&] { hostAction(window, QKeySequence::SaveAs).trigger(); });
    CHECK(exported(editor) == beforeFileCancel && window.currentFilePath().isEmpty() && window.isWindowModified());
    dialogs({messageResponse(QMessageBox::Save), fileResponse(promptedPath, true)}, [&] { result = window.openFile(targetPath); });
    CHECK(result);
    CHECK(fileDocument(promptedPath) == beforeFileCancel);
    CHECK(exported(editor) == nativeDocument(target));
    CHECK(sameFile(window.currentFilePath(), targetPath) && !window.isWindowModified());

    // Replace the current path with a directory: Save is a real QSaveFile
    // failure, without depending on permission bits or a chooser accepting an
    // invalid filename. The failed pending save must abort each transition.
    const QString blockedPath = directory.filePath(QStringLiteral("blocked.json"));
    CHECK(window.saveFile(blockedPath));
    CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Cannot save this yet")));
    const Json blockedDoc = exported(editor);
    const QString blockedName = window.currentFilePath();
    CHECK(QFile::remove(blockedPath));
    CHECK(QDir().mkdir(blockedPath));
    dialogs({messageResponse(QMessageBox::Save)}, [&] { result = window.newFile(); });
    CHECK(!result && exported(editor) == blockedDoc && window.currentFilePath() == blockedName && window.isWindowModified());
    dialogs({messageResponse(QMessageBox::Save)}, [&] { result = window.openFile(inputPath); });
    CHECK(!result && exported(editor) == blockedDoc && window.currentFilePath() == blockedName && window.isWindowModified());
    dialogs({messageResponse(QMessageBox::Save)}, [&] { result = window.close(); });
    CHECK(!result && window.isVisible());
    CHECK(exported(editor) == blockedDoc && window.currentFilePath() == blockedName && window.isWindowModified());
    CHECK(fileDocument(inputPath) == saveBeforeNew && readFile(targetPath) == encoded(target));

    const QString closingPath = directory.filePath(QStringLiteral("closing.json"));
    CHECK(window.saveFile(closingPath));
    CHECK(!window.isWindowModified());
    CHECK(editor.renameNode(QStringLiteral("d"), QStringLiteral("Persist before close")));
    const Json beforeClose = exported(editor);
    dialogs({messageResponse(QMessageBox::Save)}, [&] { result = window.close(); });
    CHECK(result && !window.isVisible());
    CHECK(fileDocument(closingPath) == beforeClose && !window.isWindowModified());

    DemoWindow discarded;
    CHECK(discarded.openFile(inputPath));
    showDemo(discarded);
    CHECK(embedded(discarded).renameNode(QStringLiteral("a"), QStringLiteral("Discard before close")));
    dialogs({messageResponse(QMessageBox::Discard)}, [&] { result = discarded.close(); });
    CHECK(result && !discarded.isVisible());
    CHECK(fileDocument(inputPath) == saveBeforeNew);
}
#endif

static void tree_edits_scene() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor);
    CHECK(!texts(editor, QStringLiteral("Alpha")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Beta")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
    const QString added = editor.addNode(QStringLiteral("a"), QStringLiteral("Added visible\n世界"));
    CHECK(!added.isEmpty());
    CHECK(!texts(editor, QStringLiteral("Added visible\n世界")).isEmpty());
    CHECK(topicRect(editor, QStringLiteral("Added visible\n世界")).center().x() > topicRect(editor, QStringLiteral("Alpha")).center().x());
    CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Renamed visible")));
    CHECK(texts(editor, QStringLiteral("Alpha")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Renamed visible")).isEmpty());
    const QRectF deltaBefore = topicRect(editor, QStringLiteral("Delta"));
    CHECK(editor.moveNode(QStringLiteral("d"), QStringLiteral("b")));
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(topicRect(editor, QStringLiteral("Delta")).center().x() > topicRect(editor, QStringLiteral("Beta")).center().x());
    CHECK(topicRect(editor, QStringLiteral("Delta")).center() != deltaBefore.center());
    CHECK(editor.moveNode(QStringLiteral("c"), QStringLiteral("r"), 0));
    CHECK(emptyNodeRect(editor).center().y() < topicRect(editor, QStringLiteral("Renamed visible")).center().y());
    CHECK(topicRect(editor, QStringLiteral("Renamed visible")).center().y() < topicRect(editor, QStringLiteral("Beta")).center().y());
    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(editor.removeNode(QStringLiteral("b")));
    CHECK(texts(editor, QStringLiteral("Beta")).isEmpty() && texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(texts(editor, QStringLiteral("Related")).isEmpty() && texts(editor, QStringLiteral("Other")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Added visible\n世界")).isEmpty());
    CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    CHECK(editor.selectNode(QStringLiteral("c")));
    CHECK(finiteRect(emptyNodeRect(editor)));
}

static void collapse_scene() {
    Editor editor;
    const Json input = editorFixture();
    CHECK(editor.loadJson(encoded(input)));
    showEditor(editor);
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty() && !texts(editor, QStringLiteral("Other")).isEmpty());
    clickLabel(editor, QStringLiteral("Delta"));
    CHECK(editor.selectedNodeId() == QStringLiteral("d"));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty() && texts(editor, QStringLiteral("Other")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Alpha")).isEmpty() && !texts(editor, QStringLiteral("Related")).isEmpty());
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    CHECK(editor.moveNode(QStringLiteral("b"), QStringLiteral("a")));
    const QString inserted = editor.addNode(QStringLiteral("a"), QStringLiteral("Hidden child"));
    CHECK(!inserted.isEmpty());
    CHECK(texts(editor, QStringLiteral("Hidden child")).isEmpty());
    CHECK(texts(editor, QStringLiteral("Beta")).isEmpty() && texts(editor, QStringLiteral("Related")).isEmpty());
    CHECK(editor.setExpanded(QStringLiteral("a"), true));
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty() && !texts(editor, QStringLiteral("Other")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Beta")).isEmpty() && !texts(editor, QStringLiteral("Related")).isEmpty());
    CHECK(!texts(editor, QStringLiteral("Hidden child")).isEmpty());
    CHECK(editor.selectNode(inserted));
    CHECK(editor.setExpanded(QStringLiteral("r"), false));
    CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    for (const QString &topic : {QStringLiteral("Alpha"), QStringLiteral("Beta"), QStringLiteral("Delta"),
                                 QStringLiteral("Hidden child"), QStringLiteral("Related"), QStringLiteral("Other")})
        CHECK(texts(editor, topic).isEmpty());
    CHECK(texts(editor, QString()).isEmpty());
    CHECK(!texts(editor, qs(record(nativeDocument(input), "nodes", QStringLiteral("r")).at("topic"))).isEmpty());
    CHECK(editor.setExpanded(QStringLiteral("r"), true));
    CHECK(!texts(editor, QStringLiteral("Hidden child")).isEmpty());
    CHECK(editor.selectNode(inserted));

    // Centering must work at either scene edge, without fitting or selecting the branch.
    Json centeredInput = editorFixture();
    for (auto &entry : centeredInput.at("nodes")) if (entry.at("id") == "a") {
        entry["expanded"] = false;
        entry["children"] = Json({"d", "e", "f"});
    }
    centeredInput["nodes"].push_back({{"id", "e"}, {"topic", "Second child"}});
    centeredInput["nodes"].push_back({{"id", "f"}, {"topic", "Third child"}});
    for (const auto direction : {Editor::LayoutDirection::Right, Editor::LayoutDirection::Left}) {
        Editor centered;
        CHECK(centered.loadJson(encoded(centeredInput)));
        CHECK(centered.setLayoutDirection(direction));
        showEditor(centered);
        trigger(centered, "resetZoom");
        CHECK(centered.selectNode(QStringLiteral("a")));
        auto &centeredView = graphics(centered);
        const QTransform zoom = centeredView.transform();
        const Json collapsed = exported(centered);
        Json expanded = collapsed;
        for (auto &entry : expanded.at("nodes")) if (entry.at("id") == "a") entry["expanded"] = true;
        QSignalSpy centeredChanges(&centered, &Editor::documentChanged), centeredSelection(&centered, &Editor::selectionChanged);
        auto parentCentered = [&] {
            // Compare continuous viewport coordinates, not two independently rounded integer points.
            const QPointF midpoint(centeredView.viewport()->width() / 2.0, centeredView.viewport()->height() / 2.0);
            return QLineF(centeredView.viewportTransform().map(topicRect(centered, QStringLiteral("Alpha")).center()),
                          midpoint).length() <= 2.0;
        };
        auto childrenVisible = [&] {
            for (const auto &topic : {QStringLiteral("Delta"), QStringLiteral("Second child"), QStringLiteral("Third child")})
                CHECK(centeredView.viewport()->rect().adjusted(-1, -1, 1, 1).contains(
                    centeredView.mapFromScene(topicRect(centered, topic)).boundingRect()));
        };
        CHECK(!parentCentered());
        shortcut(centered, Qt::Key_Space);
        CHECK(parentCentered());
        childrenVisible();
        CHECK(centeredView.transform() == zoom && centered.selectedNodeId() == QStringLiteral("a"));
        CHECK(exported(centered) == expanded && centeredChanges.size() == 1 && centeredSelection.isEmpty());

        CHECK(centered.selectNode(QStringLiteral("b")));
        auto *scroll = centeredView.horizontalScrollBar();
        scroll->setValue(scroll->maximum());
        pump();
        const QPointF beforeCollapse = centeredView.mapToScene(centeredView.viewport()->rect().center());
        CHECK(QLineF(beforeCollapse, topicRect(centered, QStringLiteral("Alpha")).center()).length() > 2.0);
        CHECK(centered.setExpanded(QStringLiteral("a"), false));
        pump();
        CHECK(QLineF(beforeCollapse, centeredView.mapToScene(centeredView.viewport()->rect().center())).length() < 2.0);
        CHECK(exported(centered) == collapsed && centeredChanges.size() == 2 && centeredSelection.size() == 1);

        const QRectF parent = topicRect(centered, QStringLiteral("Alpha"));
        const QPoint circle = centeredView.mapFromScene(QPointF(parent.right() - 15, parent.center().y()));
        CHECK(centeredView.viewport()->rect().contains(circle));
        QTest::mouseClick(centeredView.viewport(), Qt::LeftButton, Qt::NoModifier, circle);
        pump();
        CHECK(parentCentered());
        childrenVisible();
        CHECK(centeredView.transform() == zoom && centered.selectedNodeId() == QStringLiteral("b"));
        CHECK(exported(centered) == expanded && centeredChanges.size() == 3 && centeredSelection.size() == 1);
    }
}

static void graph_edits_scene() {
    Editor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor);
    const QString first = editor.addLink(QStringLiteral("a"), QStringLiteral("b"), true, QStringLiteral("Route one"));
    const QString second = editor.addLink(QStringLiteral("a"), QStringLiteral("b"), false, QStringLiteral("Route two"));
    const QString self = editor.addLink(QStringLiteral("a"), QStringLiteral("a"), true, QStringLiteral("Self route"));
    CHECK(!first.isEmpty() && !second.isEmpty() && !self.isEmpty());
    editor.fitToContents();
    pump();
    const std::vector<QRectF> nodes{
        topicRect(editor, qs(record(exported(editor), "nodes", QStringLiteral("r")).at("topic"))),
        topicRect(editor, QStringLiteral("Alpha")), topicRect(editor, QStringLiteral("Beta")),
        topicRect(editor, QStringLiteral("Delta")), emptyNodeRect(editor)
    };
    for (const auto &route : std::vector<std::pair<QString, QString>>{
             {QStringLiteral("Route one"), first}, {QStringLiteral("Route two"), second},
             {QStringLiteral("Self route"), self}}) {
        clickLabel(editor, route.first);
        CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId() == route.second);
        editor.clearSelection();
        const QPoint point = curvePoint(editor, route.first, nodes);
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
        CHECK(editor.selectedLinkId() == route.second);
    }
    const QRectF alpha = topicRect(editor, QStringLiteral("Alpha"));
    const QRectF selfBounds = ownerItem(textItem(editor, QStringLiteral("Self route")))->sceneBoundingRect();
    CHECK(selfBounds.top() < alpha.top() && selfBounds.right() > alpha.right());
    CHECK(editor.selectLink(first));
    CHECK(editor.updateLink(first, QStringLiteral("c"), QStringLiteral("d"), false, QStringLiteral("Changed route")));
    CHECK(texts(editor, QStringLiteral("Route one")).isEmpty());
    clickLabel(editor, QStringLiteral("Changed route"));
    CHECK(editor.selectedLinkId() == first);
    CHECK(editor.removeLink(first));
    CHECK(editor.selectedLinkId().isEmpty());
    CHECK(texts(editor, QStringLiteral("Changed route")).isEmpty());
    clickLabel(editor, QStringLiteral("Route two"));
    CHECK(editor.selectedLinkId() == second);
    clickLabel(editor, QStringLiteral("Self route"));
    CHECK(editor.selectedLinkId() == self);
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    CHECK(editor.selectedLinkId().isEmpty());
    CHECK(texts(editor, QStringLiteral("Other")).isEmpty());
}

int main(int argc, char **argv) {
    try {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication application(argc, argv);
        QApplication::setQuitOnLastWindowClosed(false);
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "document") document_case();
        else if (name == "tree_edits") { tree_edits_case(); tree_edits_scene(); }
        else if (name == "collapse") { collapse_case(); collapse_scene(); }
        else if (name == "graph_edits") { graph_edits_case(); graph_edits_scene(); }
        else if (name == "render") { render_case(); routing_case(); }
        else if (name == "navigation") navigation_case();
        else if (name == "node_drag") node_drag_case();
        else if (name == "properties") properties_case();
        else if (name == "hyperlinks") hyperlinks_case();
        else if (name == "controls") controls_case();
        else if (name == "inline_edit") inline_edit_case();
        else if (name == "configuration") configuration_case();
        else if (name == "shortcuts") shortcuts_case();
        else if (name == "lifetime") lifetime_case();
#ifdef M3_QT_TEST_DEMO
        else if (name == "demo_files") demo_files_case();
        else if (name == "demo_prompts") demo_prompts_case();
#endif
        else throw std::runtime_error("Unknown Qt test case: " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
