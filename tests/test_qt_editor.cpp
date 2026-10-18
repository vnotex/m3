#include "test_support.h"
#include "m3/qt/editor.h"
#ifdef M3_QT_TEST_DEMO
#include "qt_demo_window.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#endif
#include <QAction>
#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFocusEvent>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineF>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QMainWindow>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEngine>
#include <QRegularExpression>
#include <QPalette>
#include <QPointer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QScreen>
#include <QShortcut>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QUrl>
#include <QWheelEvent>
#include <QWindow>
#include <QXmlStreamReader>
#include <QMap>
#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

using Editor = m3::qt::MindMapEditor;

// Existing rendering fixtures exercise authored labels, not automatic decoration.
struct RawLabelConfig : m3::qt::EditorConfig {
    RawLabelConfig() { autoSectionNumbering = false; }
};
class RawLabelEditor : public Editor {
public:
    explicit RawLabelEditor(QWidget *parent = nullptr) : Editor(RawLabelConfig{}, parent) {}
    explicit RawLabelEditor(const m3::qt::EditorConfig &config, QWidget *parent = nullptr)
        : Editor(config, parent) {}
};

static QString qs(const Json &value) {
    const auto &text = value.get_ref<const std::string &>();
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
static std::string utf8(const QString &text) { return text.toUtf8().toStdString(); }
static QByteArray encoded(const Json &value) { return QByteArray::fromStdString(value.dump()); }
// A real host override reached only through the native action/drag boundary.
class MimeEditor : public RawLabelEditor {
public:
    using RawLabelEditor::RawLabelEditor;
    QStringList targets;
    QList<MimeInsertionSource> origins;
    std::function<void()> insertion;
protected:
    bool canInsertFromMimeData(const QMimeData *source, MimeInsertionSource) const override {
        return source && (source->hasUrls() || source->hasImage() ||
            source->text().startsWith(QStringLiteral("https://")));
    }
    void insertFromMimeData(const QString &id, const QMimeData *source, MimeInsertionSource origin) override {
        targets.append(id);
        origins.append(origin);
        const auto urls = source->urls();
        const QString url = urls.isEmpty() ? source->text() : urls.front().toString(QUrl::FullyEncoded);
        if (insertion) {
            const auto callback = insertion;
            callback();
        } else CHECK(updateNode(id, encoded(Json{{"hyperLink", utf8(url)}})));
    }
};
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
static void expandProperties(Editor &editor) {
    auto *toggle = editor.findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
    CHECK(toggle && !editor.nodePropertiesPanel());
    toggle->setChecked(true);
    pump();
    auto *scroll = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"))->findChild<QScrollArea *>();
    CHECK(scroll && scroll->isVisible());
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
static QPoint expansionPoint(Editor &editor, const QString &topic) {
    const QRectF rect = topicRect(editor, topic).adjusted(2, 2, -2, -2);
    return graphics(editor).mapFromScene(QPointF(rect.right() - 22, rect.center().y()));
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
    auto *action = editor.commandAction(QString::fromLatin1(name));
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
    editor->setSectionNumbering(false, QStringLiteral("1.1."));
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

struct HtmlElement {
    QString tag, text;
    QMap<QString, QString> attributes;
    int parent = -1;
};
struct HtmlPage {
    std::vector<HtmlElement> elements;
    explicit HtmlPage(const QString &html) {
        CHECK(!html.isEmpty());
        QXmlStreamReader reader(html);
        std::vector<int> stack;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) {
                HtmlElement element;
                element.tag = reader.name().toString();
                if (element.tag == QStringLiteral("br")) element.text = QStringLiteral("\n");
                element.parent = stack.empty() ? -1 : stack.back();
                for (const auto &attribute : reader.attributes())
                    element.attributes.insert(attribute.name().toString(), attribute.value().toString());
                elements.push_back(std::move(element));
                stack.push_back(static_cast<int>(elements.size()) - 1);
            } else if (reader.isCharacters() && !stack.empty()) {
                elements[stack.back()].text += reader.text();
            } else if (reader.isEndElement()) {
                CHECK(!stack.empty());
                const int child = stack.back();
                stack.pop_back();
                if (!stack.empty()) elements[stack.back()].text += elements[child].text;
            }
        }
        CHECK(!reader.hasError() && stack.empty());
    }
    std::vector<int> children(int parent) const {
        std::vector<int> result;
        for (size_t i = 0; i < elements.size(); ++i)
            if (elements[i].parent == parent) result.push_back(static_cast<int>(i));
        return result;
    }
    std::vector<int> tags(const QString &tag, int ancestor = -1) const {
        std::vector<int> matches;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (elements[i].tag != tag) continue;
            int parent = static_cast<int>(i);
            while (parent >= 0 && parent != ancestor) parent = elements[parent].parent;
            if (ancestor < 0 || parent == ancestor) matches.push_back(static_cast<int>(i));
        }
        return matches;
    }
    QStringList articleIds() const {
        QStringList ids;
        for (const int i : tags(QStringLiteral("article"))) ids.append(elements[i].attributes.value(QStringLiteral("id")));
        return ids;
    }
    QStringList values(const QString &tag) const {
        QStringList result;
        for (const int i : tags(tag)) result.append(elements[i].text);
        return result;
    }
    std::vector<int> classes(const QString &name, int ancestor = -1) const {
        std::vector<int> matches;
        for (size_t i = 0; i < elements.size(); ++i) {
            if (!elements[i].attributes.value(QStringLiteral("class")).split(u' ').contains(name)) continue;
            int parent = static_cast<int>(i);
            while (parent >= 0 && parent != ancestor) parent = elements[parent].parent;
            if (ancestor < 0 || parent == ancestor) matches.push_back(static_cast<int>(i));
        }
        return matches;
    }
    QStringList classValues(const QString &name, int ancestor = -1) const {
        QStringList result;
        for (const int i : classes(name, ancestor)) result.append(elements[i].text);
        return result;
    }
    int identified(const QString &attribute, const QString &value) const {
        int match = -1;
        for (size_t i = 0; i < elements.size(); ++i) if (elements[i].attributes.value(attribute) == value) {
            CHECK(match == -1);
            match = static_cast<int>(i);
        }
        CHECK(match >= 0);
        return match;
    }
    int node(const QString &id) const { return identified(QStringLiteral("data-node-id"), id); }
    int article(const QString &id) const {
        return identified(QStringLiteral("id"), QStringLiteral("m3-node-") + QString::fromLatin1(id.toUtf8().toHex()));
    }
    QSizeF size() const {
        const auto &attributes = elements[identified(QStringLiteral("id"), QStringLiteral("mindmap"))].attributes;
        bool widthOk = false, heightOk = false;
        const double width = attributes.value(QStringLiteral("data-width")).toDouble(&widthOk);
        const double height = attributes.value(QStringLiteral("data-height")).toDouble(&heightOk);
        CHECK(widthOk && heightOk && std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0);
        return QSizeF(width, height);
    }
    QImage nodeImage(int index) const {
        CHECK(elements[index].tag == QStringLiteral("img"));
        const QString source = elements[index].attributes.value(QStringLiteral("src"));
        const QString prefix = QStringLiteral("data:image/png;base64,");
        CHECK(source.startsWith(prefix));
        const QImage image = QImage::fromData(QByteArray::fromBase64(source.mid(prefix.size()).toLatin1()), "PNG");
        CHECK(!image.isNull());
        return image;
    }
};
static void checkHtmlDocument(const HtmlPage &page) {
    const int visual = page.identified(QStringLiteral("id"), QStringLiteral("visual-map"));
    const int map = page.identified(QStringLiteral("id"), QStringLiteral("mindmap"));
    const int separator = page.identified(QStringLiteral("id"), QStringLiteral("map-articles-separator"));
    const int articles = page.identified(QStringLiteral("id"), QStringLiteral("articles"));
    CHECK(page.children(visual) == std::vector<int>{map});
    CHECK(page.elements[visual].parent == page.elements[separator].parent);
    CHECK(page.elements[visual].parent == page.elements[articles].parent && visual < separator && separator < articles);
    CHECK(page.elements[separator].tag == QStringLiteral("hr"));
    CHECK(page.tags(QStringLiteral("script")).empty() && page.tags(QStringLiteral("button")).empty());
    for (const auto &element : page.elements) {
        CHECK(!element.attributes.contains(QStringLiteral("hidden")));
        CHECK(!element.attributes.contains(QStringLiteral("data-mode")));
        CHECK(!element.attributes.contains(QStringLiteral("data-main-node")));
        for (auto it = element.attributes.begin(); it != element.attributes.end(); ++it)
            CHECK(!it.key().startsWith(QStringLiteral("on"), Qt::CaseInsensitive));
    }
    const QStringList rowOrder{QStringLiteral("m3-article-title"), QStringLiteral("m3-article-icons"),
        QStringLiteral("m3-article-tags"), QStringLiteral("m3-article-url"), QStringLiteral("m3-article-references"),
        QStringLiteral("m3-article-note"), QStringLiteral("m3-article-figure")};
    const QStringList rowTags{QString(), QStringLiteral("div"), QStringLiteral("div"), QStringLiteral("p"),
        QStringLiteral("div"), QStringLiteral("div"), QStringLiteral("figure")};
    for (const int article : page.tags(QStringLiteral("article"))) {
        const auto rows = page.children(article);
        CHECK(!rows.empty());
        int previous = -1;
        for (const int row : rows) {
            const auto &element = page.elements[row];
            const int order = rowOrder.indexOf(element.attributes.value(QStringLiteral("class")));
            CHECK(order >= 0 && order > previous);
            CHECK(order == 0 || element.tag == rowTags[order]);
            previous = order;
        }
        const int title = rows.front();
        CHECK(page.elements[title].attributes.value(QStringLiteral("class")) == rowOrder.front());
        const auto topics = page.classes(QStringLiteral("m3-article-topic"), article);
        CHECK(topics.size() == 1 && page.children(title) == topics);
        CHECK(page.elements[topics.front()].tag == QStringLiteral("span"));
        CHECK(page.elements[title].text == page.elements[topics.front()].text);
        const QString tag = page.elements[title].tag;
        if (tag == QStringLiteral("p")) CHECK(page.elements[page.elements[article].parent].tag == QStringLiteral("li"));
        else {
            CHECK(tag.size() == 2 && tag[0] == u'h' && tag[1] >= u'1' && tag[1] <= u'6');
            CHECK(page.elements[article].parent == articles);
        }
    }
    for (const int list : page.tags(QStringLiteral("ul"), articles)) {
        const int parent = page.elements[list].parent;
        CHECK(parent == articles || page.elements[parent].tag == QStringLiteral("li"));
        const auto items = page.children(list);
        CHECK(!items.empty());
        for (const int item : items) CHECK(page.elements[item].tag == QStringLiteral("li"));
        if (parent == articles) {
            int previous = -1;
            for (const int sibling : page.children(articles)) if (sibling < list) previous = sibling;
            CHECK(previous >= 0 && page.elements[previous].tag == QStringLiteral("article"));
            CHECK(page.elements[page.children(previous).front()].tag == QStringLiteral("h6"));
        }
    }
    for (const int item : page.tags(QStringLiteral("li"), articles)) {
        CHECK(page.elements[page.elements[item].parent].tag == QStringLiteral("ul"));
        const auto contents = page.children(item);
        CHECK(contents.size() == 1 || contents.size() == 2);
        CHECK(page.elements[contents.front()].tag == QStringLiteral("article"));
        if (contents.size() == 2) CHECK(page.elements[contents.back()].tag == QStringLiteral("ul"));
    }
    for (const int node : page.classes(QStringLiteral("m3-node"))) {
        const auto topics = page.classes(QStringLiteral("m3-topic"), node);
        CHECK(topics.size() == 1);
        const auto &topic = page.elements[topics.front()];
        const int article = page.article(page.elements[node].attributes.value(QStringLiteral("data-node-id")));
        CHECK(topic.tag == QStringLiteral("a"));
        CHECK(topic.attributes.value(QStringLiteral("href")) == u'#' + page.elements[article].attributes.value(QStringLiteral("id")));
        CHECK(topic.attributes.value(QStringLiteral("draggable")) == QStringLiteral("false"));
    }
}
static void checkHtmlReferences(const HtmlPage &page, const QString &id,
                                std::initializer_list<std::pair<QString, QString>> expected) {
    const int article = page.article(id);
    const auto topics = page.classes(QStringLiteral("m3-article-topic"), article);
    CHECK(topics.size() == 1 && page.elements[topics.front()].tag == QStringLiteral("span"));
    const int title = page.elements[topics.front()].parent;
    CHECK(page.elements[title].parent == article);
    CHECK(page.elements[title].attributes.value(QStringLiteral("class")) == QStringLiteral("m3-article-title"));
    const auto containers = page.classes(QStringLiteral("m3-article-references"), article);
    const auto links = page.classes(QStringLiteral("m3-cross-reference"), article);
    CHECK(links.size() == expected.size());
    if (expected.size() == 0) { CHECK(containers.empty()); return; }
    CHECK(containers.size() == 1 && page.elements[containers.front()].parent == article);
    CHECK(page.elements[containers.front()].tag == QStringLiteral("div") && containers.front() > title);
    size_t next = 0;
    for (const auto &reference : expected) {
        const auto &link = page.elements[links[next++]];
        const int destination = page.article(reference.first);
        const auto target = page.classValues(QStringLiteral("m3-article-topic"), destination);
        CHECK(target.size() == 1 && !target.front().isEmpty());
        CHECK(link.tag == QStringLiteral("a") && link.parent == containers.front());
        CHECK(link.attributes.value(QStringLiteral("href")) == u'#' + page.elements[destination].attributes.value(QStringLiteral("id")));
        CHECK(link.attributes.value(QStringLiteral("title")) == reference.second);
        CHECK(link.text.contains(target.front()));
    }
}
static void checkHtmlRows(const HtmlPage &page, const QString &id, const QStringList &expected) {
    QStringList rows;
    for (const int row : page.children(page.article(id)))
        rows.append(page.elements[row].attributes.value(QStringLiteral("class")));
    CHECK(rows == expected);
}
static void checkHtmlTitle(const HtmlPage &page, const QString &id, const QString &tag, const QString &topic) {
    const int article = page.article(id);
    const auto titles = page.classes(QStringLiteral("m3-article-title"), article);
    CHECK(titles.size() == 1 && page.elements[titles.front()].parent == article);
    CHECK(page.elements[titles.front()].tag == tag && page.elements[titles.front()].text == topic);
}
static QMap<QString, QString> htmlStyle(const HtmlElement &element) {
    QMap<QString, QString> result;
    const QString style = element.attributes.value(QStringLiteral("style"));
    QString declaration;
    QChar quote;
    bool escaped = false;
    auto append = [&] {
        const int colon = declaration.indexOf(u':');
        if (colon >= 0) {
            const QString key = declaration.left(colon).trimmed();
            CHECK(!result.contains(key));
            result.insert(key, declaration.mid(colon + 1).trimmed());
        } else CHECK(declaration.trimmed().isEmpty());
        declaration.clear();
    };
    for (const QChar c : style) {
        if (escaped) escaped = false;
        else if (c == u'\\') escaped = true;
        else if (!quote.isNull()) { if (c == quote) quote = QChar(); }
        else if (c == u'\'' || c == u'"') quote = c;
        else if (c == u';') { append(); continue; }
        declaration += c;
    }
    CHECK(quote.isNull() && !escaped);
    append();
    return result;
}
static QString htmlCssString(const QString &value) {
    CHECK(value.size() >= 2 && (value.front() == u'\'' || value.front() == u'"') && value.back() == value.front());
    QString result;
    for (qsizetype i = 1; i + 1 < value.size(); ++i) {
        if (value[i] != u'\\') { result += value[i]; continue; }
        CHECK(++i + 1 < value.size());
        QString digits;
        while (i + 1 < value.size() && digits.size() < 6 &&
               ((value[i] >= u'0' && value[i] <= u'9') ||
                (value[i].toLower() >= u'a' && value[i].toLower() <= u'f'))) digits += value[i++];
        if (digits.isEmpty()) result += value[i];
        else {
            bool ok = false;
            const char32_t code = digits.toUInt(&ok, 16);
            CHECK(ok && code > 0 && code <= 0x10ffff);
            result += QString::fromUcs4(&code, 1);
            if (!value[i].isSpace()) --i;
        }
    }
    return result;
}
static qreal htmlNumber(const QString &text) {
    bool ok = false;
    const qreal value = text.toDouble(&ok);
    CHECK(ok && std::isfinite(value));
    return value;
}
static qreal htmlPixels(const QMap<QString, QString> &style, const QString &property) {
    const QString value = style.value(property);
    CHECK(value.endsWith(QStringLiteral("px")));
    return htmlNumber(value.chopped(2));
}
static QRectF htmlRect(const HtmlElement &element) {
    const auto style = htmlStyle(element);
    return QRectF(htmlPixels(style, QStringLiteral("left")), htmlPixels(style, QStringLiteral("top")),
                  htmlPixels(style, QStringLiteral("width")), htmlPixels(style, QStringLiteral("height")));
}
static void nearHtml(qreal actual, qreal expected) {
    if (std::abs(actual - expected) > 0.02)
        std::cerr << "HTML geometry: actual=" << actual << ", expected=" << expected << '\n';
    CHECK(std::abs(actual - expected) <= 0.02);
}
static void nearHtml(QPointF actual, QPointF expected) {
    nearHtml(actual.x(), expected.x()); nearHtml(actual.y(), expected.y());
}
static void nearHtml(QRectF actual, QRectF expected) {
    nearHtml(actual.topLeft(), expected.topLeft());
    nearHtml(actual.width(), expected.width()); nearHtml(actual.height(), expected.height());
}
static std::vector<qreal> htmlNumbers(const QString &value) {
    const auto tokens = value.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    std::vector<qreal> result;
    for (const auto &token : tokens) result.push_back(htmlNumber(token));
    return result;
}
static QPainterPath htmlPath(const QString &value) {
    QString separated = value;
    separated.replace(QRegularExpression(QStringLiteral("([MLCZ])")), QStringLiteral(" \\1 "));
    const auto tokens = separated.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
    QPainterPath path;
    qsizetype i = 0;
    auto point = [&] {
        CHECK(i + 1 < tokens.size());
        const qreal x = htmlNumber(tokens[i++]), y = htmlNumber(tokens[i++]);
        return QPointF(x, y);
    };
    while (i < tokens.size()) {
        const QString command = tokens[i++];
        if (command == QStringLiteral("M")) path.moveTo(point());
        else if (command == QStringLiteral("L")) path.lineTo(point());
        else if (command == QStringLiteral("C")) {
            const QPointF first = point(), second = point(), end = point();
            path.cubicTo(first, second, end);
        } else { CHECK(command == QStringLiteral("Z")); path.closeSubpath(); }
    }
    CHECK(!path.isEmpty());
    return path;
}
static void nearHtml(const QPainterPath &actual, const QPainterPath &expected) {
    CHECK(actual.elementCount() == expected.elementCount());
    for (int i = 0; i < actual.elementCount(); ++i) {
        const auto a = actual.elementAt(i), b = expected.elementAt(i);
        CHECK(a.type == b.type);
        nearHtml(QPointF(a.x, a.y), QPointF(b.x, b.y));
    }
}
static void checkHtmlText(const HtmlPage &page, int index, QGraphicsTextItem &item) {
    const auto &element = page.elements[index];
    CHECK(element.text == item.toPlainText());
    const auto style = htmlStyle(element);
    const QFontInfo font(item.document()->defaultFont());
    CHECK(htmlCssString(style.value(QStringLiteral("font-family"))) == font.family());
    nearHtml(htmlPixels(style, QStringLiteral("font-size")), font.pixelSize());
    const qreal cssWeight = htmlNumber(style.value(QStringLiteral("font-weight")));
    CHECK(cssWeight >= 100 && cssWeight <= 900 && (cssWeight >= 600) == font.bold());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    nearHtml(cssWeight, font.weight());
#endif
    CHECK(style.value(QStringLiteral("font-style")) == (font.italic() ? QStringLiteral("italic") : QStringLiteral("normal")));
    CHECK(QColor(style.value(QStringLiteral("color"))) == item.defaultTextColor());
    const auto lines = page.classes(QStringLiteral("m3-text-line"), index);
    size_t indexInLines = 0;
    for (auto block = item.document()->begin(); block.isValid(); block = block.next()) {
        const auto *layout = block.layout();
        for (int i = 0; i < layout->lineCount(); ++i) {
            CHECK(indexInLines < lines.size());
            const auto line = layout->lineAt(i);
            const auto &actual = page.elements[lines[indexInLines++]];
            nearHtml(htmlRect(actual), QRectF(layout->position() + line.naturalTextRect().topLeft(),
                                              QSizeF(line.naturalTextWidth(), line.height())));
            QString text = block.text().mid(line.textStart(), line.textLength());
            if (i + 1 == layout->lineCount() && block.next().isValid()) text += u'\n';
            CHECK(actual.text == text);
        }
    }
    CHECK(indexInLines == lines.size());
}
// Record the scene item's actual vector paint primitives, without rasterizing or reimplementing routing.
struct HtmlPaintEngine final : QPaintEngine {
    struct Stroke { QPainterPath path; QPen pen; QBrush brush; };
    std::vector<Stroke> strokes;
    std::vector<QPolygonF> arrows;
    std::vector<QColor> fills;
    QPen pen;
    QBrush brush;
    HtmlPaintEngine() : QPaintEngine(QPaintEngine::AllFeatures) {}
    bool begin(QPaintDevice *) override { setActive(true); return true; }
    bool end() override { setActive(false); return true; }
    Type type() const override { return QPaintEngine::User; }
    void updateState(const QPaintEngineState &updated) override {
        if (updated.state() & DirtyPen) pen = updated.pen();
        if (updated.state() & DirtyBrush) brush = updated.brush();
    }
    void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override { CHECK(false); }
    void drawPath(const QPainterPath &path) override { strokes.push_back({path, pen, brush}); }
    void drawPolygon(const QPointF *points, int count, PolygonDrawMode) override {
        QPolygonF polygon;
        for (int i = 0; i < count; ++i) polygon.append(points[i]);
        arrows.push_back(std::move(polygon)); fills.push_back(brush.color());
    }
};
struct HtmlPaintDevice final : QPaintDevice {
    mutable HtmlPaintEngine engine;
    QPaintEngine *paintEngine() const override { return &engine; }
    int metric(PaintDeviceMetric metric) const override {
        switch (metric) {
        case PdmWidth: case PdmHeight: return 4096;
        case PdmWidthMM: case PdmHeightMM: return 1084;
        case PdmDepth: return 32;
        case PdmNumColors: return 0;
        case PdmDevicePixelRatio: return 1;
        case PdmDevicePixelRatioScaled: return 65536;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        case PdmDevicePixelRatioF_EncodedA: case PdmDevicePixelRatioF_EncodedB:
            return QPaintDevice::encodeMetricF(metric, 1.0);
#endif
        default: return 96;
        }
    }
};
static void checkHtmlPen(const HtmlElement &element, const QPen &pen) {
    const auto &attributes = element.attributes;
    CHECK(QColor(attributes.value(QStringLiteral("stroke"))) == pen.color());
    nearHtml(htmlNumber(attributes.value(QStringLiteral("stroke-width"))), pen.widthF());
    CHECK(attributes.value(QStringLiteral("stroke-linecap")) ==
          (pen.capStyle() == Qt::RoundCap ? QStringLiteral("round") :
           pen.capStyle() == Qt::FlatCap ? QStringLiteral("butt") : QStringLiteral("square")));
    CHECK(attributes.value(QStringLiteral("stroke-linejoin")) ==
          (pen.joinStyle() == Qt::RoundJoin ? QStringLiteral("round") :
           pen.joinStyle() == Qt::BevelJoin ? QStringLiteral("bevel") : QStringLiteral("miter")));
    const QString dash = attributes.value(QStringLiteral("stroke-dasharray"));
    if (pen.style() == Qt::SolidLine) CHECK(dash.isEmpty() || dash == QStringLiteral("none"));
    else {
        const auto actual = htmlNumbers(dash);
        const auto expected = pen.dashPattern();
        CHECK(actual.size() == static_cast<size_t>(expected.size()));
        for (size_t i = 0; i < actual.size(); ++i) nearHtml(actual[i], expected[static_cast<qsizetype>(i)] * pen.widthF());
        const QString offset = attributes.value(QStringLiteral("stroke-dashoffset"));
        nearHtml(offset.isEmpty() ? 0 : htmlNumber(offset), pen.dashOffset() * pen.widthF());
    }
}
static void sameHtmlMap(const HtmlPage &actual, const HtmlPage &expected) {
    nearHtml(actual.size().width(), expected.size().width());
    nearHtml(actual.size().height(), expected.size().height());
    for (const QString &name : {QStringLiteral("m3-node"), QStringLiteral("m3-topic"), QStringLiteral("m3-icons"),
                              QStringLiteral("m3-tag"), QStringLiteral("m3-link-label"), QStringLiteral("m3-node-image"),
                              QStringLiteral("m3-image-placeholder"), QStringLiteral("m3-tree-edge"),
                              QStringLiteral("m3-cross-link"), QStringLiteral("m3-link-curve"),
                              QStringLiteral("m3-link-leader"), QStringLiteral("m3-link-arrow"),
                              QStringLiteral("m3-text-line"), QStringLiteral("m3-tag-text"),
                              QStringLiteral("m3-node-link"), QStringLiteral("m3-fold-indicator"),
                              QStringLiteral("m3-image-message")}) {
        const auto a = actual.classes(name), b = expected.classes(name);
        CHECK(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            const auto &left = actual.elements[a[i]], &right = expected.elements[b[i]];
            CHECK(left.tag == right.tag && left.text == right.text);
            for (const QString &key : {QStringLiteral("data-node-id"), QStringLiteral("data-link-id"),
                                      QStringLiteral("data-source"), QStringLiteral("data-target"),
                                      QStringLiteral("data-end"), QStringLiteral("data-image-state"), QStringLiteral("href"),
                                      QStringLiteral("stroke"), QStringLiteral("fill"),
                                      QStringLiteral("stroke-linecap"), QStringLiteral("stroke-linejoin")})
                CHECK(left.attributes.value(key) == right.attributes.value(key));
            if (left.attributes.contains(QStringLiteral("d")))
                nearHtml(htmlPath(left.attributes.value(QStringLiteral("d"))), htmlPath(right.attributes.value(QStringLiteral("d"))));
            if (left.attributes.contains(QStringLiteral("points"))) {
                const auto x = htmlNumbers(left.attributes.value(QStringLiteral("points"))),
                           y = htmlNumbers(right.attributes.value(QStringLiteral("points")));
                CHECK(x.size() == y.size());
                for (size_t j = 0; j < x.size(); ++j) nearHtml(x[j], y[j]);
            }
            const auto leftStyle = htmlStyle(left), rightStyle = htmlStyle(right);
            CHECK(leftStyle.keys() == rightStyle.keys());
            for (auto it = leftStyle.begin(); it != leftStyle.end(); ++it) {
                const QString other = rightStyle.value(it.key());
                if (it.value().endsWith(QStringLiteral("px"))) nearHtml(htmlNumber(it.value().chopped(2)), htmlNumber(other.chopped(2)));
                else if (it.key() == QStringLiteral("font-family")) CHECK(htmlCssString(it.value()) == htmlCssString(other));
                else if (it.key() == QStringLiteral("font-weight")) nearHtml(htmlNumber(it.value()), htmlNumber(other));
                else CHECK(it.value().simplified() == other.simplified());
            }
            for (const QString &key : {QStringLiteral("stroke-width"), QStringLiteral("stroke-dashoffset"), QStringLiteral("stroke-dasharray")}) {
                const auto x = htmlNumbers(left.attributes.value(key)), y = htmlNumbers(right.attributes.value(key));
                CHECK(x.size() == y.size());
                for (size_t j = 0; j < x.size(); ++j) nearHtml(x[j], y[j]);
            }
            if (name == QStringLiteral("m3-node-image")) CHECK(actual.nodeImage(a[i]) == expected.nodeImage(b[i]));
        }
    }
}
static Json imageDocument(const QString &url = QStringLiteral("memory:picture"), double width = 0, double height = 0) {
    Json input = editorFixture();
    setTopic(input, "r", QStringLiteral("Image root"));
    setTopic(input, "c", QStringLiteral("Gamma"));
    for (auto &node : input.at("nodes")) if (node.at("id") == "r") {
        node["tags"] = Json::array({"one", "two"});
        node["image"] = Json{{"url", utf8(url)}, {"width", width}, {"height", height}};
    }
    return input;
}
static QImage imagePixels(QColor left = QColor(231, 37, 53), QColor right = QColor(29, 71, 223),
                          QSize size = QSize(120, 60)) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(left);
    QPainter painter(&image);
    painter.fillRect(QRect(size.width() / 2, 0, size.width() - size.width() / 2, size.height()), right);
    return image;
}
static QGraphicsItem *nodeImageItem(Editor &editor, const QString &topic, const QString &url) {
    for (auto *child : ownerItem(textItem(editor, topic))->childItems())
        if (child->isVisible() && !dynamic_cast<QGraphicsTextItem *>(child) &&
            child->cursor().shape() != Qt::PointingHandCursor &&
            child->toolTip().contains(url.toHtmlEscaped()) && !url.isEmpty()) return child;
    return nullptr;
}
static QRectF nodeImageRect(Editor &editor, const QString &topic = QStringLiteral("Image root"),
                             const QString &url = QStringLiteral("memory:picture")) {
    auto *item = nodeImageItem(editor, topic, url);
    CHECK(item != nullptr);
    return item->sceneBoundingRect();
}
static bool imageHasColors(Editor &editor, const QString &topic, const QString &url,
                           QColor left = QColor(231, 37, 53), QColor right = QColor(29, 71, 223)) {
    auto *item = nodeImageItem(editor, topic, url);
    if (!item) return false;
    const QRectF rect = item->sceneBoundingRect();
    const QImage painted = paintScene(editor, rect);
    auto matches = [&](qreal fraction, QColor color) {
        const QPoint point = imagePoint(painted, rect, QPointF(rect.left() + fraction * rect.width(), rect.center().y()));
        if (!painted.rect().contains(point)) return false;
        const QColor actual = painted.pixelColor(point);
        return std::abs(actual.red() - color.red()) <= 2 && std::abs(actual.green() - color.green()) <= 2 &&
               std::abs(actual.blue() - color.blue()) <= 2;
    };
    return matches(0.25, left) && matches(0.75, right);
}
static void checkImageSize(const QRectF &rect, QSizeF size, qreal tolerance = 0.01) {
    CHECK(finiteRect(rect));
    CHECK(std::abs(rect.width() - size.width()) <= tolerance);
    CHECK(std::abs(rect.height() - size.height()) <= tolerance);
}
static void checkImageLayout(Editor &editor, QSizeF size, const QString &url = QStringLiteral("memory:picture")) {
    const QRectF image = nodeImageRect(editor, QStringLiteral("Image root"), url);
    checkImageSize(image, size);
    CHECK(topicRect(editor, QStringLiteral("Image root")).contains(image));
    CHECK(image.top() > textItem(editor, QStringLiteral("one"))->sceneBoundingRect().bottom());
    CHECK(image.top() > textItem(editor, QStringLiteral("two"))->sceneBoundingRect().bottom());
    std::vector<QRectF> nodes;
    for (const QString &topic : {QStringLiteral("Image root"), QStringLiteral("Alpha"), QStringLiteral("Beta"),
                                 QStringLiteral("Gamma"), QStringLiteral("Delta")}) {
        const QRectF node = topicRect(editor, topic);
        for (const auto &other : nodes) CHECK(!node.intersects(other));
        nodes.push_back(node);
    }
}
static QGraphicsItem *imageHandle(Editor &editor, const QString &topic = QStringLiteral("Image root")) {
    auto *owner = ownerItem(textItem(editor, topic));
    for (auto *item : graphics(editor).scene()->items())
        if (item->isVisible() && item->cursor().shape() == Qt::SizeFDiagCursor && belongsTo(item, owner)) return item;
    return nullptr;
}
static QPoint imageHandlePoint(Editor &editor, const QString &topic = QStringLiteral("Image root")) {
    auto &view = graphics(editor);
    auto *handle = imageHandle(editor, topic);
    CHECK(handle != nullptr);
    view.ensureVisible(handle, 30, 30);
    pump();
    handle = imageHandle(editor, topic);
    CHECK(handle != nullptr);
    const QPoint point = handle->deviceTransform(view.viewportTransform()).map(handle->boundingRect().center()).toPoint();
    CHECK(view.viewport()->rect().contains(point) && belongsTo(view.itemAt(point), handle));
    return point;
}
static QPoint startImageResize(Editor &editor, QPointF sceneDelta,
                                const QString &topic = QStringLiteral("Image root")) {
    auto &view = graphics(editor);
    const QPoint from = imageHandlePoint(editor, topic);
    const QPoint to = view.mapFromScene(view.mapToScene(from) + sceneDelta);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
    movePointer(view, to, Qt::LeftButton);
    return to;
}
static void releaseImageResize(Editor &editor, QPoint point) {
    QTest::mouseRelease(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, point);
    pump();
}
static QPoint nodeLinkPoint(Editor &editor, const QString &topic) {
    auto &view = graphics(editor);
    for (auto *child : ownerItem(textItem(editor, topic))->childItems()) {
        if (!child->isVisible() || child->cursor().shape() != Qt::PointingHandCursor) continue;
        view.ensureVisible(child);
        pump();
        const QPoint point = view.mapFromScene(child->sceneBoundingRect().center());
        CHECK(view.viewport()->rect().contains(point) && belongsTo(view.itemAt(point), child));
        return point;
    }
    throw std::runtime_error("Missing reachable node link");
}

static void htmlAppearance(Editor &editor) {
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(16);
    editor.setFont(font);
    QPalette palette;
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::Text, QColor(20, 25, 30));
    palette.setColor(QPalette::Button, QColor(234, 240, 248));
    palette.setColor(QPalette::ButtonText, QColor(20, 25, 30));
    palette.setColor(QPalette::Mid, QColor(100, 110, 125));
    palette.setColor(QPalette::Dark, QColor(55, 65, 80));
    editor.setPalette(palette);
}
static void compareHtmlScene(Editor &editor, const HtmlPage &page) {
    auto *scene = graphics(editor).scene();
    const QRectF bounds = scene->itemsBoundingRect().adjusted(-32, -32, 32, 32);
    const QPointF offset = -bounds.topLeft();
    nearHtml(page.size().width(), bounds.width()); nearHtml(page.size().height(), bounds.height());
    const int map = page.identified(QStringLiteral("id"), QStringLiteral("mindmap"));
    nearHtml(htmlRect(page.elements[map]), QRectF(QPointF(), bounds.size()));
    const auto svg = page.classes(QStringLiteral("m3-connectors"));
    CHECK(svg.size() == 1 && page.elements[svg.front()].tag == QStringLiteral("svg"));
    const auto viewBox = htmlNumbers(page.elements[svg.front()].attributes.value(QStringLiteral("viewBox")));
    CHECK(viewBox.size() == 4);
    nearHtml(viewBox[0], 0); nearHtml(viewBox[1], 0);
    nearHtml(viewBox[2], bounds.width()); nearHtml(viewBox[3], bounds.height());
    const Json document = exported(editor);
    std::set<QString> visible;
    for (const int index : page.classes(QStringLiteral("m3-node"))) {
        const auto &node = page.elements[index];
        const QString id = node.attributes.value(QStringLiteral("data-node-id"));
        CHECK(visible.insert(id).second);
        CHECK(node.attributes.value(QStringLiteral("id")) == QStringLiteral("m3-map-node-") + QString::fromLatin1(id.toUtf8().toHex()));
        const QString topic = qs(record(document, "nodes", id).at("topic"));
        const auto topics = page.classes(QStringLiteral("m3-topic"), index);
        CHECK(topics.size() == 1 && page.elements[topics.front()].text == topic);
        auto *label = textItem(editor, topic);
        auto *owner = ownerItem(label);
        const QRectF rectangle = owner->mapToScene(owner->shape()).boundingRect();
        nearHtml(htmlRect(node), rectangle.translated(offset));
        CHECK(QRectF(QPointF(), page.size()).contains(htmlRect(node)));
        nearHtml(htmlRect(page.elements[topics.front()]), label->sceneBoundingRect().translated(-rectangle.topLeft()));
        checkHtmlText(page, topics.front(), *label);
        HtmlPaintDevice nodePaint;
        QPainter nodePainter(&nodePaint);
        owner->paint(&nodePainter, nullptr, nullptr);
        CHECK(nodePainter.end() && !nodePaint.engine.strokes.empty());
        const auto appearance = htmlStyle(node);
        CHECK(QColor(appearance.value(QStringLiteral("background"))) == nodePaint.engine.strokes.front().brush.color());
        const auto outline = appearance.value(QStringLiteral("outline")).split(u' ', Qt::SkipEmptyParts);
        CHECK(outline.size() == 3 && outline[1] == QStringLiteral("solid"));
        CHECK(QColor(outline[2]) == nodePaint.engine.strokes.front().pen.color());
        const auto images = page.classes(QStringLiteral("m3-node-image"), index);
        if (!images.empty()) {
            CHECK(images.size() == 1);
            const QString url = qs(record(document, "nodes", id).at("image").at("url"));
            nearHtml(htmlRect(page.elements[images.front()]), nodeImageRect(editor, topic, url).translated(-rectangle.topLeft()));
            CHECK(!page.elements[images.front()].attributes.contains(QStringLiteral("data-image-state")));
            page.nodeImage(images.front());
        }
    }
    std::vector<QGraphicsPathItem *> treePaths;
    for (auto *item : scene->items()) if (auto *path = dynamic_cast<QGraphicsPathItem *>(item)) treePaths.push_back(path);
    const auto edges = page.classes(QStringLiteral("m3-tree-edge"));
    CHECK(edges.size() == treePaths.size());
    std::set<QGraphicsPathItem *> matched;
    for (const int index : edges) {
        const auto &edge = page.elements[index];
        const QString source = edge.attributes.value(QStringLiteral("data-source")), target = edge.attributes.value(QStringLiteral("data-target"));
        CHECK(visible.count(source) == 1 && visible.count(target) == 1);
        const auto &children = record(document, "nodes", source).at("children");
        CHECK(std::find(children.begin(), children.end(), utf8(target)) != children.end());
        const QPainterPath actual = htmlPath(edge.attributes.value(QStringLiteral("d")));
        QGraphicsPathItem *match = nullptr;
        for (auto *path : treePaths) {
            const auto expected = path->mapToScene(path->path()).translated(offset);
            if (QLineF(actual.pointAtPercent(0), expected.pointAtPercent(0)).length() < 0.02 &&
                QLineF(actual.pointAtPercent(1), expected.pointAtPercent(1)).length() < 0.02) {
                CHECK(!match); match = path;
            }
        }
        CHECK(match && matched.insert(match).second);
        nearHtml(actual, match->mapToScene(match->path()).translated(offset));
        checkHtmlPen(edge, match->pen());
    }
    size_t visibleLinks = 0;
    for (const auto &link : document.at("crossLinks"))
        if (visible.count(qs(link.at("source"))) && visible.count(qs(link.at("target")))) ++visibleLinks;
    const auto links = page.classes(QStringLiteral("m3-cross-link"));
    CHECK(links.size() == visibleLinks);
    for (const int index : links) {
        const auto &group = page.elements[index];
        const QString id = group.attributes.value(QStringLiteral("data-link-id"));
        const auto &link = record(document, "crossLinks", id);
        CHECK(group.attributes.value(QStringLiteral("data-source")) == qs(link.at("source")));
        CHECK(group.attributes.value(QStringLiteral("data-target")) == qs(link.at("target")));
        const auto labels = page.classes(QStringLiteral("m3-link-label"));
        int labelIndex = -1;
        for (const int candidate : labels)
            if (page.elements[candidate].attributes.value(QStringLiteral("data-link-id")) == id) labelIndex = candidate;
        CHECK(labelIndex >= 0);
        auto *label = textItem(editor, page.elements[labelIndex].text);
        auto *owner = ownerItem(label);
        nearHtml(htmlRect(page.elements[labelIndex]), label->sceneBoundingRect().translated(offset));
        checkHtmlText(page, labelIndex, *label);
        HtmlPaintDevice device;
        QPainter painter(&device);
        owner->paint(&painter, nullptr, nullptr);
        CHECK(painter.end());
        const auto curves = page.classes(QStringLiteral("m3-link-curve"), index),
                   leaders = page.classes(QStringLiteral("m3-link-leader"), index),
                   arrows = page.classes(QStringLiteral("m3-link-arrow"), index);
        CHECK(curves.size() == 1 && leaders.size() <= 1);
        CHECK(device.engine.strokes.size() == 1 + leaders.size());
        const auto checkStroke = [&](int element, size_t stroke) {
            nearHtml(htmlPath(page.elements[element].attributes.value(QStringLiteral("d"))),
                     owner->mapToScene(device.engine.strokes[stroke].path).translated(offset));
            checkHtmlPen(page.elements[element], device.engine.strokes[stroke].pen);
        };
        checkStroke(curves.front(), 0);
        if (!leaders.empty()) checkStroke(leaders.front(), 1);
        CHECK(arrows.size() == device.engine.arrows.size());
        const std::string direction = link.at("style").value("arrowDirection", std::string("forward"));
        const bool directed = link.at("directed").get<bool>();
        const bool sourceArrow = directed && (direction == "backward" || direction == "both");
        const bool targetArrow = directed && direction != "backward";
        CHECK(arrows.size() == static_cast<size_t>(sourceArrow) + static_cast<size_t>(targetArrow));
        QStringList ends;
        for (const int arrow : arrows) ends.append(page.elements[arrow].attributes.value(QStringLiteral("data-end")));
        CHECK(ends.count(QStringLiteral("source")) == (sourceArrow ? 1 : 0));
        CHECK(ends.count(QStringLiteral("target")) == (targetArrow ? 1 : 0));
        const auto curve = htmlPath(page.elements[curves.front()].attributes.value(QStringLiteral("d")));
        for (size_t i = 0; i < arrows.size(); ++i) {
            const auto &arrow = page.elements[arrows[i]];
            const auto points = htmlNumbers(arrow.attributes.value(QStringLiteral("points")));
            const auto expected = owner->mapToScene(device.engine.arrows[i]).translated(offset);
            CHECK(points.size() == static_cast<size_t>(expected.size()) * 2);
            for (qsizetype j = 0; j < expected.size(); ++j) nearHtml(QPointF(points[2 * j], points[2 * j + 1]), expected[j]);
            CHECK(QColor(arrow.attributes.value(QStringLiteral("fill"))) == device.engine.fills[i]);
            const QString end = arrow.attributes.value(QStringLiteral("data-end"));
            CHECK(end == QStringLiteral("source") || end == QStringLiteral("target"));
            nearHtml(QPointF(points[0], points[1]), curve.pointAtPercent(end == QStringLiteral("source") ? 0 : 1));
        }
    }
}
static void html_case() {
    QString retained;
    const QString root = QString::fromUtf8("Literal <script>世界</script>");
    {
        RawLabelEditor editor;
        htmlAppearance(editor);
        Json input = editorFixture();
        setTopic(input, "r", root);
        setTopic(input, "c", QStringLiteral("Gamma"));
        for (auto &node : input.at("nodes")) {
            if (node.at("id") == "r") {
                node["tags"] = Json::array({"x", "", "x"});
                node["icons"] = Json::array({"star", "", "star"});
                node["note"] = "First <b>café</b> line\r\nsecond line\r\n\r\n**Plain text**, not Markdown";
                node["hyperLink"] = "https://example.com/?a=1&b=%22quoted%22";
                node["style"] = {{"fontSize", 23}, {"fontWeight", "bold"}, {"fontStyle", "italic"},
                                 {"color", "#123456"}, {"background", "#cde8fa"}, {"branchColor", "#654321"}};
            }
            if (node.at("id") == "b") {
                node["tags"] = Json::array({"", " \t"});
                node["icons"] = Json::array({"", " \t"});
                node["note"] = "";
                node["hyperLink"] = "";
            }
        }
        for (auto &link : input.at("crossLinks")) {
            if (link.at("id") == "l3") link["topic"] = "Self";
            if (link.at("id") == "l4") { link["source"] = "a"; link["target"] = "b"; link["topic"] = "Parallel"; }
        }
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        showEditor(editor, QSize(500, 400));
        editor.clearSelection();
        QTest::mouseMove(graphics(editor).viewport(), QPoint(1, 1));
        pump();
        retained = editor.toHtml();
        const HtmlPage page(retained);
        compareHtmlScene(editor, page);
        checkHtmlDocument(page);
        CHECK(page.classes(QStringLiteral("m3-node")).size() == 4);
        const auto topic = page.classes(QStringLiteral("m3-topic"), page.node(QStringLiteral("r")));
        CHECK(topic.size() == 1 && page.elements[topic.front()].text == root);
        const auto style = htmlStyle(page.elements[topic.front()]);
        nearHtml(htmlPixels(style, QStringLiteral("font-size")), 23);
        CHECK(htmlNumber(style.value(QStringLiteral("font-weight"))) == 700);
        CHECK(style.value(QStringLiteral("font-style")) == QStringLiteral("italic"));
        CHECK(QColor(style.value(QStringLiteral("color"))) == textItem(editor, root)->defaultTextColor());
        CHECK(QColor(htmlStyle(page.elements[page.node(QStringLiteral("r"))]).value(QStringLiteral("background"))) == QColor("#cde8fa"));
        const auto visibleTags = page.classes(QStringLiteral("m3-tag"), page.node(QStringLiteral("r")));
        CHECK(visibleTags.size() == 2);
        for (const int tag : visibleTags) CHECK(page.elements[tag].text.simplified() == QStringLiteral("x"));
        CHECK(!page.classes(QStringLiteral("m3-icons"), page.node(QStringLiteral("r"))).empty());
        CHECK(!page.classes(QStringLiteral("m3-fold-indicator"), page.node(QStringLiteral("a"))).empty());
        CHECK(page.values(QStringLiteral("title")) == QStringList{root});
        CHECK(page.articleIds() == QStringList({"m3-node-72", "m3-node-61", "m3-node-64", "m3-node-62", "m3-node-63"}));
        CHECK(page.classValues(QStringLiteral("m3-article-topic")).contains(QStringLiteral("Delta")));
        checkHtmlTitle(page, QStringLiteral("r"), QStringLiteral("h1"), root);
        checkHtmlTitle(page, QStringLiteral("a"), QStringLiteral("h2"), QStringLiteral("Alpha"));
        checkHtmlTitle(page, QStringLiteral("d"), QStringLiteral("h3"), QStringLiteral("Delta"));
        checkHtmlTitle(page, QStringLiteral("b"), QStringLiteral("h2"), QStringLiteral("Beta"));
        checkHtmlTitle(page, QStringLiteral("c"), QStringLiteral("h2"), QStringLiteral("Gamma"));
        CHECK(page.tags(QStringLiteral("ul")).empty() && page.tags(QStringLiteral("li")).empty());
        const int rootArticle = page.article(QStringLiteral("r"));
        CHECK(page.classValues(QStringLiteral("m3-article-topic"), rootArticle) == QStringList{root});
        CHECK(page.classValues(QStringLiteral("m3-article-tag"), rootArticle) == QStringList({"x", "x"}));
        const auto icons = page.classValues(QStringLiteral("m3-article-icon"), rootArticle);
        CHECK(icons.size() == 2 && !icons.front().trimmed().isEmpty() && icons.front() == icons.back());
        const auto notes = page.classes(QStringLiteral("m3-article-note"), rootArticle);
        CHECK(notes.size() == 1);
        const auto paragraphs = page.tags(QStringLiteral("p"), notes.front());
        CHECK(paragraphs.size() == 2);
        CHECK(page.elements[paragraphs[0]].text == QString::fromUtf8("First <b>café</b> line\nsecond line"));
        CHECK(page.elements[paragraphs[1]].text == QStringLiteral("**Plain text**, not Markdown"));
        CHECK(page.tags(QStringLiteral("br"), notes.front()).size() == 1);
        CHECK(page.tags(QStringLiteral("b"), notes.front()).empty());
        const auto urls = page.classes(QStringLiteral("m3-article-url"), rootArticle);
        CHECK(urls.size() == 1);
        const auto hyperlinks = page.tags(QStringLiteral("a"), urls.front());
        CHECK(hyperlinks.size() == 1);
        CHECK(page.elements[hyperlinks.front()].attributes.value(QStringLiteral("href")) ==
              QStringLiteral("https://example.com/?a=1&b=%22quoted%22"));
        checkHtmlRows(page, QStringLiteral("r"), {QStringLiteral("m3-article-title"), QStringLiteral("m3-article-icons"),
            QStringLiteral("m3-article-tags"), QStringLiteral("m3-article-url"), QStringLiteral("m3-article-note"),
            QStringLiteral("m3-article-figure")});
        checkHtmlRows(page, QStringLiteral("a"), {QStringLiteral("m3-article-title"), QStringLiteral("m3-article-references")});
        checkHtmlRows(page, QStringLiteral("b"), {QStringLiteral("m3-article-title")});
        const int bareArticle = page.article(QStringLiteral("b"));
        CHECK(page.elements[bareArticle].text.trimmed() == QStringLiteral("Beta"));
        for (const QString &name : {QStringLiteral("m3-article-icons"), QStringLiteral("m3-article-tags"),
                                   QStringLiteral("m3-article-note"), QStringLiteral("m3-article-url"),
                                   QStringLiteral("m3-article-figure")}) CHECK(page.classes(name, bareArticle).empty());
        checkHtmlReferences(page, QStringLiteral("r"), {});
        checkHtmlReferences(page, QStringLiteral("a"), {{"b", "Related"}, {"a", "Self"}, {"b", "Parallel"}});
        checkHtmlReferences(page, QStringLiteral("b"), {});
        checkHtmlReferences(page, QStringLiteral("c"), {{"d", "Other"}});
        checkHtmlReferences(page, QStringLiteral("d"), {{"c", "Other"}});
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
        const QRectF bounds = graphics(editor).scene()->itemsBoundingRect().adjusted(-32, -32, 32, 32);
        for (const QString &label : {QStringLiteral("Related"), QStringLiteral("Self"), QStringLiteral("Parallel")}) {
            bool present = false;
            for (const int index : page.classes(QStringLiteral("m3-link-label"))) present |= page.elements[index].text == label;
            CHECK(present);
        }
        CHECK(page.size().width() > graphics(editor).viewport()->width());
        const QByteArray baseline = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        auto unchangedExport = [&] {
            auto &view = graphics(editor);
            auto *scene = view.scene();
            const auto transform = view.transform();
            const QRectF sceneRect = view.sceneRect();
            const int horizontal = view.horizontalScrollBar()->value(), vertical = view.verticalScrollBar()->value();
            const QString node = editor.selectedNodeId(), link = editor.selectedLinkId();
            const auto selections = selected.size();
            const HtmlPage snapshot(editor.toHtml());
            sameHtmlMap(snapshot, page);
            checkHtmlDocument(snapshot);
            CHECK(snapshot.values(QStringLiteral("article")) == page.values(QStringLiteral("article")));
            CHECK(view.scene() == scene && view.transform() == transform && view.sceneRect() == sceneRect);
            CHECK(view.horizontalScrollBar()->value() == horizontal && view.verticalScrollBar()->value() == vertical);
            CHECK(editor.selectedNodeId() == node && editor.selectedLinkId() == link);
            CHECK(editor.toJson() == baseline && changed.isEmpty() && selected.size() == selections);
        };
        CHECK(editor.selectNode(QStringLiteral("b")));
        graphics(editor).scale(2, 2);
        graphics(editor).setSceneRect(bounds.adjusted(-800, -800, 800, 800));
        graphics(editor).horizontalScrollBar()->setValue(150);
        graphics(editor).verticalScrollBar()->setValue(100);
        unchangedExport();
        CHECK(editor.selectLink(QStringLiteral("l1")));
        unchangedExport();
        CHECK(editor.selectNode(QStringLiteral("r")));
        shortcut(editor, Qt::Key_F2);
        auto *draft = &topicInput(editor);
        draft->setPlainText(QStringLiteral("UNCOMMITTED DRAFT"));
        unchangedExport();
        sameHtmlMap(HtmlPage(editor.toHtml()), page);
        CHECK(activeTopicInput(editor) == draft && draft->hasFocus());
        CHECK(draft->toPlainText() == QStringLiteral("UNCOMMITTED DRAFT"));
        topicKey(editor, Qt::Key_Escape);
        CHECK(editor.setExpanded(QStringLiteral("a"), true));
        CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
        const HtmlPage expanded(editor.toHtml());
        CHECK(expanded.classes(QStringLiteral("m3-node")).size() == 5);
        CHECK(expanded.elements[expanded.classes(QStringLiteral("m3-topic"), expanded.node(QStringLiteral("d"))).front()].text == QStringLiteral("Delta"));
        CHECK(editor.renameNode(QStringLiteral("r"), QStringLiteral("Later edit")));
    }
    const HtmlPage saved(retained);
    CHECK(saved.values(QStringLiteral("title")) == QStringList{root});
    CHECK(saved.classValues(QStringLiteral("m3-article-topic"), saved.article(QStringLiteral("r"))) == QStringList{root});
    {
        RawLabelEditor editor;
        htmlAppearance(editor);
        const QString topic = QString::fromUtf8("Image <b>世界</b>\n  second\tline");
        Json input = imageDocument();
        input["crossLinks"] = Json::array();
        setTopic(input, "r", topic);
        QSignalSpy requested(&editor, &Editor::imageRequested);
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor);
        editor.clearSelection();
        CHECK(QTest::qWaitFor([&] { return requested.size() == 1; }, 5000));
        const HtmlPage loading(editor.toHtml());
        const auto pending = loading.classes(QStringLiteral("m3-image-placeholder"), loading.node(QStringLiteral("r")));
        CHECK(pending.size() == 1);
        CHECK(loading.elements[pending.front()].attributes.value(QStringLiteral("data-image-state")) == QStringLiteral("loading"));
        CHECK(loading.classes(QStringLiteral("m3-node-image")).empty());
        const auto articlePending = loading.classes(QStringLiteral("m3-article-image-placeholder"), loading.article(QStringLiteral("r")));
        CHECK(articlePending.size() == 1 && loading.classes(QStringLiteral("m3-article-image")).empty());
        CHECK(loading.elements[articlePending.front()].attributes.value(QStringLiteral("data-image-state")) == QStringLiteral("loading"));
        editor.provideImage(requested.front()[0].toString(), requested.front()[1].toULongLong(), QImage());
        pump();
        const HtmlPage failed(editor.toHtml());
        const auto unavailable = failed.classes(QStringLiteral("m3-image-placeholder"), failed.node(QStringLiteral("r")));
        CHECK(unavailable.size() == 1);
        CHECK(failed.elements[unavailable.front()].attributes.value(QStringLiteral("data-image-state")) == QStringLiteral("unavailable"));
        CHECK(failed.elements[unavailable.front()].text.contains(QStringLiteral("Image unavailable")));
        const auto articleFailed = failed.classes(QStringLiteral("m3-article-image-placeholder"), failed.article(QStringLiteral("r")));
        CHECK(articleFailed.size() == 1);
        CHECK(failed.elements[articleFailed.front()].attributes.value(QStringLiteral("data-image-state")) == QStringLiteral("unavailable"));
        CHECK(!failed.elements[articleFailed.front()].text.trimmed().isEmpty());
        requested.clear();
        editor.reloadImages();
        CHECK(QTest::qWaitFor([&] { return requested.size() == 1; }, 5000));
        const QImage pixels = imagePixels();
        editor.provideImage(requested.front()[0].toString(), requested.front()[1].toULongLong(), pixels);
        pump();
        const HtmlPage loaded(editor.toHtml());
        compareHtmlScene(editor, loaded);
        const auto images = loaded.classes(QStringLiteral("m3-node-image"));
        CHECK(images.size() == 1 && loaded.tags(QStringLiteral("img")).size() == 2);
        const auto articleImages = loaded.classes(QStringLiteral("m3-article-image"), loaded.article(QStringLiteral("r")));
        CHECK(articleImages.size() == 1 && loaded.classes(QStringLiteral("m3-article-image-placeholder")).empty());
        CHECK(loaded.nodeImage(articleImages.front()).convertToFormat(QImage::Format_RGB32) == pixels);
        CHECK(loaded.elements[articleImages.front()].attributes.value(QStringLiteral("src")) ==
              loaded.elements[images.front()].attributes.value(QStringLiteral("src")));
        CHECK(!loaded.elements[loaded.article(QStringLiteral("r"))].text.contains(QStringLiteral("memory:picture")));
        CHECK(loaded.nodeImage(images.front()).convertToFormat(QImage::Format_RGB32) == pixels);
        CHECK(loaded.classes(QStringLiteral("m3-image-placeholder")).empty());
        const auto topics = loaded.classes(QStringLiteral("m3-topic"), loaded.node(QStringLiteral("r")));
        CHECK(topics.size() == 1 && loaded.elements[topics.front()].text == topic);
        CHECK(loaded.classes(QStringLiteral("m3-text-line"), topics.front()).size() >= 2);
        CHECK(loaded.classes(QStringLiteral("m3-tag"), loaded.node(QStringLiteral("r"))).size() == 2);
    }
    QString retainedImages;
    {
        RawLabelConfig config;
        config.initialVisibleLevels = 2;
        RawLabelEditor editor(config);
        const Json input{{"schemaVersion", 1}, {"rootId", "r"}, {"nodes", Json::array({
            {{"id", "r"}, {"topic", "Visible image"}, {"children", Json::array({"a"})},
             {"image", {{"url", "memory:shared"}, {"width", 48}, {"height", 32}}}},
            {{"id", "a"}, {"topic", "Folded branch"}, {"children", Json::array({"d", "e", "f"})}},
            {{"id", "d"}, {"topic", "Shared hidden image"}, {"image", {{"url", "memory:shared"}, {"width", 48}, {"height", 32}}}},
            {{"id", "e"}, {"topic", "Unique hidden image"}, {"image", {{"url", "memory:unique"}, {"width", 48}, {"height", 32}}}},
            {{"id", "f"}, {"topic", "Failed hidden image"}, {"image", {{"url", "memory:missing"}, {"width", 48}, {"height", 32}}}}
        })}};
        QSignalSpy requested(&editor, &Editor::imageRequested);
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return requested.size() == 1; }, 5000));
        CHECK(requested.front()[0].toString() == QStringLiteral("memory:shared"));
        const QImage shared = imagePixels(), unique = imagePixels(QColor(17, 191, 73), QColor(249, 193, 23));
        editor.provideImage(requested.front()[0].toString(), requested.front()[1].toULongLong(), shared);
        pump();
        const QByteArray baseline = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const HtmlPage deferred(editor.toHtml());
        CHECK(deferred.classes(QStringLiteral("m3-node")).size() == 2);
        CHECK(deferred.classes(QStringLiteral("m3-node-image")).size() == 1);
        CHECK(deferred.classes(QStringLiteral("m3-article-image")).size() == 2);
        const auto cached = deferred.classes(QStringLiteral("m3-article-image"), deferred.article(QStringLiteral("d")));
        CHECK(cached.size() == 1 && deferred.nodeImage(cached.front()).convertToFormat(QImage::Format_RGB32) == shared);
        for (const QString &id : {QStringLiteral("e"), QStringLiteral("f")}) {
            const auto placeholders = deferred.classes(QStringLiteral("m3-article-image-placeholder"), deferred.article(id));
            CHECK(placeholders.size() == 1);
            CHECK(deferred.elements[placeholders.front()].attributes.value(QStringLiteral("data-image-state")) == QStringLiteral("loading"));
        }
        pump();
        CHECK(requested.size() == 1 && editor.toJson() == baseline && changed.isEmpty());
        CHECK(editor.setExpanded(QStringLiteral("a"), true));
        CHECK(QTest::qWaitFor([&] { return requested.size() == 3; }, 5000));
        for (qsizetype i = 1; i < requested.size(); ++i) {
            const QString url = requested[i][0].toString();
            CHECK(url == QStringLiteral("memory:unique") || url == QStringLiteral("memory:missing"));
            editor.provideImage(url, requested[i][1].toULongLong(), url == QStringLiteral("memory:unique") ? unique : QImage());
        }
        pump();
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        editor.clearSelection();
        auto &view = graphics(editor);
        auto *scene = view.scene();
        const auto transform = view.transform();
        const int horizontal = view.horizontalScrollBar()->value(), vertical = view.verticalScrollBar()->value();
        QSignalSpy selected(&editor, &Editor::selectionChanged);
        retainedImages = editor.toHtml();
        const HtmlPage hidden(retainedImages);
        CHECK(hidden.classes(QStringLiteral("m3-node")).size() == 2);
        CHECK(hidden.classes(QStringLiteral("m3-article-image")).size() == 3);
        for (const QString &id : {QStringLiteral("r"), QStringLiteral("d"), QStringLiteral("e")}) {
            const auto images = hidden.classes(QStringLiteral("m3-article-image"), hidden.article(id));
            CHECK(images.size() == 1);
            CHECK(hidden.nodeImage(images.front()).convertToFormat(QImage::Format_RGB32) ==
                  (id == QStringLiteral("e") ? unique : shared));
        }
        const auto failed = hidden.classes(QStringLiteral("m3-article-image-placeholder"), hidden.article(QStringLiteral("f")));
        CHECK(failed.size() == 1 && !hidden.elements[failed.front()].text.trimmed().isEmpty());
        CHECK(hidden.elements[failed.front()].attributes.value(QStringLiteral("data-image-state")) == QStringLiteral("unavailable"));
        checkHtmlDocument(hidden);
        CHECK(view.scene() == scene && view.transform() == transform);
        CHECK(view.horizontalScrollBar()->value() == horizontal && view.verticalScrollBar()->value() == vertical);
        pump();
        CHECK(requested.size() == 3 && changed.isEmpty() && selected.isEmpty() && editor.toJson() == baseline);
    }
    const HtmlPage savedImages(retainedImages);
    const auto savedUnique = savedImages.classes(QStringLiteral("m3-article-image"), savedImages.article(QStringLiteral("e")));
    CHECK(savedUnique.size() == 1);
    CHECK(savedImages.nodeImage(savedUnique.front()).convertToFormat(QImage::Format_RGB32) ==
          imagePixels(QColor(17, 191, 73), QColor(249, 193, 23)));
}

static void html_edges_case() {
    RawLabelConfig config;
    config.initialVisibleLevels = 0;
    RawLabelEditor editor(config);
    htmlAppearance(editor);
    Json linked = editorFixture();
    setTopic(linked, "c", QStringLiteral("Gamma"));
    linked["crossLinks"] = Json::array({
        {{"id", "forward"}, {"source", "a"}, {"target", "b"}, {"topic", "Forward"}, {"directed", true}},
        {{"id", "parallel"}, {"source", "a"}, {"target", "b"}, {"topic", "Parallel"}, {"directed", false}},
        {{"id", "reverse"}, {"source", "b"}, {"target", "a"}, {"topic", "Reverse"}, {"directed", true}},
        {{"id", "backward"}, {"source", "a"}, {"target", "b"}, {"topic", "Backward"}, {"directed", true}, {"style", {{"arrowDirection", "backward"}}}},
        {{"id", "both"}, {"source", "a"}, {"target", "b"}, {"topic", "Both"}, {"directed", true}, {"style", {{"arrowDirection", "both"}}}},
        {{"id", "self"}, {"source", "b"}, {"target", "b"}, {"topic", "Self"}, {"directed", true}},
        {{"id", "self-both"}, {"source", "a"}, {"target", "a"}, {"topic", "Self both"}, {"directed", true}, {"style", {{"arrowDirection", "both"}}}}
    });
    CHECK(editor.loadJson(encoded(linked)));
    showEditor(editor);
    editor.clearSelection();
    const auto hierarchy = HtmlPage(editor.toHtml()).articleIds();
    bool hasLeader = false;
    for (const auto direction : {Editor::LayoutDirection::Balanced, Editor::LayoutDirection::Left,
                                 Editor::LayoutDirection::Right, Editor::LayoutDirection::Outline}) {
        CHECK(editor.setLayoutDirection(direction));
        QTest::mouseMove(graphics(editor).viewport(), QPoint(1, 1));
        pump();
        const HtmlPage page(editor.toHtml());
        compareHtmlScene(editor, page);
        CHECK(page.articleIds() == hierarchy);
        CHECK(page.classes(QStringLiteral("m3-link-arrow")).size() == 8);
        checkHtmlReferences(page, QStringLiteral("a"), {{"b", "Both"}, {"b", "Forward"},
                                                      {"b", "Parallel"}, {"a", "Self both"}});
        checkHtmlReferences(page, QStringLiteral("b"), {{"a", "Backward"}, {"a", "Both"},
                                                      {"a", "Parallel"}, {"a", "Reverse"}, {"b", "Self"}});
        checkHtmlReferences(page, QStringLiteral("r"), {});
        CHECK(page.classes(QStringLiteral("m3-cross-reference")).size() == 9);
        checkHtmlDocument(page);
        hasLeader |= !page.classes(QStringLiteral("m3-link-leader")).empty();
        const QRectF root = topicRect(editor, QString::fromUtf8("Racine 世界 🌍"));
        const QRectF alpha = topicRect(editor, QStringLiteral("Alpha"));
        if (direction == Editor::LayoutDirection::Left) CHECK(alpha.right() < root.left());
        else if (direction == Editor::LayoutDirection::Right) CHECK(alpha.left() > root.right());
        else if (direction == Editor::LayoutDirection::Outline) CHECK(alpha.top() > root.bottom() && alpha.left() > root.left());
    }
    CHECK(hasLeader);
    auto chain = [](int count) {
        Json result{{"schemaVersion", 1}, {"rootId", "n0"}, {"nodes", Json::array()}};
        for (int i = 0; i < count; ++i) {
            Json node{{"id", "n" + std::to_string(i)}, {"topic", "Node " + std::to_string(i)}};
            if (i + 1 < count) node["children"] = Json::array({"n" + std::to_string(i + 1)});
            result["nodes"].push_back(std::move(node));
        }
        return result;
    };
    {
        Json forest = chain(8);
        forest["nodes"][0]["children"].push_back("heading2");
        forest["nodes"][4]["children"].push_back("heading6");
        forest["nodes"][5]["children"].push_back("sibling7");
        forest["nodes"][6]["children"].push_back("sibling8");
        auto &details = forest["nodes"][7];
        details["icons"] = Json::array({"<star>", "", " ", "<star>"});
        details["tags"] = Json::array({"Tag & one", "", "Tag & one"});
        details["hyperLink"] = "https://example.com/?a=1&b=%22eight%22";
        details["note"] = "First <b>literal</b> line\n\nSecond paragraph";
        details["image"] = {{"url", "memory:rows"}, {"width", 120}, {"height", 60}};
        forest["nodes"].push_back({{"id", "sibling8"}, {"topic", "Sibling eight"}});
        forest["nodes"].push_back({{"id", "sibling7"}, {"topic", "Sibling seven"}});
        forest["nodes"].push_back({{"id", "heading6"}, {"topic", "Sibling six"}, {"children", {"last7"}}});
        forest["nodes"].push_back({{"id", "last7"}, {"topic", "Last seven"}});
        forest["nodes"].push_back({{"id", "heading2"}, {"topic", "Sibling two"}});
        const QString orphanTopic = QString::fromUtf8("Authored orphan <世界>");
        forest["nodes"].push_back({{"id", "orphan"}, {"topic", utf8(orphanTopic)}, {"children", {"orphan-child"}},
            {"icons", {"", " \t"}}, {"tags", {"", " \t"}}, {"hyperLink", " \t"}, {"note", "\r\n \t"}});
        forest["nodes"].push_back({{"id", "orphan-child"}, {"topic", "Orphan child"}});
        forest["nodes"].push_back({{"id", "other-orphan"}, {"topic", "Second authored root"}});
        forest["orphanIds"] = {"orphan", "other-orphan"};
        forest["crossLinks"] = Json::array({{{"id", "row-reference"}, {"source", "n7"}, {"target", "orphan"},
            {"directed", true}, {"topic", "Authored relation"}, {"icon", "<link>"}}});
        std::reverse(forest["nodes"].begin(), forest["nodes"].end());
        QSignalSpy requested(&editor, &Editor::imageRequested);
        CHECK(editor.loadJson(encoded(forest)));
        CHECK(QTest::qWaitFor([&] { return requested.size() == 1; }, 5000));
        const QImage pixels = imagePixels();
        editor.provideImage(requested.front()[0].toString(), requested.front()[1].toULongLong(), pixels);
        editor.clearSelection();
        pump();
        const QByteArray before = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        const HtmlPage page(editor.toHtml());
        CHECK(editor.toJson() == before && changed.isEmpty() && selected.isEmpty() && requested.size() == 1);
        compareHtmlScene(editor, page);
        checkHtmlDocument(page);
        QStringList expectedIds;
        for (const QString &id : {QStringLiteral("n0"), QStringLiteral("n1"), QStringLiteral("n2"), QStringLiteral("n3"),
            QStringLiteral("n4"), QStringLiteral("n5"), QStringLiteral("n6"), QStringLiteral("n7"),
            QStringLiteral("sibling8"), QStringLiteral("sibling7"), QStringLiteral("heading6"), QStringLiteral("last7"),
            QStringLiteral("heading2"), QStringLiteral("orphan"), QStringLiteral("orphan-child"), QStringLiteral("other-orphan")})
            expectedIds.append(QStringLiteral("m3-node-") + QString::fromLatin1(id.toUtf8().toHex()));
        CHECK(page.articleIds() == expectedIds);
        CHECK(page.classes(QStringLiteral("m3-article-title")).size() == 16);
        for (int i = 0; i < 8; ++i)
            checkHtmlTitle(page, QStringLiteral("n") + QString::number(i),
                i < 6 ? QStringLiteral("h") + QString::number(i + 1) : QStringLiteral("p"),
                QStringLiteral("Node ") + QString::number(i));
        checkHtmlTitle(page, QStringLiteral("heading6"), QStringLiteral("h6"), QStringLiteral("Sibling six"));
        checkHtmlTitle(page, QStringLiteral("heading2"), QStringLiteral("h2"), QStringLiteral("Sibling two"));
        checkHtmlTitle(page, QStringLiteral("orphan"), QStringLiteral("h1"), orphanTopic);
        checkHtmlTitle(page, QStringLiteral("orphan-child"), QStringLiteral("h2"), QStringLiteral("Orphan child"));
        checkHtmlTitle(page, QStringLiteral("other-orphan"), QStringLiteral("h1"), QStringLiteral("Second authored root"));
        for (const auto &entry : {std::make_pair(QStringLiteral("sibling8"), QStringLiteral("Sibling eight")),
            std::make_pair(QStringLiteral("sibling7"), QStringLiteral("Sibling seven")),
            std::make_pair(QStringLiteral("last7"), QStringLiteral("Last seven"))})
            checkHtmlTitle(page, entry.first, QStringLiteral("p"), entry.second);
        const int articles = page.identified(QStringLiteral("id"), QStringLiteral("articles"));
        const auto lists = page.tags(QStringLiteral("ul"), articles);
        CHECK(lists.size() == 3 && page.tags(QStringLiteral("li"), articles).size() == 5);
        const int seven = page.elements[page.article(QStringLiteral("n6"))].parent;
        const int eight = page.elements[page.article(QStringLiteral("n7"))].parent;
        CHECK(page.elements[lists[0]].parent == articles && page.elements[seven].parent == lists[0]);
        CHECK(page.elements[lists[1]].parent == seven && page.elements[eight].parent == lists[1]);
        CHECK(page.elements[page.elements[page.article(QStringLiteral("sibling8"))].parent].parent == lists[1]);
        CHECK(page.elements[page.elements[page.article(QStringLiteral("sibling7"))].parent].parent == lists[0]);
        CHECK(page.elements[lists[2]].parent == articles);
        CHECK(page.elements[page.elements[page.article(QStringLiteral("last7"))].parent].parent == lists[2]);
        checkHtmlRows(page, QStringLiteral("n7"), {QStringLiteral("m3-article-title"), QStringLiteral("m3-article-icons"),
            QStringLiteral("m3-article-tags"), QStringLiteral("m3-article-url"), QStringLiteral("m3-article-references"),
            QStringLiteral("m3-article-note"), QStringLiteral("m3-article-figure")});
        checkHtmlRows(page, QStringLiteral("orphan"), {QStringLiteral("m3-article-title")});
        checkHtmlRows(page, QStringLiteral("sibling8"), {QStringLiteral("m3-article-title")});
        checkHtmlReferences(page, QStringLiteral("n7"), {{QStringLiteral("orphan"), QStringLiteral("Authored relation")}});
        const int article = page.article(QStringLiteral("n7"));
        CHECK(page.classValues(QStringLiteral("m3-article-icon"), article) == QStringList({"<star>", "<star>"}));
        CHECK(page.classValues(QStringLiteral("m3-article-tag"), article) == QStringList({"Tag & one", "Tag & one"}));
        const auto notes = page.classes(QStringLiteral("m3-article-note"), article);
        CHECK(notes.size() == 1);
        const auto paragraphs = page.tags(QStringLiteral("p"), notes.front());
        CHECK(paragraphs.size() == 2 && page.elements[paragraphs[0]].text == QStringLiteral("First <b>literal</b> line"));
        CHECK(page.elements[paragraphs[1]].text == QStringLiteral("Second paragraph"));
        const auto images = page.classes(QStringLiteral("m3-article-image"), article);
        CHECK(images.size() == 1 && page.nodeImage(images.front()).convertToFormat(QImage::Format_RGB32) == pixels);
        const auto urls = page.classes(QStringLiteral("m3-article-url"), article);
        CHECK(urls.size() == 1);
        const auto link = page.tags(QStringLiteral("a"), urls.front());
        const auto icon = page.classes(QStringLiteral("m3-article-url-icon"), urls.front());
        const auto display = page.classes(QStringLiteral("m3-article-url-text"), urls.front());
        CHECK(link.size() == 1 && icon.size() == 1 && display.size() == 1);
        CHECK(page.children(link.front()) == std::vector<int>({icon.front(), display.front()}));
        CHECK(page.elements[icon.front()].attributes.value(QStringLiteral("aria-hidden")) == QStringLiteral("true"));
        CHECK(page.elements[icon.front()].text == QString(QChar(0x2197)));
        CHECK(page.elements[display.front()].text == QStringLiteral("https://example.com/?a=1&b=%22eight%22"));
        CHECK(page.elements[link.front()].attributes.value(QStringLiteral("href")) == page.elements[display.front()].text);
    }
    CHECK(editor.loadJson(encoded(chain(1024))));
    CHECK(editor.setExpanded(QStringLiteral("n0"), false));
    const HtmlPage deep(editor.toHtml());
    const auto ids = deep.articleIds();
    CHECK(ids.size() == 1024 && ids.back() == QStringLiteral("m3-node-6e31303233"));
    CHECK(texts(editor, QStringLiteral("Node 1023")).isEmpty());
    CHECK(deep.classValues(QStringLiteral("m3-article-topic")).contains(QStringLiteral("Node 1023")));
    CHECK(deep.classes(QStringLiteral("m3-node")).size() == 1);
    CHECK(deep.elements[deep.node(QStringLiteral("n0"))].text.contains(QStringLiteral("Node 0")));
    for (int i = 0; i < 6; ++i)
        checkHtmlTitle(deep, QStringLiteral("n") + QString::number(i), QStringLiteral("h") + QString::number(i + 1),
            QStringLiteral("Node ") + QString::number(i));
    checkHtmlTitle(deep, QStringLiteral("n1023"), QStringLiteral("p"), QStringLiteral("Node 1023"));
    CHECK(deep.tags(QStringLiteral("ul")).size() == 1018 && deep.tags(QStringLiteral("li")).size() == 1018);
    CHECK(editor.loadJson(encoded(chain(400))));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Outline));
    editor.clearSelection();
    const QRectF source = graphics(editor).scene()->itemsBoundingRect().adjusted(-32, -32, 32, 32);
    CHECK(source.height() > 16384);
    const HtmlPage full(editor.toHtml());
    CHECK(full.size().height() > 16384);
    CHECK(full.classes(QStringLiteral("m3-node")).size() == 400);
    CHECK(full.classes(QStringLiteral("m3-tree-edge")).size() == 399);
    compareHtmlScene(editor, full);
    const QRectF last = htmlRect(full.elements[full.node(QStringLiteral("n399"))]);
    CHECK(QRectF(QPointF(), full.size()).contains(last));
    nearHtml(last, topicRect(editor, QStringLiteral("Node 399")).adjusted(2, 2, -2, -2).translated(-source.topLeft()));
    checkHtmlDocument(full);

    const QString literal = QString::fromUtf8("</article><script>window.injected=1</script> & 世界\r\nnext\rlast\tend");
    const QStringList urls{
        QStringLiteral("JaVaScRiPt:alert(1)"), QStringLiteral("java\tscript:alert(1)"),
        QStringLiteral("data:text/html,<script>alert(1)</script>"), QStringLiteral("https://example.com/\" onclick=\"alert(1)"),
        QStringLiteral("/relative"), QStringLiteral("https:///missing-host"),
        QStringLiteral("https://example.com/?a=1&b=%22safe%22"), QStringLiteral("HTTP://example.com/path"),
        QStringLiteral("mailto:user@example.com?subject=Hi&body=World")};
    Json unsafe{{"schemaVersion", 1}, {"rootId", "root"}, {"nodes", Json::array()}, {"crossLinks", Json::array()}};
    Json root{{"id", "root"}, {"topic", ""}, {"children", Json::array()},
              {"note", utf8(literal)}, {"tags", Json::array({"<b>tag</b>", "", "<b>tag</b>", " \t"})},
              {"icons", Json::array({"<img src=x onerror='x'>", "", " \t"})}};
    for (qsizetype i = 0; i < urls.size(); ++i) {
        const auto id = QString::fromUtf8("世界-\"<&") + QString::number(i);
        root["children"].push_back(utf8(id));
        unsafe["nodes"].push_back({{"id", utf8(id)}, {"topic", "Duplicate"}, {"hyperLink", utf8(urls[i])},
                                   {"image", {{"url", utf8(urls[i])}, {"width", 0}, {"height", 0}}}});
        unsafe["nodes"].back()["style"] = {{"fontFamily", "Bad';--injected:1;/*\\\""},
                                          {"color", "red;--injected:1"}, {"background", "url(https://evil.invalid/)"}};
    }
    unsafe["nodes"].push_back(std::move(root));
    unsafe["crossLinks"].push_back({{"id", "quoted\"<id>"}, {"source", "世界-\"<&0"}, {"target", "世界-\"<&1"},
                                  {"directed", true}, {"topic", utf8(literal)}, {"icon", "<svg onload='x'>"}});
    QFont hostileFont(QStringLiteral("Missing'\\\";--injected:1;/*"));
    hostileFont.setPixelSize(16);
    editor.setFont(hostileFont);
    CHECK(editor.loadJson(encoded(unsafe)));
    editor.clearSelection();
    const HtmlPage safe(editor.toHtml());
    const auto groups = safe.classes(QStringLiteral("m3-cross-link"));
    CHECK(groups.size() == 1);
    CHECK(safe.elements[groups.front()].attributes.value(QStringLiteral("data-link-id")) == QStringLiteral("quoted\"<id>"));
    CHECK(safe.elements[groups.front()].attributes.value(QStringLiteral("data-source")) == QString::fromUtf8("世界-\"<&0"));
    CHECK(safe.elements[groups.front()].attributes.value(QStringLiteral("data-target")) == QString::fromUtf8("世界-\"<&1"));
    CHECK(safe.values(QStringLiteral("title")) == QStringList{QStringLiteral("(untitled)")});
    CHECK(safe.classValues(QStringLiteral("m3-article-topic")).count(QStringLiteral("Duplicate")) == urls.size());
    QString normalized = literal;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(u'\r', u'\n');
    const int rootArticle = safe.article(QStringLiteral("root"));
    CHECK(safe.classValues(QStringLiteral("m3-article-note"), rootArticle).size() == 1);
    const auto notes = safe.classes(QStringLiteral("m3-article-note"), rootArticle);
    const auto paragraphs = safe.tags(QStringLiteral("p"), notes.front());
    CHECK(paragraphs.size() == 1 && safe.elements[paragraphs.front()].text == normalized);
    CHECK(safe.tags(QStringLiteral("br"), paragraphs.front()).size() == 2);
    CHECK(safe.classValues(QStringLiteral("m3-article-tag"), rootArticle) == QStringList({"<b>tag</b>", "<b>tag</b>"}));
    CHECK(safe.classValues(QStringLiteral("m3-article-icon"), rootArticle) == QStringList{QStringLiteral("<img src=x onerror='x'>")});
    const QString sourceId = QString::fromUtf8("世界-\"<&0"), targetId = QString::fromUtf8("世界-\"<&1");
    checkHtmlReferences(safe, sourceId, {{targetId, literal}});
    checkHtmlReferences(safe, targetId, {});
    const auto reference = safe.classes(QStringLiteral("m3-cross-reference"), safe.article(sourceId));
    CHECK(reference.size() == 1);
    CHECK(safe.elements[reference.front()].text.contains(QStringLiteral("<svg onload='x'>")));
    CHECK(!safe.elements[safe.article(sourceId)].text.contains(QStringLiteral("quoted\"<id>")));
    CHECK(!safe.elements[safe.article(sourceId)].text.contains(normalized));
    checkHtmlDocument(safe);
    CHECK(safe.classes(QStringLiteral("m3-node")).size() == static_cast<size_t>(urls.size() + 1));
    CHECK(safe.classes(QStringLiteral("m3-link-label")).size() == 1);
    CHECK(safe.elements[safe.classes(QStringLiteral("m3-link-label")).front()].text == normalized);
    for (const int index : safe.classes(QStringLiteral("m3-node"))) {
        const QString id = safe.elements[index].attributes.value(QStringLiteral("data-node-id"));
        CHECK(safe.elements[index].attributes.value(QStringLiteral("id")) == QStringLiteral("m3-map-node-") + QString::fromLatin1(id.toUtf8().toHex()));
    }
    for (const int index : safe.classes(QStringLiteral("m3-topic"))) {
        const auto style = htmlStyle(safe.elements[index]);
        CHECK(htmlCssString(style.value(QStringLiteral("font-family"))) == QFontInfo(hostileFont).family());
        CHECK(!style.contains(QStringLiteral("--injected")));
    }
    const auto anchors = safe.articleIds();
    CHECK(std::set<QString>(anchors.begin(), anchors.end()).size() == static_cast<size_t>(urls.size() + 1));
    QStringList activated;
    for (const int i : safe.tags(QStringLiteral("a"))) {
        const auto &element = safe.elements[i];
        const QString href = element.attributes.value(QStringLiteral("href"));
        if (href.startsWith(u'#')) CHECK(anchors.contains(href.mid(1)));
        else activated.append(href);
    }
    CHECK(activated.size() == 6);
    for (qsizetype i = 0; i < urls.size(); ++i) {
        const QString id = QString::fromUtf8("世界-\"<&") + QString::number(i);
        const int article = safe.article(id);
        const auto url = safe.classes(QStringLiteral("m3-article-url"), article);
        CHECK(url.size() == 1);
        const auto display = safe.classes(QStringLiteral("m3-article-url-text"), url.front());
        CHECK(display.size() == 1 && safe.elements[display.front()].text == urls[i]);
        const auto links = safe.tags(QStringLiteral("a"), url.front());
        const auto icons = safe.classes(QStringLiteral("m3-article-url-icon"), url.front());
        CHECK(links.size() == (i >= 6 ? 1u : 0u) && icons.size() == links.size());
        if (i < 6) CHECK(safe.elements[display.front()].parent == url.front());
        if (i >= 6) {
            const QString href = QUrl(urls[i], QUrl::StrictMode).toString(QUrl::FullyEncoded);
            CHECK(safe.elements[links.front()].attributes.value(QStringLiteral("href")) == href);
            CHECK(activated.count(href) == 2);
            CHECK(safe.elements[display.front()].parent == links.front());
            CHECK(safe.elements[icons.front()].parent == links.front() && icons.front() < display.front());
            CHECK(safe.elements[icons.front()].attributes.value(QStringLiteral("aria-hidden")) == QStringLiteral("true"));
            CHECK(safe.elements[icons.front()].text == QString(QChar(0x2197)));
        }
        const auto placeholders = safe.classes(QStringLiteral("m3-article-image-placeholder"), article);
        CHECK(placeholders.size() == 1 && !safe.elements[placeholders.front()].text.trimmed().isEmpty());
        CHECK(!safe.elements[placeholders.front()].text.contains(urls[i]));
        CHECK(safe.tags(QStringLiteral("img"), article).empty());
    }
    CHECK(safe.tags(QStringLiteral("script")).empty());
    CHECK(safe.tags(QStringLiteral("svg")).size() == 1 && safe.classes(QStringLiteral("m3-connectors")).size() == 1);
    for (const QString &tag : {QStringLiteral("iframe"), QStringLiteral("object"), QStringLiteral("embed"),
                              QStringLiteral("canvas"), QStringLiteral("foreignObject")})
        CHECK(safe.tags(tag).empty());
    for (const auto &element : safe.elements) {
        const auto style = htmlStyle(element);
        CHECK(!style.contains(QStringLiteral("--injected")) && !style.contains(QStringLiteral("background-image")));
        for (auto it = element.attributes.begin(); it != element.attributes.end(); ++it) {
            CHECK(!it.key().startsWith(QStringLiteral("on"), Qt::CaseInsensitive));
            if (it.key() == QStringLiteral("href"))
                CHECK(element.tag == QStringLiteral("a") ||
                      (element.tag == QStringLiteral("link") && element.attributes.value(QStringLiteral("rel")) == QStringLiteral("icon")));
            if (it.key() == QStringLiteral("src")) CHECK(element.tag == QStringLiteral("img") && it.value().startsWith(QStringLiteral("data:image/png;base64,")));
        }
    }
}

static void markdown_case() {
    QString retained;
    {
        RawLabelEditor editor;
        Json input = editorFixture();
        setTopic(input, "r", QString::fromUtf8("Literal *世界* <br>"));
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        showEditor(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QSignalSpy selected(&editor, &Editor::selectionChanged);
        const QByteArray baseline = editor.toJson();
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
        CHECK(editor.selectNode(QStringLiteral("b")));
        graphics(editor).scale(1.2, 1.2);
        const auto transform = graphics(editor).transform();
        const auto selectionCount = selected.size();
        const Editor &snapshot = editor;
        retained = snapshot.toMarkdown();
        CHECK(retained.contains(QString::fromUtf8("Literal \\*世界\\* &lt;br&gt;")));
        CHECK(retained.contains(QStringLiteral("### <a id=\"m3-node-64\"></a>Delta")));
        CHECK(editor.toJson() == baseline && changed.isEmpty());
        CHECK(editor.selectedNodeId() == QStringLiteral("b") && editor.selectedLinkId().isEmpty());
        CHECK(selected.size() == selectionCount && graphics(editor).transform() == transform);
        CHECK(editor.selectLink(QStringLiteral("l1")));
        const auto linkSelectionCount = selected.size();
        const auto linkTransform = graphics(editor).transform();
        CHECK(snapshot.toMarkdown() == retained);
        CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId() == QStringLiteral("l1"));
        CHECK(selected.size() == linkSelectionCount && graphics(editor).transform() == linkTransform);
        CHECK(editor.toJson() == baseline && changed.isEmpty());

        CHECK(editor.selectNode(QStringLiteral("r")));
        shortcut(editor, Qt::Key_F2);
        auto *draft = &topicInput(editor);
        draft->setPlainText(QString::fromUtf8("Accepted 世界"));
        const auto draftSelections = selected.size();
        const auto draftTransform = graphics(editor).transform();
        CHECK(snapshot.toMarkdown() == retained);
        CHECK(activeTopicInput(editor) == draft && draft->hasFocus());
        CHECK(draft->toPlainText() == QString::fromUtf8("Accepted 世界"));
        CHECK(editor.toJson() == baseline && changed.isEmpty());
        CHECK(selected.size() == draftSelections && graphics(editor).transform() == draftTransform);
        topicKey(editor, Qt::Key_Return);
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
        CHECK(snapshot.toMarkdown().contains(QString::fromUtf8("# <a id=\"m3-node-72\"></a>Accepted 世界\n")));
        CHECK(!snapshot.toMarkdown().contains(QStringLiteral("Literal")));
    }
    CHECK(retained.contains(QString::fromUtf8("Literal \\*世界\\* &lt;br&gt;")));
    CHECK(!retained.contains(QStringLiteral("Accepted")));
}

static Json fiveLevelFixture() {
    return Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[
        {"id":"h","topic":"Level 4","children":["i"]},
        {"id":"r","topic":"Level 1","children":["c"]},
        {"id":"i","topic":"Level 5","note":"Retained hidden note",
         "image":{"url":"memory:deep","width":40,"height":20}},
        {"id":"g","topic":"Level 3","children":["h"]},
        {"id":"c","topic":"Level 2","children":["g"]}],
        "crossLinks":[{"id":"deep-link","source":"r","target":"i","topic":"Deep endpoint","directed":false}]})");
}
static void visibleLevels(Editor &editor, int levels) {
    for (int level = 1; level <= 5; ++level)
        CHECK(texts(editor, QStringLiteral("Level %1").arg(level)).isEmpty() == (level > levels));
    CHECK(texts(editor, QStringLiteral("Deep endpoint")).isEmpty() == (levels < 5));
}

static void document_case() {
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(fiveLevelFixture())));
        showEditor(editor);
        visibleLevels(editor, 3);
        const QByteArray baseline = editor.toJson();
        const auto outline = editor.outline();
        const QStringList ids{QStringLiteral("r"), QStringLiteral("c"), QStringLiteral("g"),
                              QStringLiteral("h"), QStringLiteral("i")};
        CHECK(outline.size() == ids.size());
        for (int i = 0; i < ids.size(); ++i) {
            CHECK(outline[i].id == ids[i] && outline[i].level == i + 1);
            CHECK(outline[i].topic == QStringLiteral("Level %1").arg(i + 1));
        }
        CHECK(editor.renameNode(QStringLiteral("r"), QStringLiteral("Renamed root")));
        CHECK(editor.renameNode(QStringLiteral("r"), QStringLiteral("Future root")));
        CHECK(editor.undo() && editor.canUndo() && editor.canRedo());
        const QByteArray current = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
        CHECK(editor.setExpanded(QStringLiteral("g"), true));
        CHECK(!texts(editor, QStringLiteral("Level 4")).isEmpty());
        CHECK(texts(editor, QStringLiteral("Level 5")).isEmpty());
        CHECK(editor.setExpanded(QStringLiteral("h"), true));
        CHECK(!texts(editor, QStringLiteral("Level 5")).isEmpty());
        CHECK(!texts(editor, QStringLiteral("Deep endpoint")).isEmpty());
        CHECK(editor.toJson() == current && changed.isEmpty() && undo.isEmpty() && redo.isEmpty());
        CHECK(editor.canUndo() && editor.canRedo());
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Outline));
        editor.setStyle({QString(), 17});
        editor.reloadImages();
        pump();
        CHECK(!texts(editor, QStringLiteral("Level 5")).isEmpty());
        CHECK(editor.toJson() == current && changed.isEmpty() && undo.isEmpty() && redo.isEmpty());
        CHECK(editor.redo() && !texts(editor, QStringLiteral("Level 5")).isEmpty());
        CHECK(editor.undo() && editor.toJson() == current);
        CHECK(!texts(editor, QStringLiteral("Level 5")).isEmpty());
        CHECK(editor.selectNode(QStringLiteral("i")));
        editor.zoom(1.25);
        const QTransform zoom = graphics(editor).transform();
        const QPointF camera = graphics(editor).mapToScene(graphics(editor).viewport()->rect().center());
        rejectUnchanged(editor, [&] { return editor.loadJson(QByteArray("{invalid")); });
        CHECK(editor.selectedNodeId() == QStringLiteral("i"));
        CHECK(!texts(editor, QStringLiteral("Level 5")).isEmpty());
        CHECK(graphics(editor).transform() == zoom);
        CHECK(QLineF(camera, graphics(editor).mapToScene(graphics(editor).viewport()->rect().center())).length() <= 1.0);
        CHECK(editor.loadJson(baseline));
        visibleLevels(editor, 3);
        CHECK(!editor.canUndo() && !editor.canRedo());
        CHECK(editor.revealNode(QStringLiteral("i")));
        visibleLevels(editor, 5);
        CHECK(editor.toJson() == baseline);
        CHECK(editor.setExpanded(QStringLiteral("i"), true));
        const QString child = editor.addNode(QStringLiteral("i"), QStringLiteral("New level 6"));
        CHECK(!child.isEmpty());
        const QString grandchild = editor.addNode(child, QStringLiteral("New level 7"));
        CHECK(!grandchild.isEmpty() && !texts(editor, QStringLiteral("New level 7")).isEmpty());
        CHECK(editor.moveNode(QStringLiteral("h"), QStringLiteral("r")));
        CHECK(!texts(editor, QStringLiteral("New level 7")).isEmpty());
        CHECK(editor.undo() && !texts(editor, QStringLiteral("New level 7")).isEmpty());
        CHECK(editor.loadJson(baseline));
        visibleLevels(editor, 3);
        // New and whitespace Open discard folds even if the replacement reuses the root ID.
        for (bool blank : {false, true}) {
            CHECK(editor.newDocument(QStringLiteral("Replacement")));
            CHECK(editor.setExpanded(QStringLiteral("root"), false));
            if (blank) CHECK(editor.loadJson(QByteArray(" \t\r\n")));
            else CHECK(editor.newDocument(QStringLiteral("Replacement")));
            const QString replacementChild = editor.addNode(QStringLiteral("root"), QStringLiteral("New visible child"));
            CHECK(!replacementChild.isEmpty() && !texts(editor, QStringLiteral("New visible child")).isEmpty());
        }
    }
    {
        RawLabelEditor edgeToEdge;
        // Host themes may style all scroll areas, independently of QFrame::NoFrame.
        edgeToEdge.setStyleSheet(QStringLiteral(
            "QAbstractScrollArea { border: 5px solid palette(mid); padding: 7px; }"));
        showEditor(edgeToEdge);
        auto &view = graphics(edgeToEdge);
        CHECK(view.geometry() == edgeToEdge.rect());
        CHECK(view.viewport()->geometry().topLeft() == QPoint(0, 0));
    }
    RawLabelEditor editor;
    CHECK(editor.findChildren<QToolBar *>().isEmpty());
    CHECK(!editor.findChild<QComboBox *>(QStringLiteral("layoutDirection")));
    CHECK(!editor.commandAction(QString()) && !editor.commandAction(QStringLiteral("missingCommand")));
    CHECK(!editor.commandAction(QStringLiteral("ADDCHILD")));
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
    RawLabelEditor second;
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
        QByteArray("[]"),
        QByteArray("null"),
        QByteArray("\v\f"),
        QByteArray(" \0", 2)
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

    for (const auto &blank : {QByteArray(), QByteArray(" \t\r\n")}) {
        RawLabelEditor emptyInput;
        CHECK(emptyInput.loadJson(encoded(input)));
        showEditor(emptyInput);
        CHECK(emptyInput.renameNode(QStringLiteral("a"), QStringLiteral("First edit")));
        CHECK(emptyInput.renameNode(QStringLiteral("a"), QStringLiteral("Second edit")));
        CHECK(emptyInput.undo() && emptyInput.canUndo() && emptyInput.canRedo());
        CHECK(emptyInput.selectNode(QStringLiteral("a")));
        shortcut(emptyInput, Qt::Key_F2);
        topicInput(emptyInput).setPlainText(QStringLiteral("Uncommitted draft"));
        CHECK(emptyInput.hasPendingEdit());
        QSignalSpy loaded(&emptyInput, &Editor::documentChanged);
        QSignalSpy errors(&emptyInput, &Editor::errorOccurred);
        CHECK(emptyInput.loadJson(blank));
        pump();
        CHECK(exported(emptyInput) == initial);
        CHECK(emptyInput.selectedNodeIds() == QStringList{QStringLiteral("root")});
        CHECK(emptyInput.selectedLinkId().isEmpty());
        CHECK(!emptyInput.canUndo() && !emptyInput.canRedo());
        CHECK(!emptyInput.hasPendingEdit() && !activeTopicInput(emptyInput));
        CHECK(loaded.size() == 1 && errors.isEmpty() && emptyInput.lastError().isEmpty());
        auto &view = graphics(emptyInput);
        const auto *rootLabel = textItem(emptyInput, QStringLiteral("Central topic"));
        const auto rootRect = view.mapFromScene(rootLabel->sceneBoundingRect()).boundingRect();
        CHECK(view.viewport()->rect().contains(rootRect));
        RawLabelEditor reopened;
        CHECK(reopened.loadJson(emptyInput.toJson()));
        CHECK(exported(reopened) == initial);
        CHECK(!emptyInput.addNode(QStringLiteral("root"), QStringLiteral("Child")).isEmpty());
        CHECK(emptyInput.undo() && exported(emptyInput) == initial);
    }
    {
        RawLabelEditor readOnly;
        CHECK(readOnly.loadJson(encoded(input)));
        readOnly.setReadOnly(true);
        CHECK(readOnly.loadJson(QByteArray()));
        CHECK(readOnly.isReadOnly() && exported(readOnly) == initial);
        CHECK(readOnly.selectedNodeId() == QStringLiteral("root"));
        CHECK(!readOnly.canUndo() && !readOnly.canRedo());
        CHECK(readOnly.addNode(QStringLiteral("root"), QStringLiteral("Blocked")).isEmpty());
        CHECK(exported(readOnly) == initial);
    }

    for (int mode : {0, 1, 2, 3}) {
        QWidget host;
        QVBoxLayout layout(&host);
        host.resize(1000, 700);
        host.show();
        pump();
        RawLabelEditor embedded(&host);
        CHECK(embedded.loadJson(QByteArray()));
        embedded.show();
        // The host attaches the already-shown child before returning to the event loop.
        layout.addWidget(&embedded);
        if (mode == 1) embedded.zoom(1.5);
        if (mode == 2) embedded.hide();
        if (mode == 3) {
            embedded.hide();
            embedded.fitToContents();
            embedded.show();
        }
        QSignalSpy edits(&embedded, &Editor::documentChanged);
        pump();
        if (mode == 2) {
            embedded.show();
            pump();
        }
        CHECK(exported(embedded) == initial && edits.isEmpty());
        CHECK(embedded.selectedNodeId() == QStringLiteral("root"));
        const qreal scale = embedded.zoomFactor();
        if (mode == 3) {
            CHECK(embedded.isZoomFit());
            embedded.fitToContents();
            CHECK(qFuzzyCompare(scale, embedded.zoomFactor()));
        } else if (mode == 1) {
            CHECK(scale == 1.5 && !embedded.isZoomFit());
        } else {
            CHECK(scale == 1.0 && !embedded.isZoomFit());
            auto &view = graphics(embedded);
            auto *label = textItem(embedded, QStringLiteral("Central topic"));
            const QRect rootRect = view.mapFromScene(label->sceneBoundingRect()).boundingRect();
            CHECK(rootRect.height() >= label->boundingRect().height());
            CHECK(view.viewport()->rect().contains(rootRect));
            const QPoint center = view.mapFromScene(ownerItem(label)->sceneBoundingRect().center());
            CHECK((center - view.viewport()->rect().center()).manhattanLength() <= 2);
        }
        // Later host resizes retain both default and explicitly requested zoom.
        host.resize(1100, 800);
        pump();
        CHECK(qFuzzyCompare(scale, embedded.zoomFactor()));
    }
    // An asymmetric map must center its root, not its scene bounding box.
    for (const auto direction : {Editor::LayoutDirection::Right, Editor::LayoutDirection::Left,
                                 Editor::LayoutDirection::Outline}) {
        QWidget host;
        QVBoxLayout layout(&host);
        host.resize(1000, 700);
        host.show();
        pump();
        RawLabelEditor embedded(&host);
        embedded.resize(0, 0);
        CHECK(embedded.setLayoutDirection(direction));
        embedded.zoom(2.0);
        CHECK(embedded.loadJson(encoded(input)));
        embedded.show();
        layout.addWidget(&embedded);
        pump();
        auto &view = graphics(embedded);
        const auto rootTopic = qs(record(expected, "nodes", QStringLiteral("r")).at("topic"));
        auto checkRootCamera = [&] {
            CHECK(embedded.zoomFactor() == 1.0 && !embedded.isZoomFit());
            CHECK(embedded.selectedNodeId() == QStringLiteral("r"));
            const QPoint center = view.mapFromScene(topicRect(embedded, rootTopic).center());
            CHECK((center - view.viewport()->rect().center()).manhattanLength() <= 2);
        };
        checkRootCamera();
        embedded.fitToContents();
        CHECK(embedded.isZoomFit());
        CHECK(embedded.loadJson(encoded(input)));
        pump();
        checkRootCamera();
        embedded.zoom(1.5);
        const auto camera = view.transform();
        const auto center = view.mapToScene(view.viewport()->rect().center());
        CHECK(!embedded.loadJson(QByteArray("{")));
        CHECK(view.transform() == camera);
        // Showing diagnostics resizes the viewport; integer scrollbars can round by one pixel.
        CHECK(QLineF(view.mapFromScene(center), view.viewport()->rect().center()).length() <= 1.0);
        CHECK(embedded.newDocument());
        pump();
        CHECK(embedded.zoomFactor() == 1.0 && !embedded.isZoomFit());
    }
}

static void tree_edits_case() {
    Json input = editorFixture();
    for (auto &entry : input.at("nodes")) if (entry.at("id") == "a") {
        entry["note"] = "Do not overwrite";
        entry["style"] = Json::parse(R"({"custom":{"nested":[true,3,"世界"]}})");
        entry["icons"] = Json({"bookmark"});
    }
    RawLabelEditor editor;
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

    for (int index : {-1, 0}) {
        RawLabelEditor batch;
        CHECK(batch.loadJson(encoded(input)) && batch.selectNode(QStringLiteral("b")));
        const Json before = exported(batch);
        const Json records = Json::parse(R"([{"topic":"First","hyperLink":"docs/a%20b%23%25.txt",
            "image":{"url":"images/a.png","width":32,"height":16},
            "style":{"custom":{"keep":[true,9007199254740993]}}},
            {"topic":"Second","note":"Complete at publication","tags":["batch"]}])");
        QSignalSpy mutations(&batch, &Editor::documentChanged), selection(&batch, &Editor::selectionChanged);
        Json observed;
        QObject::connect(&batch, &Editor::documentChanged, &batch, [&] { observed = exported(batch); });
        const auto ids = batch.addNodes(QStringLiteral("a"), encoded(records), index);
        CHECK(ids.size() == 2 && ids.front() != ids.back());
        CHECK(batch.selectedNodeId() == QStringLiteral("b") && selection.isEmpty() && mutations.size() == 1);
        CHECK(observed == exported(batch) && observed.at("nodes").size() == before.at("nodes").size() + 2);
        Json expectedParent = record(before, "nodes", QStringLiteral("a"));
        expectedParent["children"] = index == -1 ? Json({"d", utf8(ids[0]), utf8(ids[1])})
                                                : Json({utf8(ids[0]), utf8(ids[1]), "d"});
        CHECK(record(observed, "nodes", QStringLiteral("a")) == expectedParent);
        for (int i = 0; i < 2; ++i) {
            const auto &complete = record(observed, "nodes", ids[i]);
            for (auto it = records[i].begin(); it != records[i].end(); ++it) CHECK(complete.at(it.key()) == it.value());
            CHECK(complete.at("children").empty());
        }
        CHECK(batch.undo() && exported(batch) == before && !batch.canUndo());
        CHECK(batch.redo() && exported(batch) == observed && !batch.canRedo());
    }
    for (int selectionKind = 0; selectionKind < 3; ++selectionKind) {
        RawLabelEditor batch;
        CHECK(batch.loadJson(encoded(input)));
        showEditor(batch);
        CHECK(batch.setExpanded(QStringLiteral("a"), false));
        if (selectionKind == 0) batch.clearSelection();
        else if (selectionKind == 1) CHECK(batch.selectLink(QStringLiteral("l1")));
        else {
            CHECK(batch.selectNode(QStringLiteral("a")));
            QTest::mouseClick(graphics(batch).viewport(), Qt::LeftButton, Qt::ControlModifier,
                              labelPoint(batch, QStringLiteral("Beta")));
            CHECK(batch.selectedNodeIds().size() == 2);
        }
        const auto selectedNodes = batch.selectedNodeIds();
        const auto selectedLink = batch.selectedLinkId();
        batch.resetZoom(); batch.zoom(0.8); pump();
        const auto camera = graphics(batch).transform();
        QSignalSpy selection(&batch, &Editor::selectionChanged);
        const auto ids = batch.addNodes(QStringLiteral("a"), R"([{"topic":"Hidden first"},{"topic":"Hidden second"}])");
        CHECK(ids.size() == 2 && batch.selectedNodeIds() == selectedNodes && batch.selectedLinkId() == selectedLink);
        CHECK(selection.isEmpty() && graphics(batch).transform() == camera);
        CHECK(texts(batch, QStringLiteral("Hidden first")).isEmpty());
        CHECK(texts(batch, QStringLiteral("Hidden second")).isEmpty());
    }
    {
        RawLabelEditor batch;
        CHECK(batch.loadJson(encoded(input)) && batch.selectNode(QStringLiteral("b")));
        const QByteArray valid = R"([{"topic":"First"},{"topic":"Second"}])";
        const QList<QByteArray> invalid{
            {}, "[", "{}", "[]", "[null]", "[{}]", R"([{"topic":3}])",
            R"([{"topic":"First"},{"topic":"Second","id":"forbidden"}])",
            R"([{"topic":"First"},{"topic":"Second","children":[]}])",
            R"([{"topic":"First"},{"topic":"Second","expanded":true}])",
            R"([{"topic":"First"},{"topic":"Second","image":{"url":"bad","width":-1,"height":0}}])",
            R"([{"topic":"First"},{"topic":"Second","unknown":true}])",
            R"([{"topic":"First"},{"topic":"Second","style":false}])",
            R"([{"topic":"First"},{"topic":"Second\u0000"}])",
            R"([{"topic":"First","topic":"Duplicate"}])"
        };
        for (const auto &bytes : invalid)
            rejectUnchanged(batch, [&] { return !batch.addNodes(QStringLiteral("a"), bytes).isEmpty(); });
        rejectUnchanged(batch, [&] { return !batch.addNodes(QStringLiteral("a"), valid, -2).isEmpty(); });
        rejectUnchanged(batch, [&] { return !batch.addNodes(QStringLiteral("a"), valid, 99).isEmpty(); });
        rejectUnchanged(batch, [&] { return !batch.addNodes(QStringLiteral("missing"), valid).isEmpty(); });
        CHECK(!batch.canUndo() && !batch.canRedo());
        batch.setReadOnly(true);
        rejectUnchanged(batch, [&] { return !batch.addNodes(QStringLiteral("a"), valid).isEmpty(); });
        batch.setReadOnly(false);
        const auto orphans = batch.addNodes({}, valid);
        CHECK(orphans.size() == 2 && batch.selectedNodeId() == QStringLiteral("b"));
        CHECK(exported(batch).at("orphanIds") == Json({utf8(orphans[0]), utf8(orphans[1])}));
    }
    {
        RawLabelEditor batch;
        QStringList palette;
        for (auto *button : batch.findChildren<QToolButton *>())
            if (button->property("color").isValid()) palette.append(button->property("color").toString());
        CHECK(!palette.isEmpty());
        const QString existing = batch.addNode(QStringLiteral("root"), QStringLiteral("Existing"));
        const QString existingColor = qs(record(exported(batch), "nodes", existing).at("style").at("branchColor"));
        Json entries = Json::array();
        for (int i = 0; i < palette.size(); ++i) entries.push_back({{"topic", "Batch " + std::to_string(i)}});
        const auto ids = batch.addNodes(QStringLiteral("root"), encoded(entries));
        CHECK(ids.size() == palette.size() && batch.selectedNodeId() == existing);
        const auto result = exported(batch);
        QSet<QString> used{existingColor};
        for (qsizetype i = 0; i < ids.size(); ++i) {
            const auto color = qs(record(result, "nodes", ids[i]).at("style").at("branchColor"));
            CHECK(palette.contains(color));
            if (i + 1 < palette.size()) CHECK(!used.contains(color));
            used.insert(color);
        }
        CHECK(used.size() == palette.size());
        const auto childIds = batch.addNodes(existing, encoded(entries));
        for (const auto &id : childIds) CHECK(!record(exported(batch), "nodes", id).at("style").contains("branchColor"));
        const QString orphan = batch.addNode({}, QStringLiteral("Independent tree"));
        const Json explicitStyle{{"branchColor", utf8(palette.front())}, {"custom", {{"preserved", true}}}};
        const auto explicitIds = batch.addNodes(orphan, encoded(Json::array({
            Json{{"topic", "Explicit"}, {"style", explicitStyle}},
            Json{{"topic", "Automatic one"}}, Json{{"topic", "Automatic two"}}
        })));
        CHECK(explicitIds.size() == 3 && batch.selectedNodeId() == orphan);
        const auto orphanResult = exported(batch);
        CHECK(record(orphanResult, "nodes", explicitIds[0]).at("style") == explicitStyle);
        const auto firstColor = record(orphanResult, "nodes", explicitIds[1]).at("style").at("branchColor");
        const auto secondColor = record(orphanResult, "nodes", explicitIds[2]).at("style").at("branchColor");
        CHECK(firstColor != utf8(palette.front()) && secondColor != utf8(palette.front()) && firstColor != secondColor);
        batch.setAutoRandomBranchColor(false);
        const auto uncolored = batch.addNodes(QStringLiteral("root"), R"([{"topic":"Plain"}])");
        CHECK(uncolored.size() == 1 && !record(exported(batch), "nodes", uncolored.front()).at("style").contains("branchColor"));
    }
}

static void collapse_case() {
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    const Json original = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QSignalSpy selected(&editor, &Editor::selectionChanged);
    QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
    const QByteArray bytes = editor.toJson();
    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    CHECK(changed.isEmpty() && selected.size() == 2);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.selectedLinkId().isEmpty());
    CHECK(selected.back().at(0).toString() == QStringLiteral("a"));
    CHECK(record(exported(editor), "nodes", QStringLiteral("d")) == record(original, "nodes", QStringLiteral("d")));
    CHECK(exported(editor).at("crossLinks") == original.at("crossLinks"));
    rejectUnchanged(editor, [&] { return editor.selectNode(QStringLiteral("d")); });
    rejectUnchanged(editor, [&] { return editor.selectLink(QStringLiteral("l2")); });
    CHECK(editor.setExpanded(QStringLiteral("a"), true));
    CHECK(changed.isEmpty() && selected.size() == 2);
    CHECK(exported(editor) == original && !editor.canUndo() && !editor.canRedo());
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    CHECK(changed.isEmpty());
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
    CHECK(editor.toJson() == bytes && undo.isEmpty() && redo.isEmpty());
    CHECK(editor.selectNode(QStringLiteral("a")));
    CHECK(editor.moveNode(QStringLiteral("b"), QStringLiteral("a")));
    CHECK(changed.size() == 1);
    rejectUnchanged(editor, [&] { return editor.selectNode(QStringLiteral("b")); });
    const QString hidden = editor.addNode(QStringLiteral("a"), QStringLiteral("Hidden insertion"));
    CHECK(!hidden.isEmpty() && changed.size() == 2);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("children") == Json({"d", "b", utf8(hidden)}));
    rejectUnchanged(editor, [&] { return editor.selectNode(hidden); });
    CHECK(editor.setExpanded(QStringLiteral("a"), true));
    CHECK(changed.size() == 2);
    CHECK(editor.selectNode(hidden));
    CHECK(editor.selectNode(QStringLiteral("b")));
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.selectNode(QStringLiteral("d")));
    CHECK(editor.setExpanded(QStringLiteral("r"), false));
    CHECK(changed.size() == 2);
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
    CHECK(changed.size() == 2 && editor.selectNode(hidden));
    rejectUnchanged(editor, [&] { return editor.setExpanded(QStringLiteral("absent"), false); });
    {
        RawLabelEditor cursor;
        CHECK(cursor.loadJson(encoded(editorFixture())));
        CHECK(cursor.renameNode(QStringLiteral("r"), QStringLiteral("Current")));
        CHECK(cursor.renameNode(QStringLiteral("r"), QStringLiteral("Future")));
        const QByteArray future = cursor.toJson();
        CHECK(cursor.undo() && cursor.canUndo() && cursor.canRedo());
        const QByteArray current = cursor.toJson();
        QSignalSpy cursorChanged(&cursor, &Editor::documentChanged);
        QSignalSpy cursorUndo(&cursor, &Editor::undoAvailable), cursorRedo(&cursor, &Editor::redoAvailable);
        CHECK(cursor.selectNode(QStringLiteral("d")));
        CHECK(cursor.setExpanded(QStringLiteral("a"), false));
        CHECK(cursor.selectedNodeId() == QStringLiteral("a"));
        CHECK(texts(cursor, QStringLiteral("Delta")).isEmpty());
        CHECK(cursor.setExpanded(QStringLiteral("a"), false));
        rejectUnchanged(cursor, [&] { return cursor.setExpanded(QStringLiteral("missing"), true); });
        CHECK(cursor.toJson() == current && cursorChanged.isEmpty() && cursorUndo.isEmpty() && cursorRedo.isEmpty());
        CHECK(cursor.canUndo() && cursor.canRedo());
        CHECK(cursor.redo() && cursor.toJson() == future);
        CHECK(texts(cursor, QStringLiteral("Delta")).isEmpty());
        CHECK(cursor.undo() && cursor.toJson() == current);
        CHECK(texts(cursor, QStringLiteral("Delta")).isEmpty());
        CHECK(cursor.setExpanded(QStringLiteral("a"), true));
        CHECK(!texts(cursor, QStringLiteral("Delta")).isEmpty());
    }

    // The node menu folds a complete subtree, even if its root already matches.
    const Json forest = Json::parse(R"({"schemaVersion":1,"rootId":"r","orphanIds":["o"],"nodes":[
        {"id":"ax","topic":"Deep Alpha"},
        {"id":"o1","topic":"Orphan Child","children":["ox"]},
        {"id":"r","topic":"Root","children":["a","b"]},
        {"id":"a","topic":"Alpha","children":["a1","a2"]},
        {"id":"bx","topic":"Deep Beta"},
        {"id":"a2","topic":"Alpha Sibling"},
        {"id":"b","topic":"Beta","children":["b1"]},
        {"id":"a1","topic":"Nested Alpha","children":["ax"]},
        {"id":"o","topic":"Orphan","children":["o1"]},
        {"id":"b1","topic":"Nested Beta","children":["bx"]},
        {"id":"ox","topic":"Deep Orphan"}],"crossLinks":[
        {"id":"cross","source":"ax","target":"bx","directed":false,"topic":"Cross branch"}]})");
    for (bool readOnly : {false, true}) {
        RawLabelConfig config;
        config.initialVisibleLevels = 0;
        RawLabelEditor subtree(config);
        CHECK(subtree.loadJson(encoded(forest)));
        CHECK(subtree.renameNode(QStringLiteral("r"), QStringLiteral("Current root")));
        CHECK(subtree.renameNode(QStringLiteral("r"), QStringLiteral("Future root")));
        const QByteArray future = subtree.toJson();
        CHECK(subtree.undo() && subtree.canUndo() && subtree.canRedo());
        CHECK(subtree.setExpanded(QStringLiteral("b1"), false));
        CHECK(subtree.setExpanded(QStringLiteral("o1"), false));
        subtree.setReadOnly(readOnly);
        showEditor(subtree, QSize(1100, 800));
        auto &view = graphics(subtree);
        const QByteArray before = subtree.toJson();
        QSignalSpy mutations(&subtree, &Editor::documentChanged);
        QSignalSpy undoChanges(&subtree, &Editor::undoAvailable), redoChanges(&subtree, &Editor::redoAvailable);
        auto foldMenu = [&](const QString &id, const QString &label, bool enabled = true) {
            CHECK(subtree.selectNode(id));
            pump();
            const QPoint point = labelPoint(subtree, qs(record(exported(subtree), "nodes", id).at("topic")));
            const QTransform transform = view.viewportTransform();
            const QRectF rootBounds = topicRect(subtree, QStringLiteral("Current root"));
            std::exception_ptr failure;
            QPointer<QMenu> menu;
            bool visited = false;
            QTimer dismiss;
            dismiss.setSingleShot(true);
            QObject::connect(&dismiss, &QTimer::timeout, &subtree, [&] {
                if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) popup->close();
            });
            dismiss.start(5000);
            QTimer::singleShot(0, &subtree, [&] {
                menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                try {
                    CHECK(menu);
                    auto &picked = textAction(*menu, {label});
                    CHECK(picked.isEnabled() == enabled);
                    if (enabled)
                        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(&picked).center());
                    visited = true;
                } catch (...) { failure = std::current_exception(); }
                if (menu) menu->close();
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, view.viewport()->mapToGlobal(point));
            QCoreApplication::sendEvent(view.viewport(), &event);
            dismiss.stop();
            pump();
            if (failure) std::rethrow_exception(failure);
            CHECK(visited && subtree.selectedNodeId() == id);
            CHECK(view.viewportTransform() == transform);
            CHECK(topicRect(subtree, QStringLiteral("Current root")) == rootBounds);
        };
        const QString expandAll = QStringLiteral("Expand All"), collapseAll = QStringLiteral("Collapse All");
        CHECK(subtree.setExpanded(QStringLiteral("a"), false));
        foldMenu(QStringLiteral("a"), collapseAll); // Root already collapsed; hidden children still need folding.
        CHECK(subtree.setExpanded(QStringLiteral("a"), true));
        CHECK(!texts(subtree, QStringLiteral("Nested Alpha")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Deep Alpha")).isEmpty());
        foldMenu(QStringLiteral("a"), expandAll); // Root already expanded; nested folds must still open.
        CHECK(!texts(subtree, QStringLiteral("Deep Alpha")).isEmpty());
        CHECK(!texts(subtree, QStringLiteral("Alpha Sibling")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Deep Beta")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Deep Orphan")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Cross branch")).isEmpty());
        foldMenu(QStringLiteral("ax"), expandAll, false);
        foldMenu(QStringLiteral("ax"), collapseAll, false);
        foldMenu(QStringLiteral("a"), collapseAll);
        CHECK(texts(subtree, QStringLiteral("Nested Alpha")).isEmpty());
        CHECK(!texts(subtree, QStringLiteral("Nested Beta")).isEmpty());
        foldMenu(QStringLiteral("r"), expandAll);
        CHECK(!texts(subtree, QStringLiteral("Deep Alpha")).isEmpty());
        CHECK(!texts(subtree, QStringLiteral("Deep Beta")).isEmpty());
        CHECK(!texts(subtree, QStringLiteral("Cross branch")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Deep Orphan")).isEmpty());
        foldMenu(QStringLiteral("r"), collapseAll);
        CHECK(texts(subtree, QStringLiteral("Alpha")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Beta")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Cross branch")).isEmpty());
        CHECK(!texts(subtree, QStringLiteral("Orphan Child")).isEmpty());
        foldMenu(QStringLiteral("o"), expandAll);
        CHECK(!texts(subtree, QStringLiteral("Deep Orphan")).isEmpty());
        CHECK(texts(subtree, QStringLiteral("Alpha")).isEmpty());
        foldMenu(QStringLiteral("o"), collapseAll);
        CHECK(texts(subtree, QStringLiteral("Orphan Child")).isEmpty());
        CHECK(subtree.toJson() == before && mutations.isEmpty());
        CHECK(undoChanges.isEmpty() && redoChanges.isEmpty());
        subtree.setReadOnly(false);
        CHECK(subtree.canUndo() && subtree.canRedo());
        CHECK(subtree.redo() && subtree.toJson() == future);
        CHECK(texts(subtree, QStringLiteral("Alpha")).isEmpty());
    }
}

static void graph_edits_case() {
    RawLabelEditor editor;
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
    CHECK(changed.size() == 5);
    CHECK(editor.selectedLinkId().isEmpty() && editor.selectedNodeId().isEmpty());
    CHECK(hasRecord(exported(editor), "crossLinks", QStringLiteral("l2")));
    rejectUnchanged(editor, [&] { return editor.selectLink(QStringLiteral("l2")); });
    CHECK(editor.setExpanded(QStringLiteral("a"), true));
    CHECK(editor.selectLink(QStringLiteral("l2")));
    CHECK(editor.updateLink(QStringLiteral("l2"), QStringLiteral("c"), QStringLiteral("d"), true, QString()));
    CHECK(changed.size() == 6);
    Json expectedOther = record(original, "crossLinks", QStringLiteral("l2"));
    expectedOther["source"] = "c";
    expectedOther["target"] = "d";
    expectedOther["directed"] = true;
    expectedOther["topic"] = "";
    CHECK(record(exported(editor), "crossLinks", QStringLiteral("l2")) == expectedOther);
    CHECK(editor.removeLink(QStringLiteral("l2")) && changed.size() == 7);
    CHECK(editor.selectedLinkId().isEmpty());
}

static void lifetime_case() {
    RawLabelEditor survivor;
    CHECK(survivor.newDocument(QStringLiteral("Independent survivor")));
    const Json untouched = exported(survivor);
    QSignalSpy survivorChanges(&survivor, &Editor::documentChanged);
    QByteArray retained;
    Json retainedDocument;
    for (int iteration = 0; iteration < 24; ++iteration) {
        auto parent = std::make_unique<QWidget>();
        auto *editor = new RawLabelEditor(parent.get());
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
        RawLabelEditor other;
        CHECK(other.loadJson(retained));
        CHECK(other.renameNode(QStringLiteral("r"), QStringLiteral("Other editor only")));
        CHECK(other.removeNode(QStringLiteral("a")));
        CHECK(exported(survivor) == snapshot && survivorChanges.size() == 1);
    }
    CHECK(survivor.renameNode(QStringLiteral("r"), QStringLiteral("Still alive")));
    CHECK(survivorChanges.size() == 2);
    RawLabelEditor restored;
    CHECK(restored.loadJson(retained));
    CHECK(exported(restored) == retainedDocument);
}

static void image_activation_case() {
    const QString root = QStringLiteral("r"), topic = QStringLiteral("Image root"), url = QStringLiteral("memory:picture");
    const QImage pixels = imagePixels();
    auto prepare = [&](Editor &editor) {
        QObject::connect(&editor, &Editor::imageRequested, &editor,
            [&editor, pixels](const QString &source, quint64 id) { editor.provideImage(source, id, pixels); });
        CHECK(editor.loadJson(encoded(imageDocument())));
        showEditor(editor, QSize(1200, 900));
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, url); }, 5000));
        CHECK(editor.selectNode(root));
    };
    for (bool readOnly : {false, true}) {
        RawLabelEditor editor;
        prepare(editor);
        CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("First history entry")));
        CHECK(editor.renameNode(QStringLiteral("a"), QStringLiteral("Second history entry")));
        CHECK(editor.undo());
        CHECK(editor.selectNode(root));
        editor.setReadOnly(readOnly);
        const Json before = exported(editor);
        const bool canUndo = editor.canUndo(), canRedo = editor.canRedo();
        QSignalSpy activated(&editor, &Editor::nodeImageActivated), changed(&editor, &Editor::documentChanged);
        QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
        auto &view = graphics(editor);
        const QPoint point = view.mapFromScene(nodeImageRect(editor).center());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        CHECK(activated.isEmpty());
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
        CHECK(activated.size() == 1 && activated.front().at(0).toString() == root);
        CHECK(qvariant_cast<QImage>(activated.front().at(1)) == pixels);
        CHECK(!activeTopicInput(editor) && !editor.hasPendingEdit());
        CHECK(exported(editor) == before && changed.isEmpty() && undo.isEmpty() && redo.isEmpty());
        CHECK(editor.canUndo() == canUndo && editor.canRedo() == canRedo);
        checkImageSize(nodeImageRect(editor), QSizeF(120, 60));
    }
    for (bool failed : {false, true}) {
        RawLabelEditor editor;
        if (failed) QObject::connect(&editor, &Editor::imageRequested, &editor,
            [&editor](const QString &source, quint64 id) { editor.provideImage(source, id, QImage()); });
        CHECK(editor.loadJson(encoded(imageDocument())));
        showEditor(editor);
        const Json before = exported(editor);
        QSignalSpy activated(&editor, &Editor::nodeImageActivated), changed(&editor, &Editor::documentChanged);
        auto &view = graphics(editor);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                          view.mapFromScene(nodeImageRect(editor).center()));
        pump();
        CHECK(activated.isEmpty() && changed.isEmpty() && exported(editor) == before);
        CHECK(!activeTopicInput(editor) && !editor.canUndo() && !editor.canRedo());
    }
    {
        RawLabelEditor editor;
        prepare(editor);
        auto &view = graphics(editor);
        const Json before = exported(editor);
        QSignalSpy activated(&editor, &Editor::nodeImageActivated), changed(&editor, &Editor::documentChanged);
        const auto point = [&] { return view.mapFromScene(nodeImageRect(editor).center()); };
        for (auto modifier : {Qt::ShiftModifier, Qt::ControlModifier, Qt::AltModifier, Qt::MetaModifier}) {
            QTest::mouseDClick(view.viewport(), Qt::LeftButton, modifier, point());
            CHECK(activated.isEmpty() && !activeTopicInput(editor));
        }
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, imageHandlePoint(editor));
        CHECK(activated.isEmpty());
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, imageHandlePoint(editor));
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point());
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point());
        CHECK(activated.isEmpty());
        QTest::mousePress(view.viewport(), Qt::MiddleButton, Qt::NoModifier, point());
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point());
        QTest::mouseRelease(view.viewport(), Qt::MiddleButton, Qt::NoModifier, point());
        CHECK(activated.isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier,
                          labelPoint(editor, QStringLiteral("Alpha")));
        CHECK(editor.selectedNodeIds().size() == 2);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point());
        CHECK(activated.isEmpty());
        // The first click collapses the group, but its second click still must not activate.
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point());
        CHECK(editor.selectedNodeIds().size() == 1);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point());
        CHECK(activated.isEmpty());
        editor.setEnabled(false);
        // Exercise the facade gate even if the native view emits while disabled.
        CHECK(QMetaObject::invokeMethod(&view, "nodeImageActivated", Qt::DirectConnection,
                                       Q_ARG(QString, root), Q_ARG(QImage, pixels)));
        CHECK(activated.isEmpty());
        CHECK(exported(editor) == before && changed.isEmpty());
        CHECK(!activeTopicInput(editor) && !editor.canUndo() && !editor.canRedo());
        checkImageSize(nodeImageRect(editor), QSizeF(120, 60));
    }
    {
        RawLabelEditor editor;
        prepare(editor);
        // Qt's default right-button double-click handling can change scene selection;
        // keep it separate from the selected-image handle scenarios above.
        const Json before = exported(editor);
        QSignalSpy activated(&editor, &Editor::nodeImageActivated), changed(&editor, &Editor::documentChanged);
        auto &view = graphics(editor);
        QTest::mouseDClick(view.viewport(), Qt::RightButton, Qt::NoModifier,
                          view.mapFromScene(nodeImageRect(editor).center()));
        CHECK(activated.isEmpty() && changed.isEmpty() && exported(editor) == before);
        CHECK(!activeTopicInput(editor) && !editor.canUndo() && !editor.canRedo());
    }
    {
        RawLabelEditor editor;
        prepare(editor);
        bool replaced = false;
        QObject::connect(&editor, &Editor::nodeImageActivated, &editor, [&](const QString &id, const QImage &image) {
            CHECK(id == root && image == pixels);
            CHECK(editor.newDocument(QStringLiteral("Reentrant image replacement")));
            CHECK(id == root && image == pixels);
            replaced = true;
        });
        auto &view = graphics(editor);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                          view.mapFromScene(nodeImageRect(editor).center()));
        pump();
        CHECK(replaced && editor.selectedNodeId() == QStringLiteral("root"));
    }
    {
        auto editor = std::make_unique<Editor>();
        prepare(*editor);
        QPointer<Editor> alive(editor.get());
        QSignalSpy activated(editor.get(), &Editor::nodeImageActivated);
        bool delivered = false;
        QObject::connect(editor.get(), &Editor::nodeImageActivated, [&](const QString &id, const QImage &image) {
            CHECK(id == root && image == pixels);
            editor.reset();
            CHECK(id == root && image == pixels);
            delivered = true;
        });
        auto &view = graphics(*editor);
        auto *viewport = view.viewport();
        const QPoint point = view.mapFromScene(nodeImageRect(*editor).center());
        QMouseEvent event(QEvent::MouseButtonDblClick, QPointF(point), QPointF(viewport->mapToGlobal(point)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(viewport, &event);
        pump();
        CHECK(delivered && alive.isNull() && activated.size() == 1);
        CHECK(activated.front().at(0).toString() == root && qvariant_cast<QImage>(activated.front().at(1)) == pixels);
    }
}

static void images_case() {
    const QString root = QStringLiteral("r"), topic = QStringLiteral("Image root"), url = QStringLiteral("memory:picture");
    const QImage pixels = imagePixels();
    const QImage alternate = imagePixels(QColor(13, 181, 67), QColor(197, 29, 163));
    auto serve = [&](Editor &editor) {
        QObject::connect(&editor, &Editor::imageRequested, &editor,
            [&editor, pixels](const QString &source, quint64 id) { editor.provideImage(source, id, pixels); });
    };
    {
        RawLabelEditor editor;
        std::vector<std::pair<QString, quint64>> requests;
        QObject::connect(&editor, &Editor::imageRequested, &editor,
            [&](const QString &source, quint64 id) { requests.emplace_back(source, id); });
        CHECK(editor.loadJson(encoded(imageDocument())));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return requests.size() == 1; }, 5000));
        checkImageLayout(editor, QSizeF(120, 90));
        CHECK(imageHandle(editor) == nullptr);
        const Json before = exported(editor);
        CHECK(editor.selectNode(QStringLiteral("a")));
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Uncommitted image-resolution draft"));
        const QTransform zoom = graphics(editor).transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        // A mismatched URL and unknown ID cannot consume the outstanding response.
        editor.provideImage(QStringLiteral("memory:wrong"), requests.front().second, alternate);
        editor.provideImage(url, requests.front().second + 100, alternate);
        pump();
        checkImageSize(nodeImageRect(editor), QSizeF(120, 90));
        QImage highDpi = pixels;
        highDpi.setDevicePixelRatio(2);
        editor.provideImage(requests.front().first, requests.front().second, highDpi);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, url); }, 5000));
        checkImageSize(nodeImageRect(editor), QSizeF(120, 60));
        CHECK(exported(editor) == before && changed.isEmpty() && selected.isEmpty());
        CHECK(editor.selectedNodeId() == QStringLiteral("a") && graphics(editor).transform() == zoom);
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("Uncommitted image-resolution draft"));
        editor.provideImage(url, requests.front().second, alternate);
        pump();
        CHECK(imageHasColors(editor, topic, url));
        topicKey(editor, Qt::Key_Escape);
        checkImageLayout(editor, QSizeF(120, 60));
        for (auto direction : {Editor::LayoutDirection::Right, Editor::LayoutDirection::Outline}) {
            CHECK(editor.setLayoutDirection(direction));
            checkImageLayout(editor, QSizeF(120, 60));
            CHECK(imageHasColors(editor, topic, url) && exported(editor) == before && changed.isEmpty());
        }
    }
    for (const auto &sizes : std::vector<std::pair<QSizeF, QSizeF>>{
             {QSizeF(90, 0), QSizeF(90, 67.5)}, {QSizeF(0, 60), QSizeF(80, 60)},
             {QSizeF(90, 90), QSizeF(90, 90)}, {QSizeF(1e300, 1e300), QSizeF(4096, 4096)}}) {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(imageDocument(url, sizes.first.width(), sizes.first.height()))));
        showEditor(editor);
        const Json before = exported(editor);
        checkImageLayout(editor, sizes.second);
        CHECK(imageHandle(editor) == nullptr && exported(editor) == before);
    }
    {
        RawLabelEditor editor;
        const QImage large = imagePixels(QColor(231, 37, 53), QColor(29, 71, 223), QSize(480, 240));
        QObject::connect(&editor, &Editor::imageRequested, &editor, [&](const QString &source, quint64 id) {
            editor.provideImage(source, id, large);
        });
        CHECK(editor.loadJson(encoded(imageDocument())));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, url); }, 5000));
        checkImageLayout(editor, QSizeF(240, 120));
        CHECK(record(exported(editor), "nodes", root).at("image") ==
              Json({{"url", utf8(url)}, {"width", 0}, {"height", 0}}));
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(imageDocument())));
        showEditor(editor);
        checkImageSize(nodeImageRect(editor), QSizeF(120, 90));
        const Json before = exported(editor);
        const QTransform zoom = graphics(editor).transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        serve(editor);
        editor.reloadImages();
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, url); }, 5000));
        CHECK(exported(editor) == before && changed.isEmpty() && selected.isEmpty());
        CHECK(graphics(editor).transform() == zoom);
    }
    // Metadata is a proportional box, never a stretch or a semantic default-size write.
    for (const QSizeF stored : {QSizeF(90, 0), QSizeF(90, 90), QSizeF(0, 45), QSizeF(1e300, 1e300)}) {
        RawLabelEditor editor;
        serve(editor);
        CHECK(editor.loadJson(encoded(imageDocument(url, stored.width(), stored.height()))));
        showEditor(editor);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, url); }, 5000));
        checkImageLayout(editor, stored.width() > 1e200 ? QSizeF(4096, 2048) : QSizeF(90, 45));
        CHECK(exported(editor) == before && changed.isEmpty());
    }
    {
        RawLabelEditor editor;
        serve(editor);
        Json input = imageDocument();
        for (auto &node : input.at("nodes")) if (node.at("id") == "a")
            node["image"] = Json{{"url", "memory:child"}, {"width", 0}, {"height", 0}};
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor, QSize(1200, 900));
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, url); }, 5000));
        shortcut(editor, Qt::Key_0, Qt::ControlModifier);
        editor.clearSelection();
        CHECK(imageHandle(editor) == nullptr);
        auto &view = graphics(editor);
        const QPoint body = view.mapFromScene(nodeImageRect(editor).center());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, body);
        pump();
        CHECK(editor.selectedNodeId() == root && imageHandle(editor) != nullptr);
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, nodeLinkPoint(editor, topic));
        CHECK(activated.size() == 1 && activated.front().at(1).toString() == QStringLiteral("opaque:anything"));
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        Json expected = exported(editor);
        const HtmlPage htmlBefore(editor.toHtml());
        editor.clearSelection();
        sameHtmlMap(HtmlPage(editor.toHtml()), htmlBefore);
        CHECK(editor.selectNode(root));
        selected.clear();
        const QPoint from = imageHandlePoint(editor);
        const QPointF center = view.mapToScene(view.viewport()->rect().center());
        const QTransform zoom = view.transform();
        const QPoint to = view.mapFromScene(view.mapToScene(from) + QPointF(60, 30));
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
        movePointer(view, to, Qt::LeftButton);
        checkImageSize(nodeImageRect(editor), QSizeF(180, 90));
        CHECK(imageHasColors(editor, topic, url));
        CHECK(exported(editor) == expected && changed.isEmpty() && selected.isEmpty());
        CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == center);
        sameHtmlMap(HtmlPage(editor.toHtml()), htmlBefore);
        releaseImageResize(editor, to);
        for (auto &node : expected.at("nodes")) if (node.at("id") == "r") {
            node["image"]["width"] = 180;
            node["image"]["height"] = 90;
        }
        CHECK(exported(editor) == expected && changed.size() == 1 && selected.isEmpty());
        CHECK(editor.selectedNodeId() == root && view.transform() == zoom);
        checkImageLayout(editor, QSizeF(180, 90));
        const HtmlPage htmlAfter(editor.toHtml());
        const int beforeImage = htmlBefore.classes(QStringLiteral("m3-node-image"), htmlBefore.node(root)).front();
        const int afterImage = htmlAfter.classes(QStringLiteral("m3-node-image"), htmlAfter.node(root)).front();
        checkImageSize(htmlRect(htmlBefore.elements[beforeImage]), QSizeF(120, 60));
        checkImageSize(htmlRect(htmlAfter.elements[afterImage]), QSizeF(180, 90));
        CHECK(htmlBefore.nodeImage(beforeImage) == htmlAfter.nodeImage(afterImage));
        RawLabelEditor restored;
        serve(restored);
        CHECK(restored.loadJson(editor.toJson()));
        showEditor(restored);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(restored, topic, url); }, 5000));
        checkImageLayout(restored, QSizeF(180, 90));
        CHECK(exported(restored) == expected);
        // A child's handle resizes, rather than initiating a node-reparent drag.
        editor.activateWindow();
        CHECK(editor.selectNode(QStringLiteral("a")));
        const QPoint childEnd = startImageResize(editor, QPointF(30, 15), QStringLiteral("Alpha"));
        releaseImageResize(editor, childEnd);
        for (auto &node : expected.at("nodes")) if (node.at("id") == "a") {
            node["image"]["width"] = 150;
            node["image"]["height"] = 75;
        }
        CHECK(exported(editor) == expected && changed.size() == 2);
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
        CHECK(editor.setExpanded(QStringLiteral("a"), true));
        CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
        const Json beforePan = exported(editor);
        const QPoint panStart = view.mapFromScene(nodeImageRect(editor, QStringLiteral("Alpha"), QStringLiteral("memory:child")).center());
        const QPointF panCenter = view.mapToScene(view.viewport()->rect().center());
        dragMiddle(view, panStart, panStart + QPoint(35, 20));
        CHECK(view.mapToScene(view.viewport()->rect().center()) != panCenter && exported(editor) == beforePan);
    }
    {
        RawLabelEditor editor;
        serve(editor);
        CHECK(editor.loadJson(encoded(imageDocument())));
        showEditor(editor, QSize(1200, 900));
        CHECK(QTest::qWaitFor([&] { return imageHandle(editor) != nullptr; }, 5000));
        shortcut(editor, Qt::Key_0, Qt::ControlModifier);
        const Json original = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const QPoint click = imageHandlePoint(editor);
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, click);
        QTest::mouseDClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, click);
        QTest::mouseRelease(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, click);
        CHECK(activeTopicInput(editor) == nullptr && exported(editor) == original && changed.isEmpty());
        const QPoint away = startImageResize(editor, QPointF(60, 30));
        const QPoint back = away - QPoint(60, 30);
        movePointer(graphics(editor), back, Qt::LeftButton);
        releaseImageResize(editor, back);
        CHECK(exported(editor) == original && changed.isEmpty());
        shortcut(editor, Qt::Key_Minus, Qt::ControlModifier);
        const QTransform zoom = graphics(editor).transform();
        const QPoint end = startImageResize(editor, QPointF(60, 30));
        releaseImageResize(editor, end);
        const QRectF resized = nodeImageRect(editor);
        checkImageSize(resized, QSizeF(180, 90), 0.8);
        CHECK(std::abs(resized.width() / resized.height() - 2) < 1e-9);
        CHECK(changed.size() == 1 && graphics(editor).transform() == zoom);
        const QPoint extreme = startImageResize(editor, QPointF(10000, 5000));
        checkImageSize(nodeImageRect(editor), QSizeF(4096, 2048));
        releaseImageResize(editor, extreme);
        CHECK(record(exported(editor), "nodes", root).at("image").at("width") == 4096);
        // Start near the bottom-right of a huge image without fitting/changing zoom.
        const QPoint minimum = startImageResize(editor, QPointF(-10000, -5000));
        checkImageSize(nodeImageRect(editor), QSizeF(16, 8));
        releaseImageResize(editor, minimum);
        CHECK(record(exported(editor), "nodes", root).at("image").at("width") == 16);
        CHECK(record(exported(editor), "nodes", root).at("image").at("height") == 8);
    }
    enum class Cancel { Escape, Url, Replace, Focus, NoButtons, Ungrab, Hide, Selection, Reload };
    for (auto cancel : {Cancel::Escape, Cancel::Url, Cancel::Replace, Cancel::Focus, Cancel::NoButtons,
                        Cancel::Ungrab, Cancel::Hide, Cancel::Selection, Cancel::Reload}) {
        RawLabelEditor editor;
        serve(editor);
        CHECK(editor.loadJson(encoded(imageDocument())));
        showEditor(editor, QSize(1200, 900));
        CHECK(QTest::qWaitFor([&] { return imageHandle(editor) != nullptr; }, 5000));
        shortcut(editor, Qt::Key_0, Qt::ControlModifier);
        auto &view = graphics(editor);
        const QPoint end = startImageResize(editor, QPointF(60, 30));
        checkImageSize(nodeImageRect(editor), QSizeF(180, 90));
        QString retainedUrl = url;
        switch (cancel) {
        case Cancel::Escape: QTest::keyClick(view.viewport(), Qt::Key_Escape); break;
        case Cancel::Url: {
            retainedUrl = QStringLiteral("memory:replacement");
            auto *field = editor.findChild<QLineEdit *>(QStringLiteral("nodeImageUrl"));
            CHECK(field != nullptr);
            field->setText(retainedUrl);
            break;
        }
        case Cancel::Replace:
            retainedUrl = QStringLiteral("memory:new-document");
            CHECK(editor.loadJson(encoded(imageDocument(retainedUrl, 80, 40))));
            break;
        case Cancel::Focus: {
            QFocusEvent event(QEvent::FocusOut, Qt::OtherFocusReason);
            QCoreApplication::sendEvent(&view, &event);
            break;
        }
        case Cancel::NoButtons: movePointer(view, end, Qt::NoButton); break;
        case Cancel::Ungrab: {
            QEvent event(QEvent::UngrabMouse);
            QCoreApplication::sendEvent(view.viewport(), &event);
            break;
        }
        case Cancel::Hide: editor.hide(); break;
        case Cancel::Selection: CHECK(editor.selectNode(QStringLiteral("a"))); break;
        case Cancel::Reload: editor.reloadImages(); pump(); break;
        }
        pump();
        const Json retained = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        releaseImageResize(editor, end);
        CHECK(exported(editor) == retained && changed.isEmpty());
        CHECK(record(retained, "nodes", root).at("image").at("width") == (cancel == Cancel::Replace ? 80 : 0));
        checkImageSize(nodeImageRect(editor, topic, retainedUrl), cancel == Cancel::Replace ? QSizeF(80, 40) : QSizeF(120, 60));
        if (cancel == Cancel::Escape) CHECK(editor.selectedNodeId() == root);
    }
    {
        QPointer<Editor> editor = new RawLabelEditor;
        serve(*editor);
        CHECK(editor->loadJson(encoded(imageDocument())));
        showEditor(*editor, QSize(1200, 900));
        CHECK(QTest::qWaitFor([&] { return imageHandle(*editor) != nullptr; }, 5000));
        shortcut(*editor, Qt::Key_0, Qt::ControlModifier);
        const QPoint end = startImageResize(*editor, QPointF(60, 30));
        bool committed = false;
        QObject::connect(editor, &Editor::documentChanged, editor, [&] {
            committed = record(exported(*editor), "nodes", root).at("image").at("width") == 180;
            delete editor.data();
        });
        // Do not access the editor/view after the release signal destroys them.
        QTest::mouseRelease(graphics(*editor).viewport(), Qt::LeftButton, Qt::NoModifier, end);
        pump();
        CHECK(committed && editor.isNull());
    }
    {
        RawLabelEditor editor;
        std::vector<std::pair<QString, quint64>> requests;
        QObject::connect(&editor, &Editor::imageRequested, &editor,
            [&](const QString &source, quint64 id) { requests.emplace_back(source, id); });
        CHECK(editor.loadJson(encoded(imageDocument(QStringLiteral("memory:A")))));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return requests.size() == 1; }, 5000));
        const auto requestA = requests.front();
        CHECK(editor.loadJson(encoded(imageDocument(QStringLiteral("memory:B")))));
        CHECK(QTest::qWaitFor([&] { return requests.size() == 2; }, 5000));
        const auto requestB = requests.back();
        const Json retained = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        editor.provideImage(requestB.first, requestB.second, pixels);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, requestB.first); }, 5000));
        editor.provideImage(requestA.first, requestA.second, alternate);
        pump();
        CHECK(imageHasColors(editor, topic, requestB.first));
        editor.reloadImages();
        CHECK(QTest::qWaitFor([&] { return requests.size() == 3; }, 5000));
        CHECK(requests.back().second > requestB.second);
        editor.provideImage(requestB.first, requestB.second, alternate);
        pump();
        checkImageSize(nodeImageRect(editor, topic, requestB.first), QSizeF(120, 90));
        const QImage pending = paintScene(editor, nodeImageRect(editor, topic, requestB.first));
        editor.provideImage(requests.back().first, requests.back().second, QImage());
        CHECK(QTest::qWaitFor([&] {
            return paintScene(editor, nodeImageRect(editor, topic, requestB.first)) != pending;
        }, 5000));
        CHECK(imageHandle(editor) == nullptr && !imageHasColors(editor, topic, requestB.first));
        CHECK(exported(editor) == retained && changed.isEmpty() && editor.lastError().isEmpty());
        pump();
        CHECK(requests.size() == 3); // Cached failures are not automatically retried.
        editor.reloadImages();
        CHECK(QTest::qWaitFor([&] { return requests.size() == 4; }, 5000));
        editor.provideImage(requests.back().first, requests.back().second, pixels);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, requestB.first); }, 5000));
        CHECK(exported(editor) == retained && changed.isEmpty());
    }
    {
        RawLabelEditor editor;
        Json input = imageDocument();
        for (auto &node : input.at("nodes")) if (node.at("id") == "a")
            node["image"] = Json{{"url", utf8(url)}, {"width", 0}, {"height", 0}};
        QSignalSpy requests(&editor, &Editor::imageRequested);
        serve(editor);
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, QStringLiteral("Alpha"), url); }, 5000));
        CHECK(imageHasColors(editor, topic, url) && requests.size() == 1);
        CHECK(editor.setExpanded(root, false));
        CHECK(editor.setExpanded(root, true));
        pump();
        CHECK(requests.size() == 1 && imageHasColors(editor, QStringLiteral("Alpha"), url));
    }
    {
        RawLabelEditor editor;
        Json input = imageDocument();
        for (auto &node : input.at("nodes")) if (node.at("id") == "d")
            node["image"] = Json{{"url", "memory:hidden"}, {"width", 0}, {"height", 0}};
        QSignalSpy requests(&editor, &Editor::imageRequested);
        serve(editor);
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, url); }, 5000));
        CHECK(requests.size() == 1);
        CHECK(editor.setExpanded(QStringLiteral("a"), true));
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, QStringLiteral("Delta"), QStringLiteral("memory:hidden")); }, 5000));
        CHECK(requests.size() == 2);
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        CHECK(editor.setExpanded(QStringLiteral("a"), true));
        pump();
        CHECK(requests.size() == 2 && imageHasColors(editor, QStringLiteral("Delta"), QStringLiteral("memory:hidden")));
    }
    {
        RawLabelEditor editor;
        Json input = imageDocument(QStringLiteral("memory:old-root"));
        for (auto &node : input.at("nodes")) if (node.at("id") == "a")
            node["image"] = Json{{"url", "memory:old-child"}, {"width", 0}, {"height", 0}};
        QStringList observed;
        QObject::connect(&editor, &Editor::imageRequested, &editor, [&](const QString &source, quint64 id) {
            observed.append(source);
            if (observed.size() == 1) CHECK(editor.loadJson(encoded(imageDocument(QStringLiteral("memory:current")))));
            else editor.provideImage(source, id, pixels);
        });
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, QStringLiteral("memory:current")); }, 5000));
        CHECK(observed.size() == 2 && observed.back() == QStringLiteral("memory:current"));
        CHECK(exported(editor) == nativeDocument(imageDocument(QStringLiteral("memory:current"))));
    }
    {
        QPointer<Editor> editor = new RawLabelEditor;
        Json input = imageDocument();
        for (auto &node : input.at("nodes")) if (node.at("id") == "a")
            node["image"] = Json{{"url", "memory:second"}, {"width", 0}, {"height", 0}};
        int requests = 0;
        QObject::connect(editor, &Editor::imageRequested, editor, [&](const QString &, quint64) {
            ++requests;
            delete editor.data();
        });
        CHECK(editor->loadJson(encoded(input)));
        CHECK(QTest::qWaitFor([&] { return editor.isNull(); }, 5000));
        CHECK(requests == 1);
    }
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
    RawLabelEditor editor;
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
    RawLabelEditor arrowEditor;
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

static void stroke_contrast_case() {
    RawLabelEditor editor;
    CHECK(editor.loadJson(QByteArray(R"({"schemaVersion":1,"rootId":"r","nodes":[
        {"id":"r","topic":"Root","children":["n"]},{"id":"n","topic":"Child"}],
        "crossLinks":[{"id":"x","source":"r","target":"n","directed":true,"topic":"Cross"}]})")));
    showEditor(editor);
    editor.clearSelection();
    const QByteArray semantic = editor.toJson();
    QSignalSpy changed(&editor, &Editor::documentChanged);
    const auto luminance = [](const QColor &color) {
        const auto linear = [](double value) {
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    };
    const auto contrast = [&](const QColor &first, const QColor &second) {
        const double a = luminance(first), b = luminance(second);
        return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    };
    const struct { QColor foreground, background; } appearances[] = {
        {QColor("#c2c5cc"), QColor("#323741")}, // Normal dark-theme softening.
        {QColor("#777777"), Qt::black},        // A fixed blend would be too dim.
        {QColor("#777777"), Qt::white},        // Same boundary with dark foreground.
        {QColor(255, 255, 255, 128), QColor("#323741")},
        {QColor("#888888"), QColor("#777777")} // Do not further dim an unreadable theme.
    };
    QImage swatch(1, 1, QImage::Format_RGB32);
    for (const auto &appearance : appearances) {
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Base, appearance.background);
        palette.setColor(QPalette::Button, appearance.background);
        palette.setColor(QPalette::Text, appearance.foreground);
        editor.setPalette(palette);
        pump();
        QList<QGraphicsPathItem *> edges;
        for (auto *item : graphics(editor).scene()->items())
            if (auto *edge = dynamic_cast<QGraphicsPathItem *>(item); edge && !edge->parentItem())
                edges.append(edge);
        CHECK(edges.size() == 1);
        const QColor stroke = edges.front()->pen().color();
        swatch.fill(appearance.background);
        {
            QPainter painter(&swatch);
            painter.fillRect(swatch.rect(), appearance.foreground);
        }
        const double foregroundContrast = contrast(swatch.pixelColor(0, 0), appearance.background);
        if (foregroundContrast < 3) {
            CHECK(stroke == appearance.foreground);
        } else {
            const double strokeContrast = contrast(stroke, appearance.background);
            CHECK(strokeContrast >= 3 && strokeContrast < foregroundContrast);
        }
        for (const auto *topic : {"Root", "Child", "Cross"}) {
            HtmlPaintDevice device;
            QPainter painter(&device);
            ownerItem(textItem(editor, QString::fromLatin1(topic)))->paint(&painter, nullptr, nullptr);
            CHECK(painter.end() && !device.engine.strokes.empty());
            CHECK(device.engine.strokes.front().pen.color() == stroke);
            if (QString::fromLatin1(topic) == QStringLiteral("Cross"))
                for (const auto &painted : device.engine.strokes) CHECK(painted.pen.color() == stroke);
        }
        const HtmlPage page(editor.toHtml());
        const auto nodes = page.classes(QStringLiteral("m3-node"));
        CHECK(nodes.size() == 2);
        for (const int index : nodes) {
            const auto outline = htmlStyle(page.elements[index]).value(QStringLiteral("outline"))
                                     .split(u' ', Qt::SkipEmptyParts);
            CHECK(outline.size() == 3 && QColor(outline[2]) == stroke);
        }
        CHECK(page.classes(QStringLiteral("m3-tree-edge")).size() == 1);
        CHECK(page.classes(QStringLiteral("m3-link-curve")).size() == 1);
        for (const auto *className : {"m3-tree-edge", "m3-link-curve", "m3-link-leader"}) {
            for (const int index : page.classes(QString::fromLatin1(className))) {
                CHECK(QColor(page.elements[index].attributes.value(QStringLiteral("stroke"))) ==
                      stroke);
            }
        }
        CHECK(editor.toJson() == semantic && changed.isEmpty());
    }
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
    RawLabelEditor editor;
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
    RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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

static void clipboard_case() {
    const QString format = QStringLiteral("application/x-m3-nodes+json");
    struct RestoreClipboard {
        std::unique_ptr<QMimeData> mime{new QMimeData};
        RestoreClipboard() {
            const auto *source = QApplication::clipboard()->mimeData();
            if (source) for (const auto &name : source->formats()) mime->setData(name, source->data(name));
        }
        ~RestoreClipboard() { QApplication::clipboard()->setMimeData(mime.release()); }
    } restore;
    const Json input = Json::parse(R"({"schemaVersion":1,"rootId":"r","orphanIds":["o"],"nodes":[
        {"id":"y","topic":"Deep child"},
        {"id":"r","topic":"Source","children":["a","b"]},
        {"id":"a","topic":"Alpha","children":["x"],"note":"Literal <note>",
         "tags":["tag","tag"],"icons":["!"],"hyperLink":"https://example.test/topic",
         "image":{"url":"images/relative.png","width":24,"height":12},
         "style":{"fontWeight":"bold","opaque":{"keep":[1,true]}}},
        {"id":"x","topic":"Child","children":["y"]},{"id":"b","topic":"Outside"},
        {"id":"o","topic":"Orphan","children":["oc"]},{"id":"oc","topic":"Orphan child"}],
        "crossLinks":[{"id":"inside","source":"a","target":"x","directed":true,"topic":"Internal","style":{"arrowDirection":"both"}},
        {"id":"outside","source":"x","target":"b","directed":false,"topic":"External"},
        {"id":"bridge","source":"x","target":"oc","directed":false,"topic":"Across roots"}]})");
    RawLabelConfig config;
    config.initialVisibleLevels = 0;
    RawLabelEditor source(config), destination(config);
    CHECK(source.loadJson(encoded(input)));
    showEditor(source);
    CHECK(source.setExpanded(QStringLiteral("a"), false));
    CHECK(source.selectNode(QStringLiteral("a")));
    source.setReadOnly(true);
    source.setSectionNumbering(true, QStringLiteral("1.1."));
    const QByteArray sourceBefore = source.toJson();
    shortcut(source, Qt::Key_C, Qt::ControlModifier);
    CHECK(source.toJson() == sourceBefore);
    CHECK(QApplication::clipboard()->text() == QStringLiteral("Alpha"));
    const QByteArray payload = QApplication::clipboard()->mimeData()->data(format);
    CHECK(!payload.isEmpty());
    source.setSectionNumbering(false, QStringLiteral("1.1."));
    const auto setNative = [&](const QByteArray &bytes) {
        auto *mime = new QMimeData;
        mime->setText(QStringLiteral("https://example.test/must-not-fallback"));
        mime->setData(format, bytes);
        QApplication::clipboard()->setMimeData(mime);
    };
    showEditor(destination);
    const QString existing = destination.addNode(QStringLiteral("root"), QStringLiteral("Existing"));
    CHECK(!existing.isEmpty());
    CHECK(destination.renameNode(QStringLiteral("root"), QStringLiteral("Future")));
    CHECK(destination.undo() && destination.canUndo() && destination.canRedo());
    CHECK(destination.setExpanded(QStringLiteral("root"), false));
    CHECK(destination.selectNode(QStringLiteral("root")));
    const QByteArray before = destination.toJson();
    const qreal zoom = destination.zoomFactor();
    destination.setSectionNumbering(true, QStringLiteral("1.1."));
    QSignalSpy changed(&destination, &Editor::documentChanged);
    shortcut(destination, Qt::Key_V, Qt::ControlModifier);
    const Json pasted = exported(destination);
    const auto &children = record(pasted, "nodes", QStringLiteral("root")).at("children");
    CHECK(children.size() == 2 && children.front() == utf8(existing));
    const QString root = qs(children.back());
    const auto &clone = record(pasted, "nodes", root);
    const QString child = qs(clone.at("children").front());
    const QString deep = qs(record(pasted, "nodes", child).at("children").front());
    CHECK(root != QStringLiteral("a") && child != QStringLiteral("x") && deep != QStringLiteral("y"));
    CHECK(pasted.at("nodes").size() == 5 && pasted.at("crossLinks").size() == 1);
    auto properties = [](Json node) { node.erase("id"); node.erase("children"); return node; };
    CHECK(properties(clone) == properties(record(exported(source), "nodes", QStringLiteral("a"))));
    CHECK(record(pasted, "nodes", deep).at("topic") == "Deep child");
    const auto &link = pasted.at("crossLinks").front();
    CHECK(link.at("id") != "inside" && link.at("source") == utf8(root) && link.at("target") == utf8(child));
    CHECK(link.at("topic") == "Internal" && link.at("style").at("arrowDirection") == "both");
    CHECK(destination.selectedNodeId() == root && destination.zoomFactor() == zoom);
    destination.setSectionNumbering(false, QStringLiteral("1.1."));
    CHECK(!texts(destination, QStringLiteral("Deep child")).isEmpty());
    CHECK(changed.size() == 1 && destination.canUndo() && !destination.canRedo());
    const QByteArray after = destination.toJson();
    CHECK(destination.undo() && destination.toJson() == before);
    CHECK(destination.redo() && destination.toJson() == after);
    CHECK(destination.selectNode(QStringLiteral("root")));
    shortcut(destination, Qt::Key_V, Qt::ControlModifier);
    const Json repeated = exported(destination);
    const QString again = qs(record(repeated, "nodes", QStringLiteral("root")).at("children").back());
    CHECK(again != root && repeated.at("nodes").size() == 8 && repeated.at("crossLinks").size() == 2);
    CHECK(destination.undo() && destination.toJson() == after && destination.canRedo());

    // Invalid recognized native data must not partially insert or use its text fallback.
    const std::vector<QByteArray> invalid{
        QByteArray("{"), payload + QByteArray(1, '\0') + QByteArray("trailing"),
        QByteArray(R"({"schemaVersion":1,"rootId":"a","nodes":[{"id":"a","topic":"one","topic":"two"}]})"),
        QByteArray(R"({"schemaVersion":1,"rootId":"a","nodes":[{"id":"a","children":["missing"]}]})")};
    CHECK(destination.selectNode(QStringLiteral("root")));
    const auto selected = destination.selectedNodeId();
    QSignalSpy errors(&destination, &Editor::errorOccurred);
    const int edits = changed.size();
    for (const auto &bytes : invalid) {
        setNative(bytes);
        const int failures = errors.size();
        trigger(destination, "paste");
        CHECK(errors.size() == failures + 1 && destination.toJson() == after);
        CHECK(destination.selectedNodeId() == selected && changed.size() == edits);
        CHECK(destination.canUndo() && destination.canRedo());
    }
    setNative(payload);
    destination.setReadOnly(true);
    CHECK(!editAction(destination, "paste").isEnabled());
    editAction(destination, "paste").trigger();
    CHECK(destination.toJson() == after);
    destination.setReadOnly(false);
    destination.setEnabled(false);
    CHECK(!editAction(destination, "paste").isEnabled());
    destination.setEnabled(true);
    destination.clearSelection();
    CHECK(!editAction(destination, "paste").isEnabled());

    // Groups are copied in document order, with selected descendant overlaps removed.
    source.setReadOnly(false);
    CHECK(source.setExpanded(QStringLiteral("a"), true));
    CHECK(source.selectNode(QStringLiteral("x")));
    QTest::mouseClick(graphics(source).viewport(), Qt::LeftButton, Qt::ControlModifier,
                      labelPoint(source, QStringLiteral("Alpha")));
    QTest::mouseClick(graphics(source).viewport(), Qt::LeftButton, Qt::ControlModifier,
                      labelPoint(source, QStringLiteral("Orphan")));
    CHECK(source.selectedNodeIds().size() == 3);
    shortcut(source, Qt::Key_C, Qt::ControlModifier);
    RawLabelEditor group(config);
    showEditor(group);
    shortcut(group, Qt::Key_V, Qt::ControlModifier);
    const Json forest = exported(group);
    const auto roots = record(forest, "nodes", QStringLiteral("root")).at("children");
    CHECK(roots.size() == 2 && forest.at("nodes").size() == 6 && forest.at("crossLinks").size() == 2);
    CHECK(record(forest, "nodes", qs(roots[0])).at("topic") == "Alpha");
    CHECK(record(forest, "nodes", qs(roots[1])).at("topic") == "Orphan");
    CHECK(!forest.contains("orphanIds")); // Copied independent roots become children of the destination.
    CHECK(source.selectNode(QStringLiteral("r")));
    shortcut(source, Qt::Key_C, Qt::ControlModifier);
    CHECK(group.selectNode(QStringLiteral("root")));
    shortcut(group, Qt::Key_V, Qt::ControlModifier);
    CHECK(record(exported(group), "nodes", group.selectedNodeId()).at("topic") == "Source");

    // Native text controls and link Copy never publish a node forest.
    CHECK(source.selectLink(QStringLiteral("inside")));
    shortcut(source, Qt::Key_C, Qt::ControlModifier);
    CHECK(QApplication::clipboard()->text() == QStringLiteral("Internal"));
    CHECK(!QApplication::clipboard()->mimeData()->hasFormat(format));
    CHECK(source.selectNode(QStringLiteral("a")));
    expandProperties(source);
    auto *url = source.findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
    CHECK(url);
    url->setFocus(); url->selectAll(); pump();
    editAction(source, "copy").trigger();
    CHECK(QApplication::clipboard()->text() == QStringLiteral("https://example.test/topic"));
    CHECK(!QApplication::clipboard()->mimeData()->hasFormat(format));
    shortcut(source, Qt::Key_F2);
    topicInput(source).setPlainText(QStringLiteral("Uncommitted draft"));
    topicInput(source).selectAll();
    editAction(source, "copy").trigger();
    CHECK(QApplication::clipboard()->text() == QStringLiteral("Uncommitted draft"));
    CHECK(!QApplication::clipboard()->mimeData()->hasFormat(format));
    CHECK(source.hasPendingEdit() && source.toJson() == sourceBefore);
    topicKey(source, Qt::Key_Escape);

    // Host callbacks may close or replace the destination during publication.
    for (bool destroy : {false, true}) {
        QPointer<Editor> closing = new RawLabelEditor(config);
        CHECK(closing->addNode(QStringLiteral("root"), QStringLiteral("Existing")) != QString());
        CHECK(closing->setExpanded(QStringLiteral("root"), false));
        CHECK(closing->selectNode(QStringLiteral("root")));
        showEditor(*closing);
        setNative(payload);
        pump();
        CHECK(editAction(*closing, "paste").isEnabled());
        bool handled = false;
        QObject::connect(closing, &Editor::documentChanged, qApp, [&] {
            if (handled) return;
            handled = true;
            if (destroy) delete closing.data();
            else CHECK(closing->newDocument(QStringLiteral("Replacement")));
        });
        editAction(*closing, "paste").trigger();
        CHECK(handled);
        if (destroy) CHECK(!closing);
        else {
            CHECK(closing->selectedNodeId() == QStringLiteral("root"));
            CHECK(exported(*closing).at("nodes").size() == 1);
            CHECK(record(exported(*closing), "nodes", QStringLiteral("root")).at("topic") == "Replacement");
            delete closing.data();
        }
    }

    // A lazy native MIME provider may reenter the host while its bytes are retrieved.
    class ClipboardProvider : public QMimeData {
    public:
        mutable std::function<void()> beforeRead;
    protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QVariant retrieveData(const QString &type, QMetaType preferred) const override {
#else
        QVariant retrieveData(const QString &type, QVariant::Type preferred) const override {
#endif
            if (type == QStringLiteral("application/x-m3-nodes+json") && beforeRead) {
                auto callback = std::move(beforeRead);
                callback();
            }
            return QMimeData::retrieveData(type, preferred);
        }
    };
    for (bool destroy : {false, true}) {
        QPointer<Editor> target = new RawLabelEditor(config);
        showEditor(*target);
        auto *provider = new ClipboardProvider;
        provider->setData(format, payload);
        QApplication::clipboard()->setMimeData(provider);
        pump();
        bool retrieved = false;
        provider->beforeRead = [&] {
            retrieved = true;
            if (destroy) delete target.data();
            else CHECK(target->newDocument(QStringLiteral("Replaced while reading clipboard")));
        };
        editAction(*target, "paste").trigger();
        CHECK(retrieved);
        if (destroy) CHECK(!target);
        else {
            CHECK(exported(*target).at("nodes").size() == 1);
            CHECK(record(exported(*target), "nodes", QStringLiteral("root")).at("topic") == "Replaced while reading clipboard");
            delete target.data();
        }
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
        expandProperties(editor);
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
        RawLabelEditor editor;
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
        const QPoint expansion = expansionPoint(editor, alpha);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, expansion);
        pump();
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty() && indicator(editor, alpha) != nullptr);
        CHECK(activated.size() == 2 && activeTopicInput(editor) == nullptr);
    }
    {
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        auto editor = std::make_unique<RawLabelEditor>();
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
    class RelativeEditor : public RawLabelEditor {
    public:
        using RawLabelEditor::RawLabelEditor;
        QStringList received;
        bool veto = false;
    protected:
        void insertFromMimeData(const QString &nodeId, const QMimeData *source, MimeInsertionSource origin) override {
            CHECK(origin == MimeInsertionSource::Drop);
            const QString filePath = source->urls().front().toLocalFile();
            received.append(filePath);
            if (veto) return;
            // Preserve literal # and % in the relative URL reference.
            const QString url = QUrl::fromLocalFile(QDir(resourceBasePath()).relativeFilePath(filePath)).path(QUrl::FullyEncoded);
            CHECK(updateNode(nodeId, encoded(Json{{"hyperLink", utf8(url)}})));
        }
    };
    {
        RawLabelEditor standalone;
        prepare(standalone, false);
        auto *clipboard = new QMimeData;
        clipboard->setUrls({QUrl::fromLocalFile(path)});
        clipboard->setImageData(QImage(8, 8, QImage::Format_ARGB32));
        QApplication::clipboard()->setMimeData(clipboard);
        CHECK(!editAction(standalone, "paste").isEnabled());
        const Json before = exported(standalone);
        standalone.setReadOnly(true);
        enterFile(standalone, localFile, actions, labelPoint(standalone, alpha), false);
        CHECK(exported(standalone) == before);
    }
    {
        MimeEditor editor;
        prepare(editor, false);
        CHECK(editor.selectNode(b));
        const Json before = exported(editor);
        auto &view = graphics(editor);
        const QTransform camera = view.transform();
        QMimeData mixed;
        mixed.setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(missingPath)});
        mixed.setImageData(QImage(8, 8, QImage::Format_ARGB32));
        enterFile(editor, mixed, actions, blankPoint(view));
        moveFile(editor, mixed, actions, labelPoint(editor, alpha));
        CHECK(editor.targets.isEmpty() && exported(editor) == before);
        dropFile(editor, mixed, actions, labelPoint(editor, alpha));
        CHECK(editor.targets == QStringList{a} && editor.origins.front() == Editor::MimeInsertionSource::Drop);
        CHECK(exported(editor) == withUrl(before, a, fileUrl));
        CHECK(editor.selectedNodeId() == b && view.transform() == camera);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, labelPoint(editor, alpha));
        CHECK(editor.selectedNodeIds().size() == 2);
        enterFile(editor, mixed, actions, labelPoint(editor, alpha), false);
        CHECK(editor.targets.size() == 1);
    }
    {
        MimeEditor editor;
        prepare(editor, false);
        CHECK(editor.selectNode(b));
        const QString url = QStringLiteral("https://example.test/pasted%20image.png");
        QApplication::clipboard()->setText(url);
        CHECK(editAction(editor, "paste").isEnabled());
        CHECK(editAction(editor, "paste").shortcuts().contains(QKeySequence(QKeySequence::Paste)));
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selection(&editor, &Editor::selectionChanged);
        shortcut(editor, Qt::Key_V, Qt::ControlModifier);
        CHECK(editor.targets == QStringList{b} && editor.origins.front() == Editor::MimeInsertionSource::Clipboard);
        CHECK(exported(editor) == withUrl(before, b, url));
        CHECK(changed.size() == 1 && selection.isEmpty());
        CHECK(editor.undo() && exported(editor) == before);
        CHECK(editor.redo() && exported(editor) == withUrl(before, b, url));
        QApplication::clipboard()->setText(QStringLiteral("ordinary canvas text"));
        CHECK(!editAction(editor, "paste").isEnabled());
        QApplication::clipboard()->setText(url);
        QSignalSpy policy(&editor, &Editor::readOnlyChanged);
        QObject::connect(&editor, &Editor::readOnlyChanged, &editor, [&](bool readOnly) {
            CHECK(editor.isReadOnly() == readOnly);
            CHECK(editAction(editor, "paste").isEnabled() == !readOnly);
        });
        editor.setReadOnly(true);
        editor.setReadOnly(true);
        CHECK(policy.size() == 1 && policy.front().front().toBool());
        editor.setReadOnly(false);
        CHECK(policy.size() == 2 && !policy.back().front().toBool());
        editor.setEnabled(false);
        CHECK(!editAction(editor, "paste").isEnabled());
        editor.setEnabled(true);
        CHECK(editAction(editor, "paste").isEnabled());
        editor.clearSelection();
        CHECK(!editAction(editor, "paste").isEnabled());
        CHECK(editor.selectLink(QStringLiteral("l1")));
        CHECK(!editAction(editor, "paste").isEnabled());
        CHECK(editor.selectNode(a));
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::ControlModifier,
                          labelPoint(editor, QStringLiteral("Beta")));
        CHECK(editor.selectedNodeIds().size() == 2 && !editAction(editor, "paste").isEnabled());
        CHECK(editor.selectNode(a));
        auto &view = graphics(editor);
        const QPoint point = labelPoint(editor, alpha);
        bool visited = false;
        std::exception_ptr failure;
        // Resolve the hit point before arming the responder: labelPoint pumps events.
        QTimer::singleShot(0, &editor, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            try {
                CHECK(menu && menu->actions().contains(&editAction(editor, "paste")));
                visited = true;
            } catch (...) { failure = std::current_exception(); }
            if (menu) menu->close();
        });
        QContextMenuEvent menu(QContextMenuEvent::Mouse, point, view.viewport()->mapToGlobal(point));
        QCoreApplication::sendEvent(view.viewport(), &menu);
        if (failure) std::rethrow_exception(failure);
        CHECK(visited);
    }
    for (bool destroy : {false, true}) {
        QPointer<MimeEditor> editor = new MimeEditor;
        prepare(*editor, false);
        QMimeData remote;
        remote.setText(QStringLiteral("https://example.test/from-drop"));
        bool inserted = false;
        editor->insertion = [&] {
            inserted = true;
            if (destroy) delete editor.data();
            else CHECK(editor->newDocument(QStringLiteral("Replacement from MIME hook")));
        };
        const QPoint point = labelPoint(*editor, alpha);
        enterFile(*editor, remote, actions, point);
        dropFile(*editor, remote, actions, point);
        CHECK(inserted);
        if (destroy) CHECK(editor.isNull());
        else {
            CHECK(editor->selectedNodeId() == QStringLiteral("root"));
            CHECK(record(exported(*editor), "nodes", QStringLiteral("root")).at("topic") == "Replacement from MIME hook");
            delete editor.data();
        }
    }
    {
        MimeEditor editor;
        prepare(editor, false);
        CHECK(editor.selectNode(a));
        bool inserted = false;
        editor.insertion = [&] { inserted = true; };
        QImage pixels(8, 8, QImage::Format_ARGB32);
        pixels.fill(Qt::red);
        QApplication::clipboard()->setImage(pixels);
        CHECK(editAction(editor, "paste").isEnabled());
        shortcut(editor, Qt::Key_V, Qt::ControlModifier);
        CHECK(inserted && editor.targets == QStringList{a});
        CHECK(editor.origins.front() == Editor::MimeInsertionSource::Clipboard);
        QMimeData image;
        image.setImageData(pixels);
        enterFile(editor, image, actions, labelPoint(editor, alpha));
        dropFile(editor, image, actions, labelPoint(editor, alpha));
        CHECK(editor.origins.size() == 2 && editor.origins.back() == Editor::MimeInsertionSource::Drop);
    }
    {
        RawLabelEditor editor;
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
        RawLabelConfig config;
        config.resourceBasePath = directory.path();
        RelativeEditor editor(config);
        prepare(editor, false);
        CHECK(editor.selectNode(a));
        // Loading, ordinary field edits, and activation must bypass MIME insertion.
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
        const QString relative = QUrl::fromLocalFile(QDir(directory.path()).relativeFilePath(path)).path(QUrl::FullyEncoded);
        CHECK(editor.received == QStringList{path});
        CHECK(exported(editor) == withUrl(before, a, relative));
        CHECK(changed.size() == 1 && selected.isEmpty() && activated.isEmpty() && errors.isEmpty());
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, linkPoint(editor, alpha));
        CHECK(activated.size() == 1 && activated.back().at(0).toString() == a && activated.back().at(1).toString() == fileUrl);
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
        RawLabelConfig config;
        config.resourceBasePath = directory.path();
        RelativeEditor editor(config);
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
        const QString root = QStringLiteral("r");
        const QString relativeUrl = QUrl::fromLocalFile(QDir(directory.path()).relativeFilePath(path)).path(QUrl::FullyEncoded);
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
        RawLabelEditor editor;
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
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor, QSize(1100, 900));
    CHECK(editor.selectNode(QStringLiteral("a")));
    expandProperties(editor);
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
        RawLabelEditor other;
        showEditor(other);
        expandProperties(other);
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
            // IconMode horizontal navigation follows model order, including a one-column grid.
            // Font and DPI determine whether the next item is beside or below the first.
            const QModelIndex first = choices->model()->index(0, 0), next = choices->model()->index(1, 0);
            choices->setCurrentIndex(first);
            choices->scrollToTop();
            const QRect firstCell = choices->visualRect(first);
            int belowRow = 1;
            while (belowRow < choices->model()->rowCount() &&
                   choices->visualRect(choices->model()->index(belowRow, 0)).top() == firstCell.top()) ++belowRow;
            CHECK(belowRow < choices->model()->rowCount());
            const QModelIndex below = choices->model()->index(belowRow, 0);
            CHECK(choices->visualRect(below).left() == firstCell.left());
            CHECK(navigate(Qt::Key_L) == next);
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
    // Let the native popup finish opening before measuring its click position.
    QTest::qWait(QApplication::doubleClickInterval() + 1);
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
    CHECK(QTest::qWaitFor([&] {
        return popup->isVisible() && !categories->view()->isVisible() && icons->hasFocus();
    }, 1000));
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

static void emoji_productivity_case() {
    RawLabelEditor editor;
    showEditor(editor, QSize(1100, 900));
    expandProperties(editor);
    auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
    auto *icons = panel->findChild<QLineEdit *>(QStringLiteral("nodeIcons"));
    panel->findChild<QScrollArea *>()->ensureWidgetVisible(icons);
    icons->setFocus();
    pump();
    auto *categories = icons->findChild<QComboBox *>(QStringLiteral("emojiCategories"));
    auto *choices = icons->findChild<QListView *>(QStringLiteral("emojiChoices"));
    CHECK(categories && choices);
    categories->setCurrentIndex(0);
    icons->clear();
    pump();
    auto rowOf = [&](const char *name) {
        for (int row = 0; row < choices->model()->rowCount(); ++row)
            if (choices->model()->index(row, 0).data().toString() == QLatin1String(name)) return row;
        return -1;
    };
    const int ordinary = rowOf("red apple");
    CHECK(ordinary >= 0);
    for (const auto *name : {"stopwatch", "rocket", "briefcase", "check mark button", "notebook",
                             "star", "red circle", "keycap: 0", "keycap: 10", "printer",
                             "office worker", "technologist", "toolbox"}) {
        CHECK(rowOf(name) >= 0 && rowOf(name) < ordinary);
    }
    // The curated prefix is a permutation, not a second copy of the catalog.
    std::set<QString> allGlyphs, categoryGlyphs;
    for (int row = 0; row < choices->model()->rowCount(); ++row) {
        CHECK(allGlyphs.insert(choices->model()->index(row, 0).data(Qt::UserRole + 1).toString()).second);
    }
    for (int category = 1; category < categories->count(); ++category) {
        categories->setCurrentIndex(category);
        for (int row = 0; row < choices->model()->rowCount(); ++row)
            categoryGlyphs.insert(choices->model()->index(row, 0).data(Qt::UserRole + 1).toString());
    }
    CHECK(allGlyphs == categoryGlyphs);
    const int objects = categories->findText(QStringLiteral("Objects"));
    CHECK(objects >= 0);
    categories->setCurrentIndex(objects);
    CHECK(rowOf("notebook") >= 0 && rowOf("notebook") < rowOf("clipboard"));
    categories->setCurrentIndex(0);
    for (const auto &item : std::initializer_list<std::pair<const char *, const char *>>{
             {"efficiency", "stopwatch"}, {"project", "rocket"}, {"management", "briefcase"},
             {"todo", "check mark button"}, {"diary", "notebook"}, {"importance", "star"},
             {"priority", "red circle"}, {"urgent", "police car light"}, {"number", "keycap: 0"},
             {"office", "printer"}, {"office stationery", "linked paperclips"},
             {"engineer", "technologist"}, {"engineering electrical", "electric plug"}}) {
        icons->setText(QString::fromLatin1(item.first));
        pump();
        CHECK(rowOf(item.second) >= 0 && rowOf("red apple") < 0);
    }
    icons->setText(QStringLiteral("  OFFICE   equipment  "));
    pump();
    CHECK(rowOf("printer") >= 0 && rowOf("office building") < 0);
    icons->setText(QStringLiteral("office"));
    pump();
    CHECK(rowOf("office building") >= 0 && rowOf("office worker") >= 0);
    categories->setCurrentIndex(objects);
    CHECK(rowOf("printer") >= 0 && rowOf("office building") < 0 && rowOf("office worker") < 0);
    categories->setCurrentIndex(0);
    icons->setText(QStringLiteral("  ENGINEERING   electrical  "));
    pump();
    CHECK(rowOf("electric plug") >= 0 && rowOf("technologist") < 0);
    const int people = categories->findText(QStringLiteral("People & Body"));
    CHECK(people >= 0);
    icons->setText(QStringLiteral("engineer"));
    pump();
    categories->setCurrentIndex(people);
    CHECK(rowOf("technologist") >= 0 && rowOf("electric plug") < 0);
    categories->setCurrentIndex(0);
    icons->setText(QStringLiteral("priority HIGH"));
    pump();
    CHECK(rowOf("red circle") >= 0 && rowOf("green circle") < 0);
    icons->setText(QStringLiteral("priority low"));
    pump();
    CHECK(rowOf("green circle") >= 0 && rowOf("red circle") < 0);
    icons->setText(QStringLiteral("priority number"));
    pump();
    QStringList numbers;
    for (int number = 0; number < 10; ++number)
        numbers.append(QString(QChar(ushort('0' + number))) + QStringLiteral("\uFE0F\u20E3"));
    numbers.append(QString::fromUtf8("\xF0\x9F\x94\x9F"));
    CHECK(choices->model()->rowCount() == numbers.size());
    for (int number = 0; number < numbers.size(); ++number)
        CHECK(choices->model()->index(number, 0).data(Qt::UserRole + 1).toString() == numbers[number]);
    choices->setCurrentIndex(choices->model()->index(10, 0));
    QTest::keyClick(icons, Qt::Key_Return);
    pump();
    CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("icons") == Json::array({utf8(numbers[10])}));
    icons->setText(QStringLiteral("  ToDo   PeNdInG  "));
    pump();
    CHECK(rowOf("hourglass not done") >= 0 && rowOf("check mark button") < 0);
    categories->setCurrentIndex(objects);
    CHECK(choices->model()->rowCount() == 0);
    icons->setText(QStringLiteral("diary"));
    pump();
    CHECK(rowOf("notebook") >= 0 && rowOf("red heart") < 0);
    categories->setCurrentIndex(0);
    icons->setText(QStringLiteral("todo complete"));
    pump();
    const int check = rowOf("check box with check");
    CHECK(check >= 0);
    choices->setCurrentIndex(choices->model()->index(check, 0));
    QTest::keyClick(icons, Qt::Key_Return);
    pump();
    const QString glyph = QString::fromUtf8("\xE2\x98\x91\xEF\xB8\x8F");
    CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("icons") == Json::array({utf8(glyph)}));
    RawLabelEditor reopened;
    CHECK(reopened.loadJson(editor.toJson()));
    CHECK(record(exported(reopened), "nodes", QStringLiteral("root")).at("icons") == Json::array({utf8(glyph)}));
    icons->setText(glyph);
    pump();
    CHECK(rowOf("check box with check") >= 0);
    icons->setText(QStringLiteral("red apple"));
    pump();
    CHECK(rowOf("red apple") >= 0 && rowOf("notebook") < 0);
}

static void properties_visibility_case() {
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor, QSize(1100, 900));
    expandProperties(editor);
    shortcut(editor, Qt::Key_0, Qt::ControlModifier);
    auto &view = graphics(editor);
    auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
    auto *toggle = editor.findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
    auto *tags = editor.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
    CHECK(panel && toggle && tags && panel->isVisible() && toggle->isChecked());
    CHECK(view.transform().m11() == 1);
    auto panelRect = [&] {
        return QRectF(QRect(view.viewport()->mapFromGlobal(panel->mapToGlobal(QPoint())), panel->size()));
    };
    const QRectF expandedPanel = panelRect();
    auto mappedNode = [&](const QString &topic) { return view.viewportTransform().mapRect(topicRect(editor, topic)); };
    auto envelope = [&](const QString &topic) {
        auto *node = ownerItem(textItem(editor, topic));
        QRectF bounds = mappedNode(topic);
        for (auto *child : node->childItems()) {
            if (child->isVisible())
                bounds = bounds.united(child->deviceTransform(view.viewportTransform()).mapRect(child->boundingRect()));
        }
        return bounds;
    };
    auto center = [&] { return view.mapToScene(view.viewport()->rect().center()); };
    auto bars = [&] { return QPoint(view.horizontalScrollBar()->value(), view.verticalScrollBar()->value()); };
    auto place = [&](const QString &topic, const QPointF &desired) {
        view.setSceneRect(view.sceneRect().adjusted(-2000, -2000, 2000, 2000));
        const QRectF mapped = mappedNode(topic);
        view.horizontalScrollBar()->setValue(view.horizontalScrollBar()->value() + qRound(mapped.left() - desired.x()));
        view.verticalScrollBar()->setValue(view.verticalScrollBar()->value() + qRound(mapped.top() - desired.y()));
        pump();
    };
    auto cover = [&](const QString &topic) {
        place(topic, expandedPanel.topLeft() + QPointF(40, 100));
        CHECK(QRectF(view.viewport()->rect()).contains(mappedNode(topic)));
        CHECK(mappedNode(topic).intersects(expandedPanel));
    };
    auto revealed = [&](const QString &topic) {
        CHECK(panel->isVisible() && toggle->isChecked());
        CHECK(QRectF(view.viewport()->rect()).contains(mappedNode(topic)));
        CHECK(!mappedNode(topic).intersects(panelRect().adjusted(-8, -8, 8, 8)));
    };
    QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
    auto action = [&](auto invoke, const QString &node, const QString &link, int selectionDelta, QWidget *focus) {
        const Json before = exported(editor);
        const QTransform zoom = view.transform();
        const QString oldNode = editor.selectedNodeId(), oldLink = editor.selectedLinkId();
        const auto changes = changed.size(), selections = selected.size();
        QWidget *oldFocus = QApplication::focusWidget();
        invoke(); pump();
        CHECK(exported(editor) == before && changed.size() == changes && view.transform() == zoom);
        CHECK(editor.selectedNodeId() == node && editor.selectedLinkId() == link);
        CHECK(selected.size() == selections + selectionDelta);
        if (!selectionDelta) CHECK(editor.selectedNodeId() == oldNode && editor.selectedLinkId() == oldLink);
        else CHECK(selected.back().at(0).toString() == node && selected.back().at(1).toString() == link);
        CHECK(QApplication::focusWidget() == (focus ? focus : oldFocus));
    };
    auto collapse = [&] {
        const QPointF beforeCenter = center();
        const QPoint beforeBars = bars();
        action([&] { shortcut(editor, Qt::Key_P); }, editor.selectedNodeId(), {}, 0, &view);
        CHECK(!toggle->isChecked() && panel->isVisible());
        CHECK(center() == beforeCenter && bars() == beforeBars);
    };
    action([&] { CHECK(editor.selectLink(QStringLiteral("l1"))); }, {}, QStringLiteral("l1"), 1, nullptr);
    CHECK(!panel->isVisible());
    cover(QStringLiteral("Alpha"));
    action([&] { CHECK(editor.selectNode(QStringLiteral("a"))); }, QStringLiteral("a"), {}, 1, nullptr);
    revealed(QStringLiteral("Alpha"));

    collapse();
    cover(QStringLiteral("Alpha"));
    action([&] { shortcut(editor, Qt::Key_P); }, QStringLiteral("a"), {}, 0, &view);
    revealed(QStringLiteral("Alpha"));
    collapse();
    cover(QStringLiteral("Alpha"));
    action([&] { shortcut(editor, Qt::Key_T); }, QStringLiteral("a"), {}, 0, tags);
    revealed(QStringLiteral("Alpha"));

    view.setFocus(); pump();
    cover(QStringLiteral("Beta"));
    action([&] { CHECK(editor.selectNode(QStringLiteral("b"))); }, QStringLiteral("b"), {}, 1, nullptr);
    revealed(QStringLiteral("Beta"));
    collapse();
    place(QStringLiteral("Beta"), QPointF(100, 150));
    CHECK(QRectF(view.viewport()->rect()).contains(envelope(QStringLiteral("Beta"))));
    CHECK(!envelope(QStringLiteral("Beta")).intersects(expandedPanel.adjusted(-8, -8, 8, 8)));
    const QPointF clearCenter = center();
    const QPoint clearBars = bars();
    action([&] { shortcut(editor, Qt::Key_P); }, QStringLiteral("b"), {}, 0, &view);
    revealed(QStringLiteral("Beta"));
    CHECK(center() == clearCenter && bars() == clearBars);

    // A shown panel is not a permanent constraint on intentional user pan.
    cover(QStringLiteral("Beta"));
    const QPointF pannedCenter = center();
    const QPoint pannedBars = bars();
    action([&] {
        QEvent layout(QEvent::LayoutRequest);
        QCoreApplication::sendEvent(&view, &layout);
    }, QStringLiteral("b"), {}, 0, nullptr);
    CHECK(center() == pannedCenter && bars() == pannedBars);
    CHECK(mappedNode(QStringLiteral("Beta")).intersects(panelRect()));
    collapse();

    // Topic wrapping alone is not enough: this node must fit no clear rectangle.
    const QString oversized = QStringLiteral("Oversized topic\n").repeated(100);
    CHECK(editor.renameNode(QStringLiteral("b"), oversized));
    pump();
    place(oversized, expandedPanel.topLeft() + QPointF(40, 100));
    CHECK(mappedNode(oversized).intersects(expandedPanel));
    const QRectF viewportRect(view.viewport()->rect());
    const QRectF obstruction = expandedPanel.intersected(viewportRect).adjusted(-8, -8, 8, 8).intersected(viewportRect);
    const QRectF candidates[] = {
        {viewportRect.left(), viewportRect.top(), obstruction.left() - viewportRect.left(), viewportRect.height()},
        {viewportRect.left(), obstruction.bottom(), viewportRect.width(), viewportRect.bottom() - obstruction.bottom()},
        {obstruction.right(), viewportRect.top(), viewportRect.right() - obstruction.right(), viewportRect.height()},
        {viewportRect.left(), viewportRect.top(), viewportRect.width(), obstruction.top() - viewportRect.top()}
    };
    qreal maximumArea = 0;
    for (const QRectF &clear : candidates) {
        if (clear.isEmpty()) continue;
        const QRectF node = mappedNode(oversized);
        CHECK(node.width() > clear.width() || node.height() > clear.height());
        maximumArea = std::max(maximumArea, std::min(node.width(), clear.width()) * std::min(node.height(), clear.height()));
    }
    CHECK(maximumArea > 0);
    action([&] { shortcut(editor, Qt::Key_P); }, QStringLiteral("b"), {}, 0, &view);
    CHECK(panel->isVisible() && toggle->isChecked() && panelRect() == expandedPanel);
    qreal exposedArea = 0;
    for (const QRectF &clear : candidates) {
        const QRectF visible = mappedNode(oversized).intersected(clear);
        if (!visible.isEmpty()) exposedArea = std::max(exposedArea, visible.width() * visible.height());
    }
    CHECK(std::abs(exposedArea - maximumArea) < 1);
    const QPointF settledCenter = center();
    const QPoint settledBars = bars();
    pump(); pump();
    CHECK(center() == settledCenter && bars() == settledBars);
    action([&] { editor.clearSelection(); }, {}, {}, 1, nullptr);
    CHECK(!panel->isVisible() && center() == settledCenter && bars() == settledBars);
    action([&] { CHECK(editor.selectNode(QStringLiteral("b"))); }, QStringLiteral("b"), {}, 1, nullptr);
    const QPointF beforeLinkCenter = center();
    const QPoint beforeLinkBars = bars();
    action([&] { CHECK(editor.selectLink(QStringLiteral("l1"))); }, {}, QStringLiteral("l1"), 1, nullptr);
    CHECK(!panel->isVisible() && center() == beforeLinkCenter && bars() == beforeLinkBars);
}

static void branch_color_case() {
    const Json input = Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[
        {"id":"r","topic":"Root","children":["a","b"]},
        {"id":"a","topic":"Alpha","children":["a1","a2"],
         "style":{"color":"#34495e","background":"#ecf0f1","custom":{"keep":true}}},
        {"id":"a1","topic":"Nested","children":["a1x"]},
        {"id":"a1x","topic":"Leaf"},{"id":"a2","topic":"Sibling"},
        {"id":"b","topic":"Beta"}],"crossLinks":[
        {"id":"cross","source":"a2","target":"b","directed":false,"topic":"Cross"}]})");
    RawLabelConfig config;
    config.autoRandomBranchColor = false;
    config.initialVisibleLevels = 0;
    RawLabelEditor editor(config);
    htmlAppearance(editor);
    CHECK(editor.loadJson(encoded(input)));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor, QSize(1100, 800));
    auto &view = graphics(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
    const QString red = QStringLiteral("Red (#e74c3c)"), blue = QStringLiteral("Blue (#3498db)");
    const QString green = QStringLiteral("Green (#2ecc71)"), orange = QStringLiteral("Orange (#e67e22)");
    const QString automatic = QStringLiteral("Auto");
    const QColor redColor("#e74c3c"), blueColor("#3498db"), greenColor("#2ecc71"), orangeColor("#e67e22");
    auto menu = [&](const QString &id, const QString &choice, const QString &checked, bool keyboard = false) {
        if (!id.isEmpty()) CHECK(editor.selectNode(id));
        pump();
        const QString selected = editor.selectedNodeId(), link = editor.selectedLinkId();
        const QTransform zoom = view.transform();
        const bool hasNode = selected.isEmpty() == false;
        const QPoint point = hasNode ? labelPoint(editor, qs(record(exported(editor), "nodes", selected).at("topic"))) : blankPoint(view);
        std::exception_ptr failure;
        QPointer<QMenu> parentMenu, submenu;
        bool visited = false;
        QTimer dismiss;
        dismiss.setSingleShot(true);
        QObject::connect(&dismiss, &QTimer::timeout, &editor, [&] {
            if (submenu) submenu->close();
            if (parentMenu) parentMenu->close();
            if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) popup->close();
        });
        dismiss.start(5000);
        QTimer::singleShot(0, &editor, [&] {
            parentMenu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            try {
                CHECK(parentMenu);
                pump();
                if (!hasNode) {
                    for (auto *action : parentMenu->actions()) CHECK(action->text() != QStringLiteral("Branch Color"));
                } else {
                    auto &action = textAction(*parentMenu, {QStringLiteral("Branch Color")});
                    submenu = action.menu();
                    CHECK(submenu);
                    parentMenu->setActiveAction(&action);
                    QTest::keyClick(parentMenu, Qt::Key_Right);
                    if (!QTest::qWaitFor([&] { return submenu && submenu->isVisible(); }, 1000))
                        submenu->popup(parentMenu->mapToGlobal(parentMenu->actionGeometry(&action).topRight()));
                    CHECK(QTest::qWaitFor([&] { return submenu && submenu->isVisible(); }, 1000));
                    const auto actions = submenu->actions();
                    CHECK(actions.size() == 24 && actions.front()->text() == automatic);
                    QStringList paletteLabels{automatic};
                    for (auto *button : editor.findChildren<QToolButton *>())
                        if (button->property("color").isValid()) paletteLabels.append(button->accessibleName());
                    CHECK(paletteLabels.size() == actions.size());
                    for (qsizetype i = 0; i < actions.size(); ++i) {
                        CHECK(actions[i]->text() == paletteLabels[i]);
                        CHECK(actions[i]->isCheckable() && actions[i]->isChecked() == (actions[i]->text() == checked));
                    }
                    if (!choice.isEmpty()) {
                        auto &picked = textAction(*submenu, {choice});
                        if (keyboard) {
                            submenu->setActiveAction(actions.front());
                            for (qsizetype i = 0; i < actions.indexOf(&picked); ++i) QTest::keyClick(submenu, Qt::Key_Down);
                            CHECK(submenu->activeAction() == &picked);
                            QTest::keyClick(submenu, Qt::Key_Return);
                        } else QTest::mouseClick(submenu, Qt::LeftButton, Qt::NoModifier, submenu->actionGeometry(&picked).center());
                    }
                }
                visited = true;
            } catch (...) { failure = std::current_exception(); }
            if (submenu) submenu->close();
            if (parentMenu) parentMenu->close();
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view.viewport()->mapToGlobal(point));
        QCoreApplication::sendEvent(view.viewport(), &event);
        dismiss.stop();
        pump();
        if (failure) std::rethrow_exception(failure);
        CHECK(visited && editor.selectedNodeId() == selected && editor.selectedLinkId() == link);
        CHECK(view.transform() == zoom);
    };
    auto raster = [&](const QRectF &source) {
        QImage image(QSize(int(std::ceil(source.width() * 4)), int(std::ceil(source.height() * 4))), QImage::Format_ARGB32_Premultiplied);
        image.fill(view.palette().color(QPalette::Base));
        QPainter painter(&image);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
        view.scene()->render(&painter, QRectF(QPointF(), image.size()), source, Qt::IgnoreAspectRatio);
        CHECK(painter.end());
        return image;
    };
    auto matches = [](QRgb pixel, const QColor &color) {
        return std::abs(qRed(pixel) - color.red()) <= 2 && std::abs(qGreen(pixel) - color.green()) <= 2 &&
               std::abs(qBlue(pixel) - color.blue()) <= 2;
    };
    auto colorPixels = [&](const QImage &image, const QColor &color) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            if (matches(image.pixel(x, y), color)) ++count;
        return count;
    };
    auto atColor = [&](QPointF point, const QColor &color) {
        CHECK(colorPixels(raster(QRectF(point - QPointF(2, 2), QSizeF(4, 4))), color) >= 4);
    };
    auto borderRegion = [&](const QString &topic) {
        // Node scene bounds include two units of padding around the painted rect.
        const QRectF rect = topicRect(editor, topic).adjusted(2, 2, -2, -2);
        return QRectF(rect.left() + 12, rect.top() - 2, rect.width() - 24, 4);
    };
    auto border = [&](const QString &topic, const QColor &color) {
        const QRectF region = borderRegion(topic);
        atColor(region.center(), color);
    };
    auto edge = [&](const QString &topic, const QColor &color) {
        const QRectF child = topicRect(editor, topic).adjusted(2, 2, -2, -2);
        bool found = false;
        for (auto *item : view.scene()->items()) {
            auto *pathItem = dynamic_cast<QGraphicsPathItem *>(item);
            if (!pathItem || pathItem->parentItem()) continue;
            const QPainterPath path = pathItem->mapToScene(pathItem->path());
            if (!child.adjusted(-0.1, -0.1, 0.1, 0.1).contains(path.pointAtPercent(1))) continue;
            QPointF point = path.pointAtPercent(0.7);
            if (editor.layoutDirection() == Editor::LayoutDirection::Outline) {
                const auto elbow = path.elementAt(path.elementCount() - 2);
                point = (QPointF(elbow.x, elbow.y) + path.pointAtPercent(1)) / 2;
            }
            atColor(point, color);
            found = true;
        }
        CHECK(found);
    };
    auto branch = [&](const QString &topic, const QColor &color) { border(topic, color); edge(topic, color); };
    auto localStyle = [&](const QString &id) { return record(exported(editor), "nodes", id).at("style"); };
    auto crossImage = [&] {
        auto *cross = ownerItem(textItem(editor, QStringLiteral("Cross")));
        QList<QGraphicsItem *> hidden;
        for (auto *item : view.scene()->items()) if (!item->parentItem() && item != cross && item->isVisible()) {
            item->hide(); hidden.append(item);
        }
        const QImage image = raster(cross->sceneBoundingRect());
        for (auto *item : hidden) item->show();
        return image;
    };
    editor.clearSelection();
    const QRectF originalBounds = view.scene()->itemsBoundingRect();
    const QImage originalMap = raster(originalBounds);
    const QImage originalRoot = raster(borderRegion("Root"));
    const QImage originalBeta = raster(borderRegion("Beta"));
    const QImage originalCross = crossImage();
    const Json before = exported(editor);
    menu("a", red, automatic, true);
    CHECK(changed.size() == 1);
    Json expected = before;
    for (auto &node : expected["nodes"]) if (node["id"] == "a") node["style"]["branchColor"] = "#e74c3c";
    CHECK(exported(editor) == expected);
    menu("a", red, red);
    menu("a", {}, red);
    CHECK(changed.size() == 1 && exported(editor) == expected);
    const int selectedPixels = colorPixels(raster(borderRegion("Alpha")), redColor);
    editor.clearSelection();
    const int normalPixels = colorPixels(raster(borderRegion("Alpha")), redColor);
    CHECK(selectedPixels > normalPixels * 2);
    for (const auto *topic : {"Alpha", "Nested", "Leaf", "Sibling"}) branch(topic, redColor);
    CHECK(raster(borderRegion("Root")) == originalRoot);
    CHECK(raster(borderRegion("Beta")) == originalBeta);
    CHECK(crossImage() == originalCross);
    CHECK(textItem(editor, "Alpha")->defaultTextColor() == QColor("#34495e"));
    const QRectF alpha = topicRect(editor, "Alpha").adjusted(2, 2, -2, -2);
    atColor(QPointF(alpha.center().x(), alpha.bottom() - 4), QColor("#ecf0f1"));
    menu({}, {}, {});
    CHECK(editor.selectLink("cross"));
    menu({}, {}, {});
    CHECK(changed.size() == 1);

    // A colored drop target keeps its color, with thick ink and visible dash gaps.
    const QPoint from = labelPoint(editor, "Beta");
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
    const QPoint target = labelPoint(editor, "Alpha");
    movePointer(view, target, Qt::LeftButton);
    CHECK(view.viewport()->cursor().shape() == Qt::DragMoveCursor);
    const QImage drop = raster(borderRegion("Alpha"));
    CHECK(colorPixels(drop, redColor) > normalPixels);
    int emptyColumns = 0;
    for (int x = 0; x < drop.width(); ++x) {
        bool colored = false;
        for (int y = 0; y < drop.height(); ++y) colored = colored || matches(drop.pixel(x, y), redColor);
        if (!colored) ++emptyColumns;
    }
    CHECK(emptyColumns > 4);
    QTest::keyClick(&view, Qt::Key_Escape);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);
    CHECK(exported(editor) == expected && changed.size() == 1);

    menu("a1", blue, automatic);
    menu("a", green, red);
    editor.clearSelection();
    branch("Alpha", greenColor); branch("Sibling", greenColor);
    branch("Nested", blueColor); branch("Leaf", blueColor);
    menu("a1", automatic, blue);
    CHECK(!localStyle("a1").contains("branchColor"));
    menu("a1", {}, automatic);
    editor.clearSelection();
    branch("Nested", greenColor); branch("Leaf", greenColor);
    menu("a", automatic, green);
    const auto cleared = changed.size();
    menu("a", automatic, automatic);
    CHECK(changed.size() == cleared && localStyle("a") == record(before, "nodes", "a").at("style"));
    editor.clearSelection();
    CHECK(raster(originalBounds) == originalMap);
    menu("a", red, automatic);
    auto *reset = editor.findChild<QToolButton *>(QStringLiteral("nodeResetAppearance"));
    CHECK(reset);
    reset->click();
    CHECK(localStyle("a") == Json({{"branchColor", "#e74c3c"}, {"custom", {{"keep", true}}}}));
    menu("a1", blue, automatic);
    CHECK(editor.setExpanded("a", false));
    CHECK(texts(editor, "Nested").isEmpty());
    menu("a", green, red);
    CHECK(editor.setExpanded("a", true));
    editor.clearSelection();
    branch("Sibling", greenColor); branch("Nested", blueColor); branch("Leaf", blueColor);
    const QString added = editor.addNode("a", "Inserted");
    CHECK(!added.isEmpty() && !localStyle(added).contains("branchColor"));
    editor.clearSelection(); branch("Inserted", greenColor);
    menu("b", orange, automatic);
    CHECK(editor.moveNode("a2", "b"));
    CHECK(!localStyle("a2").contains("branchColor"));
    editor.clearSelection(); branch("Sibling", orangeColor);
    CHECK(editor.moveNode("a1", "b"));
    editor.clearSelection(); branch("Nested", blueColor); branch("Leaf", blueColor);
    menu("r", red, automatic);
    const QString rootChild = editor.addNode("r", "Root child");
    CHECK(!rootChild.isEmpty() && !localStyle(rootChild).contains("branchColor"));
    editor.clearSelection();
    border("Root", redColor); branch("Root child", redColor);
    branch("Alpha", greenColor); branch("Beta", orangeColor); branch("Nested", blueColor);
    const Json saved = exported(editor);
    CHECK(editor.loadJson(encoded(saved)) && exported(editor) == saved);
    editor.clearSelection();
    border("Root", redColor); branch("Root child", redColor); branch("Alpha", greenColor);
    branch("Beta", orangeColor); branch("Sibling", orangeColor); branch("Nested", blueColor); branch("Leaf", blueColor);

    Json imported = saved;
    for (auto &node : imported["nodes"]) {
        if (node["id"] == "a2") node["style"]["branchColor"] = 42;
        if (node["id"] == utf8(added)) node["style"]["branchColor"] = "not-a-color";
        if (node["id"] == utf8(rootChild)) node["style"]["branchColor"] = "#123456";
    }
    CHECK(editor.loadJson(encoded(imported)) && exported(editor) == imported);
    editor.clearSelection();
    branch("Sibling", orangeColor); branch("Inserted", greenColor); branch("Root child", QColor("#123456"));
    menu(rootChild, {}, {});
    menu("a2", automatic, automatic);
    menu(added, automatic, automatic);
    CHECK(!localStyle("a2").contains("branchColor") && !localStyle(added).contains("branchColor"));
    const Json finalDocument = exported(editor);
    for (auto direction : {Editor::LayoutDirection::Balanced, Editor::LayoutDirection::Left,
                           Editor::LayoutDirection::Right, Editor::LayoutDirection::Outline}) {
        CHECK(editor.setLayoutDirection(direction));
        editor.clearSelection();
        QTest::mouseMove(view.viewport(), blankPoint(view));
        pump();
        border("Root", redColor); branch("Alpha", greenColor); branch("Inserted", greenColor);
        branch("Beta", orangeColor); branch("Sibling", orangeColor);
        branch("Nested", blueColor); branch("Leaf", blueColor); branch("Root child", QColor("#123456"));
        const QTransform zoom = view.transform();
        const QPointF camera = view.mapToScene(view.viewport()->rect().center());
        const HtmlPage html(editor.toHtml());
        compareHtmlScene(editor, html);
        CHECK(exported(editor) == finalDocument && editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
        CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == camera);
    }
    CHECK(errors.isEmpty());
}

static void auto_branch_color_case() {
    RawLabelConfig config;
    RawLabelEditor editor(config);
    config.autoRandomBranchColor = false;
    RawLabelEditor disabled(config);
    const QString plain = disabled.addNode("root", "Uncolored");
    CHECK(!plain.isEmpty() && !record(exported(disabled), "nodes", plain).at("style").contains("branchColor"));
    QStringList palette;
    for (auto *button : editor.findChildren<QToolButton *>())
        if (button->property("color").isValid()) palette.append(button->property("color").toString());
    CHECK(palette.size() == 23);
    const Json empty = Json::parse(R"({"schemaVersion":1,"rootId":"home","nodes":[
        {"id":"home","topic":"Home"}],"crossLinks":[]})");
    CHECK(editor.loadJson(encoded(empty)));
    CHECK(editor.setExpanded(QStringLiteral("home"), false));
    auto color = [&](const QString &id) {
        return qs(record(exported(editor), "nodes", id).at("style").at("branchColor"));
    };
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QList<Json> observed;
    const auto connection = QObject::connect(&editor, &Editor::documentChanged, &editor,
        [&] { observed.append(exported(editor)); });
    std::set<QString> used;
    QStringList ids;
    for (qsizetype i = 0; i < palette.size(); ++i) {
        const auto count = changed.size();
        const QString id = editor.addNode("home", QStringLiteral("Main %1").arg(i));
        CHECK(!id.isEmpty() && changed.size() == count + 1);
        const QString picked = color(id);
        CHECK(palette.contains(picked) && used.insert(picked).second);
        CHECK(record(observed.back(), "nodes", id).at("style").at("branchColor") == utf8(picked));
        ids.append(id);
    }
    QObject::disconnect(connection);
    const QString overflow = editor.addNode("home", "Reused");
    CHECK(!overflow.isEmpty() && used.count(color(overflow)) == 1);
    CHECK(editor.removeNode(overflow));
    const QString freed = color(ids.front());
    CHECK(editor.removeNode(ids.front()));
    const QString replacement = editor.addNode("home", "Replacement", 0);
    CHECK(!replacement.isEmpty() && color(replacement) == freed);
    const Json saved = exported(editor);
    CHECK(editor.loadJson(encoded(saved)) && exported(editor) == saved);
    rejectUnchanged(editor, [&] { return !editor.addNode("home", "Bad index", 1000).isEmpty(); });
    rejectUnchanged(editor, [&] { return !editor.addNode("absent", "Missing parent").isEmpty(); });

    // Only blue is unused by effective main-branch colors. Imported spellings,
    // inherited root red and hidden children count; nested blue does not.
    Json imported = empty;
    imported["nodes"][0]["style"] = {{"branchColor", "#e74c3c"}};
    imported["nodes"][0]["children"] = Json::array();
    auto main = [&](const std::string &id, const Json &style) {
        imported["nodes"][0]["children"].push_back(id);
        imported["nodes"].push_back({{"id", id}, {"topic", id}, {"style", style}});
    };
    for (const auto &hex : palette) {
        if (hex == "#3498db" || hex == "#e74c3c") continue;
        main(utf8(hex), {{"branchColor", hex == "#ffffff" ? "white" : utf8(hex.toUpper())}});
    }
    main("inherited", Json::object());
    main("invalid-number", {{"branchColor", 42}});
    main("invalid-text", {{"branchColor", "not-a-color"}});
    main("custom", {{"branchColor", "#123456"}});
    imported["nodes"][1]["children"] = {"nested"};
    imported["nodes"].push_back({{"id", "nested"}, {"topic", "Nested"}, {"style", {{"branchColor", "#3498db"}}}});
    CHECK(editor.loadJson(encoded(imported)) && exported(editor) == nativeDocument(imported));
    CHECK(editor.setExpanded(QStringLiteral("home"), false));
    const QString blue = editor.addNode("home", "Forced blue");
    CHECK(!blue.isEmpty() && color(blue) == "#3498db");
    CHECK(editor.setExpanded("home", true));
    htmlAppearance(editor);
    editor.clearSelection();
    const QRectF rectangle = topicRect(editor, "Forced blue").adjusted(2, 2, -2, -2);
    const QRectF region(rectangle.center().x() - 3, rectangle.top() - 2, 6, 4);
    QImage image(QSize(24, 16), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    graphics(editor).scene()->render(&painter, QRectF(0, 0, 24, 16), region, Qt::IgnoreAspectRatio);
    CHECK(painter.end());
    const QColor pixel = image.pixelColor(12, 8), expected("#3498db");
    CHECK(std::abs(pixel.red() - expected.red()) <= 2 && std::abs(pixel.green() - expected.green()) <= 2 &&
          std::abs(pixel.blue() - expected.blue()) <= 2);
    const Json beforeMove = record(exported(editor), "nodes", blue);
    CHECK(editor.moveNode(blue, "inherited"));
    CHECK(record(exported(editor), "nodes", blue) == beforeMove);
    const QString nested = editor.addNode("inherited", "Uncolored nested");
    CHECK(!nested.isEmpty() && !record(exported(editor), "nodes", nested).at("style").contains("branchColor"));
    CHECK(editor.moveNode(nested, "home"));
    CHECK(!record(exported(editor), "nodes", nested).at("style").contains("branchColor"));

    // Both UI entry points share the atomic insertion policy. Canceling the draft
    // retains the new node and its color; a child of that node stays inherited.
    CHECK(editor.newDocument("Home"));
    showEditor(editor);
    changed.clear();
    shortcut(editor, Qt::Key_Tab);
    const QString first = editor.selectedNodeId();
    CHECK(first != "root" && palette.contains(color(first)) && changed.size() == 1);
    topicKey(editor, Qt::Key_Escape);
    shortcut(editor, Qt::Key_Return);
    const QString sibling = editor.selectedNodeId();
    CHECK(sibling != first && palette.contains(color(sibling)) && color(sibling) != color(first));
    CHECK(changed.size() == 2);
    topicKey(editor, Qt::Key_Escape);
    shortcut(editor, Qt::Key_Tab);
    const QString child = editor.selectedNodeId();
    CHECK(child != sibling && !record(exported(editor), "nodes", child).at("style").contains("branchColor"));
    CHECK(changed.size() == 3);
    topicKey(editor, Qt::Key_Escape);
}

static void formatting_toolbar_case() {
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), root = QStringLiteral("r");
    const QString alpha = QStringLiteral("Alpha"), grin = QString::fromUtf8("😀");
    const char *const names[] = {"fontSize", "toggleBold", "toggleItalic", "resetStyle",
                                "textColorPopup", "fillColorPopup", "iconsPopup"};
    auto node = [](Json &doc, const QString &id) -> Json & {
        for (auto &entry : doc.at("nodes")) if (entry.at("id") == utf8(id)) return entry;
        throw std::runtime_error("Missing formatting fixture node");
    };
    Json fixture = editorFixture();
    node(fixture, a)["style"] = Json{{"branchColor", "#27ae60"},
        {"custom", {{"integer", UINT64_C(9007199254740993)}, {"nested", Json::array({true, "opaque"})}}}};
    node(fixture, a)["icons"] = Json::array();
    node(fixture, a)["tags"] = Json::array({"retained"});
    node(fixture, a)["hyperLink"] = "https://example.test/retained";
    node(fixture, a)["note"] = "Retained note";
    node(fixture, a)["image"] = Json{{"url", "memory:retained"}, {"width", 31}, {"height", 19}};

    QMainWindow host;
    auto *editor = new RawLabelEditor;
    auto *surface = new QWidget;
    auto *surfaceLayout = new QVBoxLayout(surface);
    surfaceLayout->setContentsMargins(0, 0, 0, 0);
    // VNote embeds toolbars in a view layout; QMainWindow dock toolbars expand in place instead.
    auto *toolbar = new QToolBar(QStringLiteral("Formatting"), surface);
    surfaceLayout->addWidget(toolbar);
    surfaceLayout->addWidget(editor, 1);
    host.setCentralWidget(surface);
    toolbar->setMovable(false);
    for (const char *name : names) {
        auto &action = editAction(*editor, name);
        toolbar->addAction(&action);
        if (action.menu()) {
            auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(&action));
            CHECK(button != nullptr);
            button->setPopupMode(QToolButton::InstantPopup);
        }
    }
    CHECK(editor->loadJson(encoded(fixture)));
    CHECK(editor->setLayoutDirection(Editor::LayoutDirection::Right));
    host.resize(1400, 900);
    host.show();
    host.activateWindow();
    showEditor(*editor, QSize(1400, 850));
    auto &view = graphics(*editor);
    auto *panel = editor->findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
    CHECK(panel != nullptr);
    auto *scroll = panel->findChild<QScrollArea *>();
    auto *toggle = panel->findChild<QAbstractButton *>(QStringLiteral("nodePropertiesToggle"));
    auto *cardSize = panel->findChild<QComboBox *>(QStringLiteral("nodeFontSize"));
    auto *cardBold = panel->findChild<QAbstractButton *>(QStringLiteral("nodeBold"));
    auto *cardItalic = panel->findChild<QAbstractButton *>(QStringLiteral("nodeItalic"));
    auto *size = toolbar->findChild<QComboBox *>(QStringLiteral("toolbarFontSize"));
    CHECK(scroll && toggle && cardSize && cardBold && cardItalic && size);
    auto menuFor = [&](const char *name) {
        auto *menu = editAction(*editor, name).menu();
        CHECK(menu != nullptr);
        return menu;
    };
    auto *textMenu = menuFor("textColorPopup");
    auto *fillMenu = menuFor("fillColorPopup");
    auto *iconsMenu = menuFor("iconsPopup");
    auto open = [&](const char *name) {
        host.activateWindow();
        pump();
        auto &action = editAction(*editor, name);
        CHECK(action.isEnabled());
        auto *widget = toolbar->widgetForAction(&action);
        CHECK(widget != nullptr);
        auto *menu = menuFor(name);
        menu->popup(widget->mapToGlobal(QPoint(0, widget->height())));
        pump();
        CHECK(menu->isVisible());
        return menu;
    };
    auto dismiss = [&](QMenu *menu) {
        QTest::keyClick(menu, Qt::Key_Escape);
        pump();
        CHECK(!menu->isVisible());
    };
    auto swatch = [](QMenu *menu, const char *name) {
        auto *button = menu->findChild<QAbstractButton *>(QString::fromLatin1(name));
        CHECK(button != nullptr);
        return button;
    };
    auto chooseSize = [](QComboBox *combo, int pixels) {
        const int index = combo->findData(pixels);
        CHECK(index >= 0);
        combo->showPopup();
        pump();
        auto *choices = combo->view();
        CHECK(choices->isVisible());
        choices->setCurrentIndex(combo->model()->index(index, 0));
        QTest::keyClick(choices, Qt::Key_Return);
        pump();
    };
    auto chooseColor = [&](const char *action, const char *button) {
        auto *menu = open(action);
        QTest::mouseClick(swatch(menu, button), Qt::LeftButton);
        pump();
        CHECK(!menu->isVisible() && view.hasFocus());
    };
    auto resetDocument = [&](const Json &doc) {
        textMenu->hide(); fillMenu->hide(); iconsMenu->hide();
        editor->setEnabled(true);
        editor->setReadOnly(false);
        CHECK(editor->loadJson(encoded(doc)));
        CHECK(editor->selectNode(a));
        host.activateWindow();
        view.setFocus();
        pump();
        CHECK(!editor->canUndo() && !editor->canRedo());
    };
    auto iconsInput = [&] {
        auto *input = iconsMenu->findChild<QLineEdit *>(QStringLiteral("toolbarNodeIcons"));
        CHECK(input != nullptr);
        return input;
    };
    auto emojiChoices = [&] {
        auto *choices = iconsMenu->findChild<QListView *>(QStringLiteral("emojiChoices"));
        CHECK(choices != nullptr);
        return choices;
    };
    auto emojiIndex = [&](const QString &name) {
        auto *choices = emojiChoices();
        for (int row = 0; row < choices->model()->rowCount(); ++row) {
            const QModelIndex index = choices->model()->index(row, 0);
            if (index.data().toString() == name) return index;
        }
        throw std::runtime_error("Missing emoji result: " + utf8(name));
    };
    auto clickEmoji = [&](const QString &name) {
        auto *choices = emojiChoices();
        const auto index = emojiIndex(name);
        choices->scrollTo(index);
        pump();
        const QRect cell = choices->visualRect(index);
        CHECK(choices->viewport()->rect().contains(cell.center()));
        QTest::mouseClick(choices->viewport(), Qt::LeftButton, Qt::NoModifier, cell.center());
        pump();
        CHECK(iconsMenu->isVisible() && iconsInput()->hasFocus());
    };
    auto synchronized = [&](int pixels) {
        auto *label = textItem(*editor, alpha);
        CHECK(size->currentData().toInt() == pixels && cardSize->currentData().toInt() == pixels);
        CHECK(editAction(*editor, "toggleBold").isChecked() == label->font().bold());
        CHECK(editAction(*editor, "toggleItalic").isChecked() == label->font().italic());
        CHECK(cardBold->isChecked() == label->font().bold());
        CHECK(cardItalic->isChecked() == label->font().italic());
        const Json appearance = record(exported(*editor), "nodes", a).at("style");
        for (bool fill : {false, true}) {
            auto *menu = open(fill ? "fillColorPopup" : "textColorPopup");
            const std::string key = fill ? "background" : "color";
            const QString prefix = fill ? QStringLiteral("toolbarFill_") : QStringLiteral("toolbarText_");
            const QString name = prefix + (appearance.contains(key)
                ? QStringLiteral("nodeColor_") + qs(appearance.at(key)).mid(1)
                : QStringLiteral("nodeDefaultColor"));
            auto *button = menu->findChild<QAbstractButton *>(name);
            CHECK(button && button->isChecked());
            dismiss(menu);
            auto *mode = panel->findChild<QAbstractButton *>(fill ? QStringLiteral("nodeFillColor") : QStringLiteral("nodeTextColor"));
            CHECK(mode != nullptr); mode->click();
            auto *cardSwatch = panel->findChild<QAbstractButton *>(name.mid(prefix.size()));
            CHECK(cardSwatch && cardSwatch->isChecked());
        }
        const Json current = exported(*editor);
        QStringList values;
        for (const auto &icon : record(current, "nodes", a).at("icons")) values.append(qs(icon));
        CHECK(panel->findChild<QLineEdit *>(QStringLiteral("nodeIcons"))->text() == values.join(QStringLiteral(", ")));
    };

    // Each toolbar edit is one undoable native command, not a parallel host-side state.
    resetDocument(fixture);
    const Json baseline = exported(*editor);
    const QTransform zoom = view.transform();
    QSignalSpy changed(editor, &Editor::documentChanged);
    for (const char *name : {"textColorPopup", "fillColorPopup", "iconsPopup"}) dismiss(open(name));
    CHECK(editor->selectNode(b) && editor->selectNode(a));
    CHECK(exported(*editor) == baseline && changed.isEmpty() && !editor->canUndo());
    const qreal normalHeight = topicRect(*editor, alpha).height();
    std::vector<Json> history{baseline};
    auto snapshot = [&] { history.push_back(exported(*editor)); CHECK(changed.size() == int(history.size()) - 1); };
    chooseSize(size, 24);
    CHECK(textItem(*editor, alpha)->font().pixelSize() == 24);
    CHECK(topicRect(*editor, alpha).height() > normalHeight);
    snapshot();
    trigger(*editor, "toggleBold");
    CHECK(textItem(*editor, alpha)->font().bold()); snapshot();
    trigger(*editor, "toggleItalic");
    CHECK(textItem(*editor, alpha)->font().italic()); snapshot();
    chooseColor("textColorPopup", "toolbarText_nodeColor_e74c3c");
    CHECK(textItem(*editor, alpha)->defaultTextColor() == QColor(QStringLiteral("#e74c3c"))); snapshot();
    const QRectF paintedRect = topicRect(*editor, alpha);
    const QImage unfilled = paintScene(*editor, paintedRect);
    chooseColor("fillColorPopup", "toolbarFill_nodeColor_a9d6f5");
    CHECK(paintScene(*editor, paintedRect) != unfilled); snapshot();
    open("iconsPopup");
    clickEmoji(QStringLiteral("grinning face"));
    CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({utf8(grin)}));
    CHECK(ownerItem(textItem(*editor, grin)) == ownerItem(textItem(*editor, alpha)));
    dismiss(iconsMenu); snapshot();
    Json styled = baseline;
    auto &style = node(styled, a)["style"];
    style["fontSize"] = 24; style["fontWeight"] = "bold"; style["fontStyle"] = "italic";
    style["color"] = "#e74c3c"; style["background"] = "#a9d6f5";
    node(styled, a)["icons"] = Json::array({utf8(grin)});
    CHECK(exported(*editor) == styled && view.transform() == zoom);
    for (int index = int(history.size()) - 2; index >= 0; --index) {
        CHECK(editor->undo()); pump();
        CHECK(exported(*editor) == history[size_t(index)]);
        synchronized(index == 0 ? 0 : 24);
    }
    CHECK(!editor->canUndo());
    for (size_t index = 1; index < history.size(); ++index) {
        CHECK(editor->redo()); pump();
        CHECK(exported(*editor) == history[index]);
        synchronized(24);
    }
    CHECK(!editor->canRedo());
    trigger(*editor, "resetStyle");
    Json reset = baseline;
    node(reset, a)["icons"] = Json::array({utf8(grin)});
    CHECK(exported(*editor) == reset);
    synchronized(0);
    CHECK(!textItem(*editor, alpha)->font().bold() && !textItem(*editor, alpha)->font().italic());
    CHECK(editor->undo() && exported(*editor) == styled);
    CHECK(editor->redo() && exported(*editor) == reset);

    // Parsed imported state, root/inherited defaults, and unavailable selections never rewrite JSON.
    Json imported = fixture;
    node(imported, a)["style"]["fontSize"] = 17;
    node(imported, a)["style"]["fontWeight"] = 700;
    node(imported, a)["style"]["fontStyle"] = "normal";
    resetDocument(imported);
    const Json importedBaseline = exported(*editor);
    changed.clear();
    synchronized(17);
    CHECK(size->findData(17) >= 0 && textItem(*editor, alpha)->font().pixelSize() == 17);
    CHECK(editAction(*editor, "toggleBold").isChecked() && !editAction(*editor, "toggleItalic").isChecked());
    CHECK(editor->selectNode(root));
    CHECK(size->currentData().toInt() == 0 && size->findData(17) < 0);
    CHECK(editAction(*editor, "toggleBold").isChecked());
    QFont originalFont = editor->font(), italicFont = originalFont;
    italicFont.setItalic(true);
    editor->setFont(italicFont); pump();
    CHECK(editAction(*editor, "toggleItalic").isChecked());
    CHECK(editor->selectNode(a) && !editAction(*editor, "toggleItalic").isChecked());
    editor->setFont(originalFont); pump();
    auto blocked = [&] {
        for (const char *name : names) { auto &action = editAction(*editor, name); CHECK(!action.isEnabled()); action.trigger(); }
        CHECK(!size->isEnabled());
        CHECK(QMetaObject::invokeMethod(size, "activated", Q_ARG(int, size->findData(24))));
        pump();
        CHECK(exported(*editor) == importedBaseline && changed.isEmpty() && !editor->canUndo());
    };
    editor->setReadOnly(true); blocked();
    shortcut(*editor, Qt::Key_Right);
    CHECK(editor->selectedNodeId() == QStringLiteral("d"));
    editor->setReadOnly(false);
    CHECK(editor->selectLink(QStringLiteral("l1"))); blocked();
    CHECK(editor->selectNode(a));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, labelPoint(*editor, QStringLiteral("Beta")));
    pump(); CHECK(editor->selectedNodeIds() == QStringList({a, b})); blocked();
    editor->clearSelection(); blocked();
    shortcut(*editor, Qt::Key_Home);
    CHECK(editor->selectedNodeId() == root && exported(*editor) == importedBaseline);

    // Rejected inline commits preserve the draft/caret and undo QAction's optimistic check state.
    resetDocument(fixture);
    shortcut(*editor, Qt::Key_F2);
    auto *draft = &topicInput(*editor);
    const QString rejected = QStringLiteral("Invalid") + QChar(QChar::Null) + QStringLiteral("draft");
    draft->setPlainText(rejected);
    auto cursor = draft->textCursor(); cursor.setPosition(2); cursor.setPosition(5, QTextCursor::KeepAnchor);
    draft->setTextCursor(cursor);
    changed.clear();
    QSignalSpy errors(editor, &Editor::errorOccurred);
    auto rejectedIntact = [&] {
        pump();
        CHECK(activeTopicInput(*editor) == draft && draft->hasFocus());
        CHECK(draft->toPlainText() == rejected && draft->textCursor().anchor() == 2 && draft->textCursor().position() == 5);
        CHECK(exported(*editor) == baseline && changed.isEmpty() && !editor->canUndo());
        CHECK(!editAction(*editor, "toggleBold").isChecked() && size->currentData().toInt() == 0);
    };
    trigger(*editor, "toggleBold"); rejectedIntact();
    // Keyboard activation avoids a synthetic focus transfer committing the draft before the size handler.
    QTest::keyClick(size, Qt::Key_Down); rejectedIntact();
    textMenu->popup(toolbar->mapToGlobal(QPoint(0, toolbar->height())));
    pump(); CHECK(!textMenu->isVisible());
    host.activateWindow(); // Offscreen popup dismissal may deactivate the host.
    rejectedIntact();
    CHECK(!errors.isEmpty());
    draft->setPlainText(QStringLiteral("Accepted toolbar draft"));
    trigger(*editor, "toggleBold");
    Json committed = baseline;
    setTopic(committed, "a", QStringLiteral("Accepted toolbar draft"));
    Json committedBold = committed; node(committedBold, a)["style"]["fontWeight"] = "bold";
    CHECK(exported(*editor) == committedBold && changed.size() == 2 && !activeTopicInput(*editor));
    CHECK(editor->undo() && exported(*editor) == committed);
    CHECK(editor->undo() && exported(*editor) == baseline && !editor->canUndo());
    CHECK(editor->redo() && exported(*editor) == committed);
    CHECK(editor->redo() && exported(*editor) == committedBold);

    // An old popup cannot mutate a same-ID replacement, or resume after a transient lock.
    for (int invalidation : {0, 1, 2, 3, 4, 5, 6}) {
        for (bool icons : {false, true}) {
            resetDocument(fixture);
            auto *menu = open(icons ? "iconsPopup" : "textColorPopup");
            auto *oldSwatch = icons ? nullptr : swatch(menu, "toolbarText_nodeColor_e74c3c");
            auto *oldInput = icons ? iconsInput() : nullptr;
            Json replacement = baseline;
            if (invalidation == 0) {
                setTopic(replacement, "a", QStringLiteral("Replacement with reused id"));
                CHECK(editor->loadJson(encoded(replacement)));
                CHECK(editor->selectNode(a));
            } else if (invalidation == 1) editor->setReadOnly(true);
            else if (invalidation == 2) editor->setEnabled(false);
            else if (invalidation == 3) CHECK(!editor->loadJson(QByteArray("not JSON")));
            else if (invalidation == 4) CHECK(editor->selectNode(b));
            else if (invalidation == 5) toolbar->hide();
            else host.hide();
            pump(); CHECK(!menu->isVisible());
            const Json retained = exported(*editor);
            editor->setReadOnly(false); editor->setEnabled(true); toolbar->show(); host.show();
            CHECK(editor->selectNode(a));
            if (oldInput) oldInput->setText(QStringLiteral("stale icon"));
            else oldSwatch->click();
            pump();
            CHECK(exported(*editor) == retained);
            if (invalidation == 0) CHECK(retained == replacement);
        }
    }
    resetDocument(fixture);
    size->showPopup(); pump(); CHECK(size->view()->isVisible());
    editor->setReadOnly(true); pump();
    CHECK(!size->view()->isVisible() && !size->isEnabled());
    editor->setReadOnly(false);

    // Immediate host deletion from a successful command or its inline-commit prerequisite is safe.
    for (int operation : {0, 1, 2, 3, 4}) {
        QMainWindow closingHost;
        QPointer<Editor> closing = new RawLabelEditor;
        closingHost.setCentralWidget(closing);
        auto *closingToolbar = closingHost.addToolBar(QStringLiteral("Formatting"));
        for (const char *name : names) closingToolbar->addAction(&editAction(*closing, name));
        CHECK(closing->loadJson(encoded(fixture)) && closing->selectNode(a));
        closingHost.resize(1400, 900); closingHost.show(); showEditor(*closing);
        QPointer<QAction> action = &editAction(*closing, operation == 0 || operation == 4 ? "toggleBold" :
            operation == 1 ? "fontSize" : operation == 2 ? "textColorPopup" : "iconsPopup");
        if (operation == 4) {
            shortcut(*closing, Qt::Key_F2);
            topicInput(*closing).setPlainText(QStringLiteral("Committed before close"));
        }
        if (action->menu()) { action->menu()->popup(closingToolbar->mapToGlobal(QPoint(0, closingToolbar->height()))); pump(); }
        Json observed;
        QObject::connect(closing.data(), &Editor::documentChanged, qApp, [&] {
            observed = exported(*closing);
            // QLineEdit::setText still uses its line control after textChanged.
            // Hosts must defer deletion while that Qt input operation is active.
            if (operation == 3) closing->deleteLater();
            else delete closing.data();
        });
        if (operation == 0 || operation == 4) action->trigger();
        else if (operation == 1) {
            auto *combo = closingToolbar->findChild<QComboBox *>(QStringLiteral("toolbarFontSize"));
            CHECK(combo != nullptr);
            const int index = combo->findData(10);
            combo->setCurrentIndex(index);
            CHECK(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index)));
        } else if (operation == 2) swatch(action->menu(), "toolbarText_nodeColor_e74c3c")->click();
        else {
            auto *input = action->menu()->findChild<QLineEdit *>(QStringLiteral("toolbarNodeIcons"));
            CHECK(input != nullptr); input->setText(grin);
            pump();
        }
        CHECK(closing.isNull() && action.isNull());
        const auto &observedNode = record(observed, "nodes", a);
        if (operation == 0) CHECK(observedNode.at("style").at("fontWeight") == "bold");
        else if (operation == 1) CHECK(observedNode.at("style").at("fontSize") == 10);
        else if (operation == 2) CHECK(observedNode.at("style").at("color") == "#e74c3c");
        else if (operation == 3) CHECK(observedNode.at("icons") == Json::array({utf8(grin)}));
        else { CHECK(observedNode.at("topic") == "Committed before close"); CHECK(!observedNode.at("style").contains("fontWeight")); }
        pump();
    }

    // The embedded picker replaces only the active comma entry and keeps an input-capable menu open.
    Json iconFixture = fixture;
    node(iconFixture, a)["icons"] = Json::array({utf8(grin), "retained second"});
    resetDocument(iconFixture);
    const Json initialIcons = exported(*editor);
    changed.clear();
    open("iconsPopup");
    auto *input = iconsInput();
    auto *choices = emojiChoices();
    auto *categories = iconsMenu->findChild<QComboBox *>(QStringLiteral("emojiCategories"));
    CHECK(categories && input->hasFocus());
    CHECK(input->text().endsWith(QStringLiteral(", ")) && input->cursorPosition() == input->text().size());
    CHECK(exported(*editor) == initialIcons && changed.isEmpty());
    input->setCursorPosition(1);
    input->setSelection(0, grin.size());
    QTest::keyClicks(input, "apple"); pump();
    clickEmoji(QStringLiteral("red apple"));
    const QString apple = QString::fromUtf8("🍎");
    CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({utf8(apple), "retained second"}));
    input->setCursorPosition(input->text().size());
    QTest::keyClicks(input, "rocket"); pump();
    choices->setCurrentIndex(emojiIndex(QStringLiteral("rocket")));
    QTest::keyClick(input, Qt::Key_Return); pump();
    CHECK(iconsMenu->isVisible() && input->hasFocus());
    const QString rocket = QString::fromUtf8("🚀");
    CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({utf8(apple), "retained second", utf8(rocket)}));
    QTest::keyClick(input, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(input, Qt::Key_Backspace); pump();
    CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array());
    CHECK(iconsMenu->isVisible() && input->hasFocus());

    QTest::mouseClick(categories, Qt::LeftButton); pump();
    CHECK(iconsMenu->isVisible() && categories->view()->isVisible());
    const int food = categories->findText(QStringLiteral("Food & Drink")); CHECK(food >= 0);
    const QModelIndex foodIndex = categories->model()->index(food, 0);
    categories->view()->scrollTo(foodIndex);
    QTest::qWait(QApplication::doubleClickInterval() + 1);
    const QPoint foodPoint = categories->view()->visualRect(foodIndex).center();
    QTest::mouseMove(categories->view()->viewport(), foodPoint);
    QTest::mouseClick(categories->view()->viewport(), Qt::LeftButton, Qt::NoModifier, foodPoint);
    pump();
    CHECK(iconsMenu->isVisible() && input->hasFocus() && !categories->view()->isVisible());
    CHECK(categories->currentIndex() == food);
    clickEmoji(QStringLiteral("red apple"));
    CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({utf8(apple)}));
    QTest::keyClicks(input, "apple"); pump();
    CHECK(emojiIndex(QStringLiteral("red apple")).isValid());
    CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({utf8(apple), "apple"}));
    choices->setCurrentIndex(emojiIndex(QStringLiteral("red apple")));
    QTest::keyClick(input, Qt::Key_Return); pump();
    CHECK(iconsMenu->isVisible() && input->hasFocus());
    CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({utf8(apple), utf8(apple)}));

    // Ctrl navigation moves a visible result, not the text caret or the host's map selection.
    const QString inputText = input->text();
    const int caret = input->cursorPosition();
    const Json beforeNavigation = exported(*editor);
    choices->scrollToTop(); choices->setCurrentIndex(choices->model()->index(0, 0)); pump();
    const QModelIndex first = choices->currentIndex(), right = choices->model()->index(1, 0);
    const QRect firstCell = choices->visualRect(first);
    CHECK(right.isValid() && choices->visualRect(right).top() == firstCell.top());
    int belowRow = 1;
    while (belowRow < choices->model()->rowCount() && choices->visualRect(choices->model()->index(belowRow, 0)).top() == firstCell.top()) ++belowRow;
    CHECK(belowRow < choices->model()->rowCount());
    const QModelIndex below = choices->model()->index(belowRow, 0);
    auto navigate = [&](Qt::Key key, const QModelIndex &expected) {
        QTest::keyClick(input, key, Qt::ControlModifier); pump();
        CHECK(choices->currentIndex() == expected && input->text() == inputText && input->cursorPosition() == caret);
        CHECK(iconsMenu->isVisible() && input->hasFocus() && exported(*editor) == beforeNavigation);
    };
    navigate(Qt::Key_L, right); navigate(Qt::Key_H, first);
    navigate(Qt::Key_J, below); navigate(Qt::Key_K, first);
    QTest::keyClick(input, Qt::Key_Down); pump();
    CHECK(choices->currentIndex() == right && exported(*editor) == beforeNavigation);
    QTest::keyClick(input, Qt::Key_Up); pump();
    CHECK(choices->currentIndex() == first && exported(*editor) == beforeNavigation);
    QTest::keyClicks(input, "no-such-emoji-xyz"); pump();
    CHECK(choices->model()->rowCount() == 0);
    auto *explanation = iconsMenu->findChild<QLabel *>(QStringLiteral("emojiDescription"));
    CHECK(explanation && explanation->isVisible() && !explanation->text().isEmpty());
    CHECK(explanation->text() != first.data().toString());
    const Json noResults = exported(*editor);
    QTest::keyClick(input, Qt::Key_Return); pump();
    CHECK(iconsMenu->isVisible() && input->hasFocus() && exported(*editor) == noResults);
    // Ordinary map shortcut letters remain ordinary input; no extra nodes or style commands.
    QTest::keyClicks(input, "bircfotnp"); pump();
    Json typed = noResults;
    node(typed, a)["icons"].back() = "no-such-emoji-xyzbircfotnp";
    CHECK(exported(*editor) == typed && editor->selectedNodeId() == a);
    QTest::mouseClick(categories, Qt::LeftButton); pump();
    CHECK(categories->view()->isVisible());
    QTest::keyClick(categories->view(), Qt::Key_Escape); pump();
    CHECK(QTest::qWaitFor([&] { return !categories->view()->isVisible() && iconsMenu->isVisible() && input->hasFocus(); }, 1000));
    QTest::keyClick(input, Qt::Key_Escape); pump();
    CHECK(!iconsMenu->isVisible() && exported(*editor) == typed);

    // Popups do not alter the user's collapsed-card preference, camera, or dismissal semantics.
    resetDocument(fixture);
    if (scroll->isVisible()) { QTest::mouseClick(toggle, Qt::LeftButton); pump(); }
    CHECK(scroll->isHidden());
    const QTransform collapsedZoom = view.transform();
    auto collapsed = [&] { CHECK(scroll->isHidden() && view.transform() == collapsedZoom); };
    for (const char *name : {"textColorPopup", "fillColorPopup", "iconsPopup"}) {
        const Json before = exported(*editor);
        dismiss(open(name)); collapsed();
        auto *menu = open(name);
        // Send an outside press at the native popup boundary, where QMenu handles dismissal.
        const QPoint outside(-12, -12);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(outside), QPointF(menu->mapToGlobal(outside)),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(menu, &press); pump();
        CHECK(!menu->isVisible() && exported(*editor) == before); collapsed();
    }
    for (bool fill : {false, true}) {
        const char *name = fill ? "fillColorPopup" : "textColorPopup";
        const char *key = fill ? "background" : "color";
        const Json before = exported(*editor);
        auto *menu = open(name);
        QTest::keyClicks(menu, "22"); pump();
        Json red = before; node(red, a)["style"][key] = "#e74c3c";
        CHECK(exported(*editor) == red && !menu->isVisible()); collapsed();
        menu = open(name); QTest::keyClicks(menu, "11"); pump();
        CHECK(exported(*editor) == before && !menu->isVisible()); collapsed();
        menu = open(name); QTest::keyClick(menu, Qt::Key_2); dismiss(menu);
        menu = open(name); QTest::keyClick(menu, Qt::Key_1); pump();
        CHECK(exported(*editor) == before && menu->isVisible());
        QTest::keyClick(menu, Qt::Key_1); pump();
        CHECK(exported(*editor) == before && !menu->isVisible()); collapsed();
    }

    // Imported nonpalette/invalid colors have honest checks and are not normalized on opening.
    Json unusual = fixture;
    node(unusual, a)["style"]["color"] = "#123456";
    node(unusual, a)["style"]["background"] = "not-a-color";
    resetDocument(unusual);
    const Json unusualBaseline = exported(*editor);
    open("textColorPopup");
    for (auto *button : textMenu->findChildren<QAbstractButton *>()) CHECK(!button->isChecked());
    dismiss(textMenu);
    open("fillColorPopup"); CHECK(swatch(fillMenu, "toolbarFill_nodeDefaultColor")->isChecked()); dismiss(fillMenu);
    CHECK(exported(*editor) == unusualBaseline && !editor->canUndo());

    // A real QMainWindow toolbar extension creates a second size presentation and retains submenu actions.
    resetDocument(fixture);
    auto *common = new QAction(QStringLiteral("Host controls preceding the formatting group"), toolbar);
    toolbar->insertAction(&editAction(*editor, "fontSize"), common);
    host.setMinimumSize(0, 0);
    host.resize(120, 700); pump();
    auto *extension = toolbar->findChild<QToolButton *>(QStringLiteral("qt_toolbar_ext_button"));
    CHECK(extension && extension->isVisible() && extension->menu());
    auto *overflow = extension->menu();
    auto openOverflow = [&] {
        overflow->popup(extension->mapToGlobal(QPoint(0, extension->height()))); pump();
        CHECK(overflow->isVisible());
    };
    openOverflow();
    QPointer<QComboBox> overflowSize = overflow->findChild<QComboBox *>(QStringLiteral("toolbarFontSize"));
    CHECK(overflowSize && overflowSize->isVisible());
    chooseSize(overflowSize, 28);
    CHECK(record(exported(*editor), "nodes", a).at("style").at("fontSize") == 28);
    CHECK(size->currentData().toInt() == 28 && overflowSize->currentData().toInt() == 28);
    overflow->hide();
    for (const char *name : {"textColorPopup", "fillColorPopup", "iconsPopup"}) {
        common->setEnabled(false);
        const auto modifiedConnection = QObject::connect(editor, &Editor::documentChanged, common, [common] { common->setEnabled(true); });
        openOverflow();
        auto &action = editAction(*editor, name);
        const QRect entry = overflow->actionGeometry(&action);
        CHECK(entry.isValid() && overflow->rect().contains(entry.center()));
        QTest::mouseMove(overflow, entry.center());
        QTest::mouseClick(overflow, Qt::LeftButton, Qt::NoModifier, entry.center()); pump();
        auto *menu = action.menu();
        CHECK(QTest::qWaitFor([&] { return menu->isVisible(); }, 1000));
        if (menu == iconsMenu) {
            input = iconsInput();
            categories->setCurrentIndex(0);
            CHECK(QTest::qWaitFor([&] {
                return input->isEnabled() && input->hasFocus() &&
                    QGuiApplication::focusWindow() == menu->windowHandle();
            }, 1000));
            // Use the active native window, not direct line-edit delivery: a rebuilt
            // toolbar extension can lose the submenu from Qt's popup keyboard stack.
            for (const auto key : {Qt::Key_R, Qt::Key_O, Qt::Key_C, Qt::Key_K, Qt::Key_E, Qt::Key_T}) {
                CHECK(QGuiApplication::focusWindow());
                QTest::keyClick(QGuiApplication::focusWindow(), key); pump();
            }
            CHECK(menu->isVisible() && input->hasFocus());
            CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({"rocket"}));
            choices = emojiChoices();
            choices->setCurrentIndex(emojiIndex(QStringLiteral("rocket")));
            QTest::keyClick(input, Qt::Key_Return); pump();
            CHECK(menu->isVisible() && input->hasFocus());
            CHECK(record(exported(*editor), "nodes", a).at("icons") == Json::array({utf8(rocket)}));
            dismiss(menu);
        }
        else {
            QTest::keyClicks(menu, "22"); pump();
            CHECK(record(exported(*editor), "nodes", a).at("style").at(menu == textMenu ? "color" : "background") == "#e74c3c");
        }
        QObject::disconnect(modifiedConnection);
        overflow->hide(); collapsed();
    }
    openOverflow();
    overflowSize = overflow->findChild<QComboBox *>(QStringLiteral("toolbarFontSize"));
    CHECK(overflowSize != nullptr);
    overflowSize->showPopup(); pump();
    const QPointer<QAbstractItemView> overflowChoices(overflowSize->view());
    CHECK(overflowChoices->isVisible());
    editor->setEnabled(false); pump();
    // QAction changes can rebuild the extension menu and release its widget presentation.
    CHECK(!overflowChoices || !overflowChoices->isVisible());
    CHECK((!overflowSize || !overflowSize->isEnabled()) && !size->isEnabled());
    overflow->hide();
    editor->setEnabled(true);
    host.resize(1400, 900); pump();
    CHECK(size->isVisible() && size->currentData().toInt() == 28);
    collapsed();
}

static void separated_properties_case() {
    RawLabelConfig config;
    config.separateNodePropertiesPanel = true;
    const QString a = QStringLiteral("a"), b = QStringLiteral("b");
    {
        QMainWindow window;
        auto *editor = new RawLabelEditor(config, &window);
        window.setCentralWidget(editor);
        auto *panel = editor->nodePropertiesPanel();
        CHECK(panel && panel == editor->nodePropertiesPanel() && panel->parentWidget() == editor);
        CHECK(editor->findChildren<QWidget *>(QStringLiteral("nodePropertiesPanel")).size() == 1);
        auto *dock = new QDockWidget(&window);
        auto *tools = new QWidget(dock);
        tools->setMinimumSize(390, 660);
        dock->setWidget(tools);
        window.addDockWidget(Qt::RightDockWidgetArea, dock);
        panel->setParent(tools);
        const QRect externalGeometry(17, 23, 330, 610);
        panel->setGeometry(externalGeometry);
        panel->show();
        window.resize(1300, 900);
        window.show();
        CHECK(editor->loadJson(encoded(editorFixture())) && editor->selectNode(a));
        showEditor(*editor);
        auto &view = graphics(*editor);
        auto *scroll = panel->findChild<QScrollArea *>();
        auto *toggle = panel->findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
        auto *tags = panel->findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        auto *icons = panel->findChild<QLineEdit *>(QStringLiteral("nodeIcons"));
        auto *url = panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
        auto *image = panel->findChild<QLineEdit *>(QStringLiteral("nodeImageUrl"));
        auto *note = panel->findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
        auto *size = panel->findChild<QComboBox *>(QStringLiteral("nodeFontSize"));
        auto *bold = panel->findChild<QAbstractButton *>(QStringLiteral("nodeBold"));
        CHECK(scroll && toggle && tags && icons && url && image && note && size && bold);
        CHECK(panel->isVisible() && scroll->isVisible() && !toggle->isVisible());
        CHECK(!editor->findChild<QWidget *>(QStringLiteral("nodePropertiesPanel")));
        auto retained = [&] {
            CHECK(editor->nodePropertiesPanel() == panel && panel->parentWidget() == tools);
            CHECK(panel->geometry() == externalGeometry && !toggle->isVisible());
        };
        auto fieldsMatch = [&] {
            const Json node = record(exported(*editor), "nodes", editor->selectedNodeId());
            auto joined = [&](const char *key) {
                QStringList values;
                for (const auto &value : node.at(key)) values.append(qs(value));
                return values.join(QStringLiteral(", "));
            };
            CHECK(tags->text() == joined("tags") && icons->text() == joined("icons"));
            CHECK(url->text() == qs(node.at("hyperLink")) && note->toPlainText() == qs(node.at("note")));
            CHECK(image->text() == (node.at("image").is_object() ? qs(node.at("image").at("url")) : QString()));
            CHECK(size->currentData().toDouble() == node.at("style").value("fontSize", 0.0));
            CHECK(bold->isChecked() == (node.at("style").value("fontWeight", std::string("normal")) == "bold"));
            retained();
        };
        fieldsMatch();
        auto &toggleAction = editAction(*editor, "toggleProperties");
        CHECK(!toggleAction.isEnabled() && toggleAction.shortcuts().isEmpty());
        const Json baseline = exported(*editor);
        const QTransform zoom = view.transform();
        const QPointF center = view.mapToScene(view.viewport()->rect().center());
        shortcut(*editor, Qt::Key_P);
        toggleAction.trigger();
        scroll->ensureWidgetVisible(tags);
        tags->setFocus(); pump();
        CHECK(tags->hasFocus());
        QTest::keyClick(tags, Qt::Key_Escape);
        editor->collapseNodeProperties();
        CHECK(tags->hasFocus() && scroll->isVisible() && panel->isVisible());
        CHECK(exported(*editor) == baseline && editor->selectedNodeId() == a);
        CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == center);
        retained();

        trigger(*editor, "editTags");
        CHECK(tags->hasFocus() && panel->isVisible());
        CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == center);
        // External controls commit to the same document/history and retain their identity.
        QSignalSpy changed(editor, &Editor::documentChanged);
        std::vector<Json> history{baseline};
        Json expected = baseline;
        auto expectedNode = [&]() -> Json & {
            for (auto &node : expected.at("nodes")) if (node.at("id") == "a") return node;
            throw std::runtime_error("Missing external properties node");
        };
        auto edited = [&] {
            CHECK(exported(*editor) == expected && editor->selectedNodeId() == a);
            CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == center);
            history.push_back(expected);
            CHECK(changed.size() == int(history.size()) - 1);
            fieldsMatch();
        };
        tags->setText(QStringLiteral("external, tags"));
        expectedNode()["tags"] = Json::array({"external", "tags"}); edited();
        icons->setText(QStringLiteral("external-icon"));
        expectedNode()["icons"] = Json::array({"external-icon"}); edited();
        url->setText(QStringLiteral("https://example.org/external"));
        expectedNode()["hyperLink"] = "https://example.org/external"; edited();
        image->setText(QStringLiteral("memory:external"));
        expectedNode()["image"] = Json{{"url", "memory:external"}, {"width", 0}, {"height", 0}}; edited();
        scroll->ensureWidgetVisible(note);
        note->setFocus(); pump();
        QApplication::clipboard()->setText(QStringLiteral("External note\nSecond line"));
        QTest::keyClick(note, Qt::Key_V, Qt::ControlModifier);
        expectedNode()["note"] = "External note\nSecond line"; edited();
        note->selectAll();
        CHECK(editor->selectedText() == QStringLiteral("External note\nSecond line"));
        size->setCurrentIndex(size->findData(24.0));
        expectedNode()["style"]["fontSize"] = 24; edited();
        bold->click();
        expectedNode()["style"]["fontWeight"] = "bold"; edited();
        for (int i = int(history.size()) - 2; i >= 0; --i) {
            CHECK(editor->undo() && exported(*editor) == history[static_cast<size_t>(i)]);
            fieldsMatch();
        }
        for (size_t i = 1; i < history.size(); ++i) {
            CHECK(editor->redo() && exported(*editor) == history[i]);
            fieldsMatch();
        }
        CHECK(editor->selectNode(b)); fieldsMatch();
        CHECK(editor->selectNode(a)); fieldsMatch();
        window.resize(1380, 940); editor->resize(700, 600); pump(); retained();

        // Policy is based on the editor/model, not the borrowed QWidget parent.
        auto rejectEdits = [&] {
            const Json before = exported(*editor);
            const auto count = changed.size();
            tags->setText(QStringLiteral("Forbidden tags"));
            icons->setText(QStringLiteral("Forbidden icons"));
            url->setText(QStringLiteral("Forbidden URL"));
            image->setText(QStringLiteral("Forbidden image"));
            note->setPlainText(QStringLiteral("Forbidden note"));
            size->setCurrentIndex(size->findData(32.0));
            bold->setChecked(!bold->isChecked());
            CHECK(exported(*editor) == before && changed.size() == count);
            retained();
        };
        editor->setEnabled(false);
        CHECK(tags->isReadOnly() && note->isReadOnly() && !bold->isEnabled() && !size->isEnabled());
        rejectEdits(); fieldsMatch();
        editor->setEnabled(true);
        editor->setReadOnly(true);
        CHECK(tags->isReadOnly() && note->isReadOnly() && !bold->isEnabled() && !size->isEnabled());
        rejectEdits(); fieldsMatch();
        editor->setReadOnly(false);
        CHECK(!tags->isReadOnly() && !note->isReadOnly() && bold->isEnabled() && size->isEnabled());
        shortcut(*editor, Qt::Key_Escape);
        CHECK(editor->selectedNodeId().isEmpty() && panel->isVisible() && scroll->isVisible());
        CHECK(tags->text().isEmpty() && note->toPlainText().isEmpty() && !tags->isEnabled() && !bold->isEnabled());
        CHECK(icons->text().isEmpty() && url->text().isEmpty() && image->text().isEmpty());
        CHECK(size->currentData().toDouble() == 0 && !bold->isChecked());
        rejectEdits();
        CHECK(tags->text().isEmpty() && note->toPlainText().isEmpty());
        CHECK(editor->selectLink(QStringLiteral("l1")));
        CHECK(panel->isVisible() && !tags->isEnabled()); rejectEdits();
        CHECK(editor->selectNode(a));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, labelPoint(*editor, QStringLiteral("Beta")));
        pump();
        CHECK(editor->selectedNodeIds().size() == 2 && panel->isVisible() && !tags->isEnabled());
        rejectEdits();
        CHECK(editor->selectNode(a)); fieldsMatch();

        // Neither presentation commands nor model refreshes override the host's hiding.
        dock->hide();
        CHECK(editor->selectNode(b));
        CHECK(editor->updateNode(b, encoded(Json{{"note", "Changed while dock hidden"}})));
        for (const char *name : {"textColor", "fillColor", "editTags", "editIcons", "editNote"}) trigger(*editor, name);
        editor->collapseNodeProperties(); pump();
        CHECK(dock->isHidden() && !panel->isVisible()); retained();
        panel->hide(); dock->show();
        CHECK(editor->selectNode(a));
        CHECK(editor->loadJson(encoded(expected)) && editor->selectNode(a));
        CHECK(panel->isHidden()); fieldsMatch();
        for (const char *name : {"textColor", "fillColor", "editTags", "editIcons", "editNote"}) trigger(*editor, name);
        CHECK(editor->newDocument() && panel->isHidden());
        CHECK(editor->loadJson(encoded(expected)) && editor->selectNode(a) && panel->isHidden());
        panel->show(); pump();
        CHECK(scroll->isVisible() && tags->isEnabled()); fieldsMatch();
    }
    for (bool editorFirst : {false, true}) for (bool deferred : {false, true}) {
        QWidget external;
        QPointer<Editor> editor = new RawLabelEditor(config);
        QPointer<QWidget> panel = editor->nodePropertiesPanel();
        panel->setParent(&external);
        int destroyed = 0;
        QObject::connect(panel, &QObject::destroyed, &external, [&] { ++destroyed; });
        if (editorFirst) {
            if (deferred) editor->deleteLater(); else delete editor.data();
        } else {
            if (deferred) panel->deleteLater(); else delete panel.data();
        }
        pump();
        CHECK(!panel && destroyed == 1);
        if (!editorFirst) {
            CHECK(editor && !editor->nodePropertiesPanel());
            for (const char *name : {"toggleBold", "toggleItalic", "resetStyle", "textColor", "fillColor",
                                     "editTags", "editIcons", "editNote", "toggleProperties"})
                editAction(*editor, name).trigger();
            editor->collapseNodeProperties();
            CHECK(editor->loadJson(encoded(editorFixture())) && editor->selectNode(a));
            CHECK(editor->renameNode(a, QStringLiteral("Still editable")) && editor->undo() && editor->redo());
            editor->setEnabled(false); editor->setEnabled(true);
            editor->setReadOnly(true); editor->setReadOnly(false);
            CHECK(!editor->nodePropertiesPanel());
            delete editor.data();
        }
        CHECK(!editor && destroyed == 1);
    }
    // QObject's shared-parent teardown may encounter either the editor or its visual host first.
    for (bool hostFirst : {false, true}) {
        auto *parent = new QWidget;
        QPointer<QWidget> external = hostFirst ? new QWidget(parent) : nullptr;
        QPointer<Editor> editor = new RawLabelEditor(config, parent);
        if (!external) external = new QWidget(parent);
        QPointer<QWidget> panel = editor->nodePropertiesPanel();
        panel->setParent(external);
        int destroyed = 0;
        QObject::connect(panel, &QObject::destroyed, qApp, [&] { ++destroyed; });
        delete parent; pump();
        CHECK(!editor && !panel && !external && destroyed == 1);
    }
    // Direct commands allow immediate teardown; QLineEdit::setText must finish first.
    for (bool viaInput : {false, true}) for (bool closeEditor : {false, true}) {
        QWidget external;
        QPointer<Editor> editor = new RawLabelEditor(config);
        QPointer<QWidget> panel = editor->nodePropertiesPanel();
        panel->setParent(&external);
        QPointer<QLineEdit> tags = panel->findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        CHECK(tags);
        Json observed;
        QObject::connect(editor, &Editor::documentChanged, &external, [&] {
            observed = exported(*editor);
            if (viaInput) {
                if (closeEditor) editor->deleteLater(); else panel->deleteLater();
            } else {
                if (closeEditor) delete editor.data(); else delete panel.data();
            }
        });
        if (viaInput) {
            tags->setText(QStringLiteral("Close from property commit"));
            CHECK(editor && panel && tags);
            pump();
        } else {
            CHECK(editor->updateNode(QStringLiteral("root"),
                encoded(Json{{"tags", Json::array({"Close from property commit"})}})));
        }
        CHECK(!panel && !tags);
        CHECK(record(observed, "nodes", QStringLiteral("root")).at("tags") ==
            Json::array({"Close from property commit"}));
        if (closeEditor) CHECK(!editor);
        else {
            CHECK(editor && !editor->nodePropertiesPanel());
            CHECK(exported(*editor) == observed);
            delete editor.data();
        }
    }
}

static void properties_case() {
    properties_visibility_case();
    emoji_category_popup_case();
    emoji_productivity_case();
    separated_properties_case();
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        auto &view = graphics(editor);
        auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        auto *scroll = panel->findChild<QScrollArea *>();
        auto *toggle = panel->findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
        CHECK(scroll && toggle && scroll->isHidden() && !toggle->isChecked());
        CHECK(editor.nodePropertiesPanel() == nullptr && panel->isVisible());
        CHECK(editor.selectNode(QStringLiteral("a")) && !toggle->isChecked());
        CHECK(editor.selectLink(QStringLiteral("l1")) && panel->isHidden());
        CHECK(editor.newDocument() && !toggle->isChecked());
        CHECK(editor.loadJson(encoded(editorFixture())) && scroll->isHidden());
        expandProperties(editor);
        const auto before = exported(editor);
        const auto selection = editor.selectedNodeId();
        const auto zoom = editor.zoomFactor();
        QSignalSpy changed(&editor, &Editor::documentChanged);
        editor.collapseNodeProperties();
        editor.collapseNodeProperties();
        pump();
        CHECK(scroll->isHidden() && panel->width() <= toggle->width() + 8);
        CHECK(view.hasFocus() && editor.selectedNodeId() == selection);
        CHECK(editor.zoomFactor() == zoom && exported(editor) == before && changed.isEmpty());
        toggle->click();
        CHECK(scroll->isVisible());
        editor.clearSelection();
        editor.setReadOnly(true);
        editor.collapseNodeProperties();
        CHECK(editor.selectedNodeId().isEmpty() && !panel->isVisible());
        CHECK(editor.selectNode(selection));
        pump();
        CHECK(panel->isVisible() && scroll->isHidden());
        CHECK(editor.isReadOnly() && exported(editor) == before && changed.isEmpty());
    }
    {
        RawLabelEditor imageEditor;
        CHECK(imageEditor.loadJson(encoded(editorFixture())));
        showEditor(imageEditor, QSize(1100, 900));
        expandProperties(imageEditor);
        auto *field = imageEditor.findChild<QLineEdit *>(QStringLiteral("nodeImageUrl"));
        auto *url = imageEditor.findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
        auto *note = imageEditor.findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
        auto *scroll = imageEditor.findChild<QScrollArea *>();
        CHECK(field && url && note && scroll);
        CHECK(field->text() == QString::fromUtf8("memory:絵"));
        Json expected = exported(imageEditor);
        auto imageRecord = [&](const char *id) -> Json & {
            for (auto &node : expected.at("nodes")) if (node.at("id") == id) return node;
            throw std::runtime_error("Missing image node");
        };
        QSignalSpy changed(&imageEditor, &Editor::documentChanged);
        QSignalSpy selected(&imageEditor, &Editor::selectionChanged);
        const auto zoom = graphics(imageEditor).transform();
        field->setText(QString::fromUtf8("memory:replacement-世界"));
        imageRecord("r")["image"]["url"] = "memory:replacement-世界";
        CHECK(exported(imageEditor) == expected && changed.size() == 1 && selected.isEmpty());
        const QString rootTopic = qs(record(expected, "nodes", QStringLiteral("r")).at("topic"));
        const qreal withImageHeight = topicRect(imageEditor, rootTopic).height();
        field->clear();
        imageRecord("r")["image"]["url"] = "";
        CHECK(exported(imageEditor) == expected && changed.size() == 2);
        CHECK(nodeImageItem(imageEditor, rootTopic, QString::fromUtf8("memory:replacement-世界")) == nullptr);
        CHECK(topicRect(imageEditor, rootTopic).height() < withImageHeight);
        CHECK(imageEditor.selectNode(QStringLiteral("a")));
        CHECK(field->text().isEmpty() && changed.size() == 2);
        field->setText(QStringLiteral("memory:new"));
        imageRecord("a")["image"] = Json{{"url", "memory:new"}, {"width", 0}, {"height", 0}};
        CHECK(exported(imageEditor) == expected && changed.size() == 3);
        auto *accessible = QAccessible::queryAccessibleInterface(field);
        CHECK(accessible != nullptr);
        accessible->setText(QAccessible::Value, QStringLiteral("memory:accessible"));
        imageRecord("a")["image"]["url"] = "memory:accessible";
        CHECK(exported(imageEditor) == expected && changed.size() == 4);
        CHECK(graphics(imageEditor).transform() == zoom);
        scroll->ensureWidgetVisible(url);
        url->setFocus();
        QTest::keyClick(url, Qt::Key_Tab);
        CHECK(field->hasFocus());
        scroll->ensureWidgetVisible(field);
        pump();
        CHECK(scroll->viewport()->rect().contains(field->mapTo(scroll->viewport(), field->rect().center())));
        QTest::keyClick(field, Qt::Key_Tab);
        CHECK(note->hasFocus());
        CHECK(imageEditor.selectNode(QStringLiteral("r")));
        field->setFocus();
        CHECK(field->hasFocus());
        imageRecord("r")["image"] = Json{{"url", "memory:loaded"}, {"width", 33}, {"height", 22}};
        CHECK(imageEditor.loadJson(encoded(expected)));
        CHECK(field->text() == QStringLiteral("memory:loaded") && changed.size() == 5);
        CHECK(exported(imageEditor) == expected);
    }
    RawLabelEditor editor;
    Json input = editorFixture();
    for (auto &entry : input.at("nodes")) if (entry.at("id") == "a")
        entry["style"] = Json{{"custom", {{"integer", UINT64_C(9007199254740993)}, {"nested", Json::array({true, "opaque"})}}}};
    CHECK(editor.loadJson(encoded(input)));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor, QSize(1100, 900));
    expandProperties(editor);
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
    RawLabelEditor roundtrip;
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
        RawLabelEditor colorsEditor;
        Json colorsInput = editorFixture();
        jsonNode(colorsInput, "a")["style"] = Json{
            {"fontSize", 24}, {"fontWeight", "bold"}, {"fontStyle", "italic"},
            {"color", "#2980b9"}, {"background", "#e74c3c"},
            {"custom", {{"integer", UINT64_C(9007199254740993)}, {"nested", Json::array({true, "opaque"})}}}};
        jsonNode(colorsInput, "b")["style"]["color"] = "#d7bde2";
        CHECK(colorsEditor.loadJson(encoded(colorsInput)));
        showEditor(colorsEditor, QSize(1100, 900));
        CHECK(colorsEditor.selectNode(QStringLiteral("a")));
        expandProperties(colorsEditor);
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
        RawLabelEditor tagged, untagged;
        Json emptyTags = editorFixture();
        jsonNode(emptyTags, "r")["tags"] = Json::array();
        CHECK(tagged.loadJson(encoded(editorFixture())) && untagged.loadJson(encoded(emptyTags)));
        untagged.setFont(tagged.font());
        CHECK(tagged.setLayoutDirection(Editor::LayoutDirection::Right));
        CHECK(untagged.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(untagged, QSize(1100, 900));
        showEditor(tagged, QSize(1100, 900));
        CHECK(tagged.selectNode(QStringLiteral("r")));
        expandProperties(tagged);
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
    RawLabelEditor editor;
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
    auto rootCentered = [&] {
        return QLineF(view.mapFromScene(topicRect(editor, rootTopic).center()),
                      view.viewport()->rect().center()).length() <= 2.0;
    };
    auto checkFocused = [&] {
        CHECK(editor.selectedNodeId() == QStringLiteral("r") && editor.selectedLinkId().isEmpty());
        CHECK(rootCentered() && view.transform() == zoom && view.hasFocus());
    };

    editor.clearSelection();
    view.clearFocus();
    CHECK(!view.hasFocus());
    trigger(editor, "selectRoot");
    pump();
    checkFocused();
    CHECK(editor.selectLink(QStringLiteral("l1")));
    trigger(editor, "selectRoot");
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

static void shift_scroll_case() {
    const Json input{{"schemaVersion", 1}, {"rootId", "r"},
                     {"nodes", Json::array({{{"id", "r"}, {"topic", "Root"}, {"children", {"a"}}},
                                            {{"id", "a"}, {"topic", "Alpha"}}})},
                     {"crossLinks", Json::array()}};
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(input)));
    showEditor(editor);
    trigger(editor, "resetZoom");
    CHECK(editor.selectNode(QStringLiteral("a")));
    auto *toggle = editor.findChild<QToolButton *>(QStringLiteral("nodePropertiesToggle"));
    CHECK(toggle != nullptr);
    if (toggle->isChecked()) toggle->click();
    pump();
    auto &view = graphics(editor);
    auto *horizontal = view.horizontalScrollBar();
    auto *vertical = view.verticalScrollBar();
    const QRectF padded = view.sceneRect().adjusted(-2000, -2000, 2000, 2000);
    view.setSceneRect(padded);
    auto midpoint = [&] {
        horizontal->setValue((horizontal->minimum() + horizontal->maximum()) / 2);
        vertical->setValue((vertical->minimum() + vertical->maximum()) / 2);
        pump();
    };
    midpoint();
    const int startX = horizontal->value(), startY = vertical->value();
    CHECK(startX > horizontal->minimum() && startX < horizontal->maximum());
    CHECK(startY > vertical->minimum() && startY < vertical->maximum());
    const QTransform transform = view.transform();
    CHECK(std::abs(transform.m11() - 1.0) < 1e-6);
    const Json original = exported(editor);
    const QString nodeId = editor.selectedNodeId(), linkId = editor.selectedLinkId();
    QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
    auto unchanged = [&] {
        CHECK(exported(editor) == original);
        CHECK(editor.selectedNodeId() == nodeId && editor.selectedLinkId() == linkId);
        CHECK(changed.isEmpty() && selected.isEmpty());
    };
    auto horizontalOnly = [&](int y) {
        CHECK(vertical->value() == y && view.transform() == transform);
        unchanged();
    };
    auto inject = [&](QPoint angle, QPoint pixels, Qt::KeyboardModifiers modifiers,
                      Qt::ScrollPhase phase = Qt::NoScrollPhase, bool inverted = false) {
        const QPoint point = blankPoint(view);
        CHECK(view.viewport()->childAt(point) == nullptr);
        QWheelEvent event(QPointF(point), QPointF(view.viewport()->mapToGlobal(point)), pixels,
                          angle, Qt::NoButton, modifiers, phase, inverted);
        QCoreApplication::sendEvent(view.viewport(), &event);
        pump();
    };
    auto shift = [&](int delta) {
        const QPoint point = blankPoint(view);
        CHECK(view.viewport()->childAt(point) == nullptr);
        wheel(view, point, delta, Qt::ShiftModifier);
    };

    // Match native horizontal line stepping rather than Shift's default page step.
    inject(QPoint(-120, 0), {}, Qt::NoModifier);
    const int nativeStep = horizontal->value() - startX;
    CHECK(nativeStep > 0);
    horizontalOnly(startY);
    midpoint();
    shift(-120);
    CHECK(horizontal->value() == startX + nativeStep);
    horizontalOnly(startY);
    shift(120);
    CHECK(horizontal->value() == startX);
    horizontalOnly(startY);
    for (int i = 0; i < 4; ++i) {
        shift(-30);
        horizontalOnly(startY);
    }
    CHECK(horizontal->value() == startX + nativeStep);

    midpoint();
    inject({}, QPoint(0, -17), Qt::ShiftModifier);
    CHECK(horizontal->value() == startX + 17);
    horizontalOnly(startY);
    inject({}, QPoint(0, 17), Qt::ShiftModifier);
    CHECK(horizontal->value() == startX);
    horizontalOnly(startY);
    inject(QPoint(0, -120), QPoint(0, -17), Qt::ShiftModifier);
    CHECK(horizontal->value() == startX + 17);
    horizontalOnly(startY);
    midpoint();
    inject({}, QPoint(0, -17), Qt::ShiftModifier, Qt::NoScrollPhase, true);
    CHECK(horizontal->value() == startX + 17);
    horizontalOnly(startY);
    midpoint();
    const bool invertedControls = horizontal->invertedControls();
    horizontal->setInvertedControls(false);
    inject({}, QPoint(0, -17), Qt::ShiftModifier);
    const int reversedPixel = horizontal->value() - startX;
    midpoint();
    shift(-120);
    const int reversedAngle = horizontal->value() - startX;
    horizontal->setInvertedControls(invertedControls);
    CHECK(reversedPixel == -17 && reversedAngle == -nativeStep);
    horizontalOnly(startY);

    midpoint();
    wheel(view, blankPoint(view), -120, Qt::NoModifier);
    CHECK(horizontal->value() == startX && vertical->value() > startY);
    CHECK(view.transform() == transform);
    unchanged();
    midpoint();
    inject(QPoint(-120, 0), {}, Qt::ShiftModifier);
    CHECK(horizontal->value() > startX);
    horizontalOnly(startY);
    midpoint();
    inject(QPoint(-240, -120), {}, Qt::ShiftModifier);
    CHECK(horizontal->value() > startX);
    horizontalOnly(startY);
    midpoint();
    const QPoint pointer = blankPoint(view);
    const QPointF anchor = view.mapToScene(pointer);
    wheel(view, pointer, 120, Qt::ControlModifier | Qt::ShiftModifier);
    CHECK(std::abs(view.transform().m11() - 1.2) < 1e-6);
    CHECK(QLineF(anchor, view.mapToScene(pointer)).length() < 3.0);
    unchanged();
    trigger(editor, "resetZoom");
    view.setSceneRect(padded);
    midpoint();

    // Exhausted horizontal ranges must not turn back into vertical scrolling.
    for (bool maximum : {true, false}) {
        const int edge = maximum ? horizontal->maximum() : horizontal->minimum();
        horizontal->setValue(edge);
        shift(maximum ? -120 : 120);
        CHECK(horizontal->value() == edge);
        horizontalOnly(startY);
        inject({}, QPoint(0, maximum ? -17 : 17), Qt::ShiftModifier);
        CHECK(horizontal->value() == edge);
        horizontalOnly(startY);
    }
    const int minimum = horizontal->minimum(), maximum = horizontal->maximum();
    horizontal->setRange(0, 0);
    shift(-120);
    CHECK(horizontal->value() == 0);
    horizontalOnly(startY);
    inject({}, QPoint(0, -17), Qt::ShiftModifier);
    CHECK(horizontal->value() == 0);
    horizontalOnly(startY);
    horizontal->setRange(minimum, maximum);
    midpoint();
    for (auto phase : {Qt::ScrollBegin, Qt::ScrollEnd}) {
        inject({}, {}, Qt::ShiftModifier, phase);
        CHECK(horizontal->value() == startX);
        horizontalOnly(startY);
    }

    // QWidget RTL must keep pixel/angle direction consistent with native x input.
    view.setLayoutDirection(Qt::RightToLeft);
    midpoint();
    const int rtlX = horizontal->value(), rtlY = vertical->value();
    inject(QPoint(-120, 0), {}, Qt::NoModifier);
    const int rtlStep = horizontal->value() - rtlX;
    CHECK(rtlStep != 0);
    horizontalOnly(rtlY);
    midpoint();
    shift(-120);
    CHECK(horizontal->value() == rtlX + rtlStep);
    horizontalOnly(rtlY);
    midpoint();
    inject({}, QPoint(0, -17), Qt::ShiftModifier);
    CHECK(horizontal->value() == rtlX + (rtlStep > 0 ? 17 : -17));
    horizontalOnly(rtlY);
    view.setLayoutDirection(Qt::LeftToRight);
    view.setSceneRect(padded);
    view.centerOn(topicRect(editor, QStringLiteral("Alpha")).center());
    pump();

    // Scrolling moves the live draft through scrollContentsBy without committing it.
    shortcut(editor, Qt::Key_F2);
    const QString draft = QStringLiteral("Uncommitted horizontal scroll");
    topicInput(editor).setPlainText(draft);
    pump();
    QPointer<QPlainTextEdit> inputWidget = &topicInput(editor);
    const QRect geometry = inputWidget->geometry();
    const int draftX = horizontal->value(), draftY = vertical->value();
    unchanged();
    inject({}, QPoint(0, -17), Qt::ShiftModifier);
    CHECK(horizontal->value() == draftX + 17);
    CHECK(inputWidget && activeTopicInput(editor) == inputWidget.data());
    CHECK(inputWidget->isVisible() && inputWidget->hasFocus() && inputWidget->toPlainText() == draft);
    CHECK(inputWidget->geometry() == geometry.translated(-17, 0));
    horizontalOnly(draftY);
    topicKey(editor, Qt::Key_Escape);
    CHECK(activeTopicInput(editor) == nullptr);
    unchanged();
}

static Json outlineFixture() {
    Json input{{"schemaVersion", 1}, {"rootId", "root-z"}, {"nodes", Json::array({
        {{"id", "root-z"}, {"topic", "Duplicate"}, {"children", {"chain-1", "sibling-a", "other-parent"}}},
        {{"id", "other-leaf"}, {"topic", "Other hidden"}},
        {{"id", "other-parent"}, {"topic", "Other collapsed"}, {"children", {"other-leaf"}}},
        {{"id", "sibling-a"}, {"topic", "Duplicate"}}
    })}, {"crossLinks", Json::array()}};
    // Storage order deliberately differs from tree preorder; the last node is level 10.
    for (int i = 9; i >= 1; --i) {
        Json entry{{"id", "chain-" + std::to_string(i)},
                   {"topic", i == 9 ? utf8(QString::fromUtf8("Deep target 世界")) : "Duplicate"}};
        if (i < 9) entry["children"] = {"chain-" + std::to_string(i + 1)};
        input["nodes"].push_back(std::move(entry));
    }
    return input;
}

static void outline_navigation_case() {
    const QString root = QStringLiteral("root-z"), outer = QStringLiteral("chain-2");
    const QString deep = QStringLiteral("chain-9"), deepTopic = QString::fromUtf8("Deep target 世界");
    const Json input = outlineFixture();
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(input)));
    CHECK(editor.setExpanded(QStringLiteral("other-parent"), false));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor);
    editor.resetZoom();
    editor.zoom(1.25);
    auto &view = graphics(editor);
    const QTransform zoom = view.transform();
    const Json baseline = exported(editor);
    const QStringList expectedIds{root, QStringLiteral("chain-1"), outer, QStringLiteral("chain-3"),
        QStringLiteral("chain-4"), QStringLiteral("chain-5"), QStringLiteral("chain-6"),
        QStringLiteral("chain-7"), QStringLiteral("chain-8"), deep, QStringLiteral("sibling-a"),
        QStringLiteral("other-parent"), QStringLiteral("other-leaf")};
    const std::vector<int> expectedLevels{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 2, 2, 3};
    auto checkOutline = [&] {
        const auto entries = editor.outline();
        CHECK(entries.size() == expectedIds.size());
        for (int i = 0; i < entries.size(); ++i) {
            CHECK(entries[i].id == expectedIds[i]);
            CHECK(entries[i].level == expectedLevels[static_cast<size_t>(i)]);
            CHECK(entries[i].topic == qs(record(baseline, "nodes", expectedIds[i]).at("topic")));
        }
    };
    CHECK(texts(editor, deepTopic).isEmpty());
    CHECK(texts(editor, QStringLiteral("Other hidden")).isEmpty());
    const QPointF initialCamera = view.mapToScene(view.viewport()->rect().center());
    QSignalSpy selected(&editor, &Editor::selectionChanged), changed(&editor, &Editor::documentChanged);
    checkOutline();
    CHECK(exported(editor) == baseline && changed.isEmpty() && selected.isEmpty());
    CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == initialCamera);

    // Navigation must retain a real history cursor with both Undo and Redo available.
    CHECK(editor.updateNode(root, encoded(Json{{"note", "Current command"}})));
    const Json current = exported(editor);
    CHECK(editor.updateNode(root, encoded(Json{{"note", "Future command"}})));
    const Json future = exported(editor);
    CHECK(editor.undo() && exported(editor) == current && editor.canUndo() && editor.canRedo());
    changed.clear(); selected.clear();
    QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
    auto navigationOnly = [&] {
        CHECK(exported(editor) == current && changed.isEmpty());
        CHECK(editor.canUndo() && editor.canRedo() && undo.isEmpty() && redo.isEmpty());
        CHECK(view.transform() == zoom);
    };
    auto reveal = [&](const QString &id) {
        CHECK(editor.revealNode(id));
        pump();
        CHECK(editor.selectedNodeIds() == QStringList{id} && editor.selectedLinkId().isEmpty());
        navigationOnly();
    };
    reveal(deep);
    CHECK(!texts(editor, deepTopic).isEmpty());
    CHECK(view.viewport()->rect().adjusted(-1, -1, 1, 1).contains(
        view.mapFromScene(topicRect(editor, deepTopic)).boundingRect()));
    checkOutline();
    // Equal topics are not identities: root, a deep ancestor and a sibling all remain distinct.
    reveal(QStringLiteral("chain-8"));
    reveal(QStringLiteral("sibling-a"));
    reveal(root);
    reveal(deep);
    auto rejectedReveal = [&](const QString &id) {
        const Json before = exported(editor);
        const QStringList nodes = editor.selectedNodeIds();
        const QString link = editor.selectedLinkId();
        const QPointF camera = view.mapToScene(view.viewport()->rect().center());
        const auto selectionCount = selected.size(), changes = changed.size();
        CHECK(!editor.revealNode(id));
        pump();
        CHECK(exported(editor) == before && changed.size() == changes && selected.size() == selectionCount);
        CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == link);
        CHECK(view.transform() == zoom);
        // Showing the diagnostic can change viewport parity; scrollbars quantize to pixels.
        CHECK(QLineF(view.mapToScene(view.viewport()->rect().center()), camera).length() <=
              1.0 / zoom.m11() + 1e-6);
    };
    rejectedReveal(QStringLiteral("absent"));
    rejectedReveal(QStringLiteral("CHAIN-9"));
    navigationOnly();

    // Explicit folding and reveal share the same local state and retain the semantic cursor.
    CHECK(editor.setExpanded(outer, false));
    pump();
    CHECK(texts(editor, deepTopic).isEmpty());
    navigationOnly();
    reveal(deep);
    CHECK(editor.selectNode(outer));
    shortcut(editor, Qt::Key_Space);
    CHECK(texts(editor, deepTopic).isEmpty());
    navigationOnly();
    CHECK(editor.redo() && exported(editor) == future);
    CHECK(editor.undo() && exported(editor) == current);
    changed.clear(); undo.clear(); redo.clear();

    CHECK(texts(editor, deepTopic).isEmpty());
    // Revealed surviving IDs stay open through semantic Undo/Redo as well.
    reveal(deep);
    CHECK(editor.setExpanded(outer, true));
    navigationOnly();
    CHECK(editor.redo() && exported(editor) == future);
    CHECK(!texts(editor, deepTopic).isEmpty());
    CHECK(editor.undo() && exported(editor) == current);
    CHECK(!texts(editor, deepTopic).isEmpty() && view.transform() == zoom);

    editor.setReadOnly(true);
    changed.clear();
    CHECK(editor.revealNode(deep));
    pump();
    CHECK(editor.selectedNodeId() == deep && !texts(editor, deepTopic).isEmpty());
    CHECK(editor.revealNode(QStringLiteral("other-leaf")));
    CHECK(editor.selectedNodeId() == QStringLiteral("other-leaf"));
    CHECK(!texts(editor, QStringLiteral("Other hidden")).isEmpty());
    checkOutline();
    CHECK(exported(editor) == current && changed.isEmpty() && view.transform() == zoom);
    rejectedReveal(QStringLiteral("absent"));
    editor.setReadOnly(false);
    CHECK(editor.canUndo() && editor.canRedo());

    // Removed IDs lose their local folds; surviving ancestor folds remain through Undo.
    CHECK(editor.setExpanded(outer, false));
    CHECK(editor.setExpanded(QStringLiteral("chain-1"), false));
    CHECK(editor.selectNode(root));
    CHECK(editor.removeNode(outer));
    const auto pruned = editor.outline();
    const QStringList remaining{root, QStringLiteral("chain-1"), QStringLiteral("sibling-a"),
        QStringLiteral("other-parent"), QStringLiteral("other-leaf")};
    CHECK(pruned.size() == remaining.size());
    for (int i = 0; i < pruned.size(); ++i) CHECK(pruned[i].id == remaining[i]);
    rejectedReveal(deep);
    CHECK(editor.undo() && exported(editor) == current);
    CHECK(texts(editor, deepTopic).isEmpty());
    checkOutline();
    CHECK(editor.setExpanded(QStringLiteral("chain-1"), true));
    CHECK(!texts(editor, deepTopic).isEmpty());
    CHECK(editor.revealNode(deep) && !texts(editor, deepTopic).isEmpty());

    // Reload reinitializes configured depth, not prior ad hoc folds.
    CHECK(editor.loadJson(encoded(current)));
    CHECK(texts(editor, deepTopic).isEmpty() && !texts(editor, QStringLiteral("Other hidden")).isEmpty());
    CHECK(!editor.canUndo() && !editor.canRedo());
    checkOutline();
    CHECK(editor.newDocument(QStringLiteral("Replacement")));
    const auto replacement = editor.outline();
    CHECK(replacement.size() == 1 && replacement.front().id == QStringLiteral("root"));
    CHECK(replacement.front().level == 1 && replacement.front().topic == QStringLiteral("Replacement"));
    // New/Open may fit the camera; rejection must preserve that new camera rather than the old one.
    const QTransform replacementZoom = view.transform();
    const QPointF replacementCamera = view.mapToScene(view.viewport()->rect().center());
    const Json replacementDocument = exported(editor);
    const auto selectionCount = selected.size(), changes = changed.size();
    CHECK(!editor.revealNode(deep));
    CHECK(exported(editor) == replacementDocument && editor.selectedNodeId() == QStringLiteral("root"));
    CHECK(selected.size() == selectionCount && changed.size() == changes);
    CHECK(view.transform() == replacementZoom &&
          view.mapToScene(view.viewport()->rect().center()) == replacementCamera);
}

static Json findFixture() {
    Json input = outlineFixture();
    setTopic(input, "root-z", QStringLiteral("Needle root"));
    setTopic(input, "chain-1", QStringLiteral("Plain branch"));
    setTopic(input, "chain-2", QStringLiteral("needle branch"));
    setTopic(input, "chain-3", QStringLiteral("Between"));
    setTopic(input, "chain-4", QStringLiteral("Needle Needle"));
    setTopic(input, "chain-5", QStringLiteral("Hidden middle"));
    setTopic(input, "chain-6", QStringLiteral("nEeDlE mixed"));
    setTopic(input, "chain-7", QStringLiteral("Plain descendant"));
    setTopic(input, "chain-8", QStringLiteral("Needle narrow"));
    setTopic(input, "chain-9", QString::fromUtf8("Needle deep 世界"));
    setTopic(input, "sibling-a", QStringLiteral("Needle sibling"));
    setTopic(input, "other-leaf", QStringLiteral("needle endpoint"));
    input["crossLinks"] = Json::array({
        {{"id", "z-link"}, {"source", "root-z"}, {"target", "chain-1"}, {"directed", false}, {"topic", "Needle z"}},
        {{"id", "m-link"}, {"source", "sibling-a"}, {"target", "other-parent"}, {"directed", false}, {"topic", "needle lower link"}},
        {{"id", "a-link"}, {"source", "chain-9"}, {"target", "other-leaf"}, {"directed", false}, {"topic", "Needle endpoints"}}
    });
    return input;
}

static void checkFind(Editor &editor, const m3::qt::FindResult &result, int total, int index,
                      const QString &id, bool link = false) {
    pump();
    CHECK(result.totalMatches == total && result.currentMatch == index);
    if (link) CHECK(editor.selectedNodeIds().isEmpty() && editor.selectedLinkId() == id);
    else CHECK(editor.selectedNodeIds() == QStringList{id} && editor.selectedLinkId().isEmpty());
}

static void find_case() {
    const QString query = QStringLiteral("Needle"), deep = QStringLiteral("chain-9");
    const QString endpoint = QStringLiteral("other-leaf"), link = QStringLiteral("a-link");
    const Json input = findFixture();
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setExpanded(QStringLiteral("other-parent"), false));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(editor);
        editor.resetZoom(); editor.zoom(1.25);
        auto &view = graphics(editor);
        const QTransform zoom = view.transform();
        const Json original = exported(editor);
        editor.setReadOnly(true);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
        CHECK(texts(editor, QString::fromUtf8("Needle deep 世界")).isEmpty());
        CHECK(texts(editor, QStringLiteral("needle endpoint")).isEmpty());
        checkFind(editor, editor.findText(QStringLiteral("endpoints"), Qt::CaseSensitive), 1, 0, link, true);
        CHECK(!texts(editor, QString::fromUtf8("Needle deep 世界")).isEmpty());
        CHECK(!texts(editor, QStringLiteral("needle endpoint")).isEmpty());
        CHECK(view.viewport()->rect().intersects(
            view.mapFromScene(textItem(editor, QStringLiteral("Needle endpoints"))->sceneBoundingRect()).boundingRect()));
        CHECK(exported(editor) == original && changed.isEmpty() && view.transform() == zoom);
        CHECK(!editor.canUndo() && !editor.canRedo() && undo.isEmpty() && redo.isEmpty());
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setExpanded(QStringLiteral("other-parent"), false)); // Search the nodes from collapsed presentation again.
        editor.resetZoom(); editor.zoom(1.25);
        changed.clear();
        const QStringList sensitive{QStringLiteral("root-z"), QStringLiteral("chain-4"),
            QStringLiteral("chain-8"), deep, QStringLiteral("sibling-a"), link, QStringLiteral("z-link")};
        for (int i = 0; i < sensitive.size(); ++i)
            checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, i, sensitive[i], i >= 5);
        // "Needle Needle" counts once. Direction changes advance from the same cursor and wrap.
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 0, sensitive[0]);
        checkFind(editor, editor.findText(query, Qt::CaseSensitive, true), 7, 6, sensitive[6], true);
        for (int i = 5; i >= 0; --i)
            checkFind(editor, editor.findText(query, Qt::CaseSensitive, true), 7, i, sensitive[i], i >= 5);
        checkFind(editor, editor.findText(query, Qt::CaseSensitive, true), 7, 6, sensitive[6], true);
        // A changed nonincremental query starts first/last rather than retaining the previous target.
        checkFind(editor, editor.findText(QStringLiteral("Needle "), Qt::CaseSensitive, true),
                  7, 6, sensitive[6], true);
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 0, sensitive[0]);
        checkFind(editor, editor.findText(QStringLiteral("needle"), Qt::CaseSensitive),
                  3, 0, QStringLiteral("chain-2"));
        // A case-mode change invalidates the cache even when the query bytes are unchanged.
        const QStringList insensitive{QStringLiteral("root-z"), QStringLiteral("chain-2"),
            QStringLiteral("chain-4"), QStringLiteral("chain-6"), QStringLiteral("chain-8"), deep,
            QStringLiteral("sibling-a"), endpoint, link, QStringLiteral("m-link"), QStringLiteral("z-link")};
        for (int i = 0; i < insensitive.size(); ++i)
            checkFind(editor, editor.findText(QStringLiteral("needle"), Qt::CaseInsensitive),
                      11, i, insensitive[i], i >= 8);
        checkFind(editor, editor.findText(QString::fromUtf8("世界"), Qt::CaseSensitive), 1, 0, deep);
        CHECK(view.viewport()->rect().adjusted(-1, -1, 1, 1).contains(
            view.mapFromScene(topicRect(editor, QString::fromUtf8("Needle deep 世界"))).boundingRect()));
        CHECK(exported(editor) == original && changed.isEmpty() && view.transform() == zoom);
        CHECK(!editor.canUndo() && !editor.canRedo() && undo.isEmpty() && redo.isEmpty());
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setExpanded(QStringLiteral("other-parent"), false));
        showEditor(editor);
        editor.resetZoom(); editor.zoom(1.2);
        auto &view = graphics(editor);
        const Json original = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 0, QStringLiteral("root-z"));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 1, QStringLiteral("chain-4"));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 2, QStringLiteral("chain-8"));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive, true, true),
                  7, 2, QStringLiteral("chain-8"));
        checkFind(editor, editor.findText(QStringLiteral("Needle n"), Qt::CaseSensitive, false, true),
                  1, 0, QStringLiteral("chain-8"));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive, false, true),
                  7, 2, QStringLiteral("chain-8"));
        checkFind(editor, editor.findText(QStringLiteral("Needle deep"), Qt::CaseSensitive, true, true),
                  1, 0, deep);
        checkFind(editor, editor.findText(QStringLiteral("needle"), Qt::CaseInsensitive, false, true),
                  11, 5, deep);
        // Incremental fallback is the first match, even when searching backward.
        checkFind(editor, editor.findText(QStringLiteral("needle"), Qt::CaseSensitive, true, true),
                  3, 0, QStringLiteral("chain-2"));
        checkFind(editor, editor.findText(QStringLiteral("needle lower"), Qt::CaseSensitive, false, true),
                  1, 0, QStringLiteral("m-link"), true);
        checkFind(editor, editor.findText(QStringLiteral("needle"), Qt::CaseInsensitive, false, true),
                  11, 9, QStringLiteral("m-link"), true);

        auto preservesSelectionAndCamera = [&](const std::function<void()> &operation) {
            const QStringList nodes = editor.selectedNodeIds();
            const QString selectedLink = editor.selectedLinkId();
            const QTransform zoom = view.transform();
            const QPointF camera = view.mapToScene(view.viewport()->rect().center());
            const auto selectionCount = selected.size();
            operation();
            pump();
            CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
            CHECK(selected.size() == selectionCount && exported(editor) == original && changed.isEmpty());
            CHECK(view.transform() == zoom && view.mapToScene(view.viewport()->rect().center()) == camera);
            CHECK(!editor.canUndo() && !editor.canRedo());
        };
        editor.scrollSteps(2, -1);
        preservesSelectionAndCamera([&] {
            const auto result = editor.findText(QStringLiteral("NEEDLE"), Qt::CaseSensitive);
            CHECK(result.totalMatches == 0 && result.currentMatch == -1);
        });
        preservesSelectionAndCamera([&] {
            const auto result = editor.findText(QStringLiteral("not in any topic"), Qt::CaseInsensitive, true, true);
            CHECK(result.totalMatches == 0 && result.currentMatch == -1);
        });
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 0, QStringLiteral("root-z"));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 1, QStringLiteral("chain-4"));
        preservesSelectionAndCamera([&] { editor.clearFind(); });
        preservesSelectionAndCamera([&] { editor.clearFind(); });
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 0, QStringLiteral("root-z"));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 1, QStringLiteral("chain-4"));
        preservesSelectionAndCamera([&] {
            const auto result = editor.findText(QString(), Qt::CaseSensitive, true, true);
            CHECK(result.totalMatches == 0 && result.currentMatch == -1);
        });
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 7, 0, QStringLiteral("root-z"));
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setExpanded(QStringLiteral("other-parent"), false));
        showEditor(editor);
        auto noMatch = [&](const QString &text) {
            const Json before = exported(editor);
            const QStringList nodes = editor.selectedNodeIds();
            const QString selectedLink = editor.selectedLinkId();
            const QTransform zoom = graphics(editor).transform();
            const QPointF camera = graphics(editor).mapToScene(graphics(editor).viewport()->rect().center());
            QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
            const auto result = editor.findText(text, Qt::CaseSensitive);
            CHECK(result.totalMatches == 0 && result.currentMatch == -1);
            CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
            CHECK(exported(editor) == before && changed.isEmpty() && selected.isEmpty());
            CHECK(graphics(editor).transform() == zoom &&
                  graphics(editor).mapToScene(graphics(editor).viewport()->rect().center()) == camera);
        };
        checkFind(editor, editor.findText(QStringLiteral("Needle deep"), Qt::CaseSensitive), 1, 0, deep);
        CHECK(editor.renameNode(deep, QStringLiteral("Renamed deep")));
        noMatch(QStringLiteral("Needle deep"));
        checkFind(editor, editor.findText(QStringLiteral("Renamed deep"), Qt::CaseSensitive), 1, 0, deep);
        CHECK(editor.undo());
        noMatch(QStringLiteral("Renamed deep"));
        CHECK(editor.redo());
        checkFind(editor, editor.findText(QStringLiteral("Renamed deep"), Qt::CaseSensitive), 1, 0, deep);
        const QString inserted = editor.addNode(QStringLiteral("chain-5"), QStringLiteral("Inserted search target"));
        CHECK(!inserted.isEmpty());
        checkFind(editor, editor.findText(QStringLiteral("Inserted search target"), Qt::CaseSensitive), 1, 0, inserted);
        CHECK(editor.removeNode(inserted));
        noMatch(QStringLiteral("Inserted search target"));

        CHECK(editor.renameNode(QStringLiteral("chain-4"), QStringLiteral("Ordered target")));
        CHECK(editor.renameNode(QStringLiteral("sibling-a"), QStringLiteral("Ordered target")));
        checkFind(editor, editor.findText(QStringLiteral("Ordered target"), Qt::CaseSensitive),
                  2, 0, QStringLiteral("chain-4"));
        CHECK(editor.moveNode(QStringLiteral("sibling-a"), QStringLiteral("root-z"), 0));
        QString previous;
        for (int i = 0; i < 2; ++i) {
            const auto result = editor.findText(QStringLiteral("Ordered target"), Qt::CaseSensitive);
            CHECK(result.totalMatches == 2 && result.currentMatch >= 0 && result.currentMatch < 2);
            const QString id = result.currentMatch == 0 ? QStringLiteral("sibling-a") : QStringLiteral("chain-4");
            CHECK(editor.selectedNodeId() == id && editor.selectedLinkId().isEmpty());
            CHECK(id != previous);
            previous = id;
        }

        checkFind(editor, editor.findText(QStringLiteral("endpoints"), Qt::CaseSensitive), 1, 0, link, true);
        CHECK(editor.updateLink(link, deep, endpoint, false, QStringLiteral("Updated searchable route")));
        noMatch(QStringLiteral("endpoints"));
        checkFind(editor, editor.findText(QStringLiteral("Updated searchable"), Qt::CaseSensitive), 1, 0, link, true);
        CHECK(editor.removeLink(link));
        noMatch(QStringLiteral("Updated searchable"));
        const auto beforeLoad = editor.findText(query, Qt::CaseSensitive);
        CHECK(beforeLoad.totalMatches == 3);
        const Json replacement{{"schemaVersion", 1}, {"rootId", "replacement"}, {"nodes", Json::array({
            {{"id", "replacement"}, {"topic", "Replacement root"}, {"children", {"chain-9"}}},
            {{"id", "chain-9"}, {"topic", "Needle replacement"}}
        })}, {"crossLinks", Json::array()}};
        CHECK(editor.loadJson(encoded(replacement)));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 1, 0, deep);
        CHECK(editor.newDocument(QStringLiteral("Needle fresh")));
        checkFind(editor, editor.findText(query, Qt::CaseSensitive), 1, 0, QStringLiteral("root"));
    }
}

static void zoom_modes_case() {
    Json children = Json::array(), nodes = Json::array();
    for (int i = 0; i < 40; ++i) {
        const std::string id = "zoom-" + std::to_string(i);
        children.push_back(id);
        nodes.push_back(Json{{"id", id}, {"topic", "Tall branch " + std::to_string(i)}});
    }
    nodes.push_back(Json{{"id", "r"}, {"topic", "Tall root"}, {"children", children}});
    const QByteArray tall = encoded(Json{{"schemaVersion", 1}, {"rootId", "r"}, {"nodes", nodes}});
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(tall));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(editor);
        editor.clearSelection();
        auto &view = graphics(editor);
        const QByteArray before = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged), zoomed(&editor, &Editor::zoomChanged);
        QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
        trigger(editor, "fit");
        CHECK(editor.zoomMode() == Editor::ZoomMode::Page && editor.isZoomFit());
        const qreal pageScale = editor.zoomFactor();
        const QRectF bounds = view.scene()->sceneRect();
        CHECK(view.viewport()->rect().adjusted(-3, -3, 3, 3).contains(
            view.mapFromScene(bounds).boundingRect()));
        trigger(editor, "fitWidth");
        CHECK(editor.zoomMode() == Editor::ZoomMode::Width && editor.isZoomFit());
        CHECK(editor.zoomFactor() > pageScale);
        CHECK(qFuzzyCompare(view.transform().m11(), view.transform().m22()));
        CHECK(std::abs(bounds.width() * editor.zoomFactor() - (view.viewport()->width() - 4)) < 1e-6);
        CHECK(bounds.height() * editor.zoomFactor() > view.viewport()->height());
        CHECK(view.verticalScrollBar()->maximum() > view.verticalScrollBar()->minimum());
        CHECK(zoomed.size() == 2 && zoomed.back().at(1).toBool());
        editor.fitToWidth();
        CHECK(zoomed.size() == 2);
        const qreal widthScale = editor.zoomFactor();
        editor.resize(1120, 760);
        pump();
        CHECK(qFuzzyCompare(widthScale, editor.zoomFactor())); // One-shot, not resize-to-fit.
        CHECK(editor.zoomMode() == Editor::ZoomMode::Width);
        wheel(view, view.viewport()->rect().center(), 120, Qt::ControlModifier);
        CHECK(editor.zoomMode() == Editor::ZoomMode::Custom && !editor.isZoomFit());
        CHECK(!zoomed.back().at(1).toBool());
        editor.fitToWidth();
        editor.resetZoom();
        CHECK(editor.zoomMode() == Editor::ZoomMode::Custom && editor.zoomFactor() == 1.0);
        CHECK(!zoomed.back().at(1).toBool());
        // Empty/invalid scene geometry is a no-op, not a remembered fit request.
        const auto camera = view.transform();
        const auto notifications = zoomed.size();
        for (qreal width : {qreal(0), qreal(-1)}) {
            view.scene()->setSceneRect(QRectF(0, 0, width, 100));
            editor.fitToWidth();
            editor.fitToContents();
            CHECK(view.transform() == camera && editor.zoomMode() == Editor::ZoomMode::Custom);
            CHECK(zoomed.size() == notifications);
        }
        view.scene()->setSceneRect(bounds);
        CHECK(editor.toJson() == before && changed.isEmpty() && undo.isEmpty() && redo.isEmpty());
    }
    {
        QWidget host;
        QVBoxLayout layout(&host);
        RawLabelEditor editor(&host);
        layout.addWidget(&editor);
        CHECK(editor.loadJson(tall));
        const QByteArray before = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged), zoomed(&editor, &Editor::zoomChanged);
        editor.fitToWidth();
        CHECK(editor.zoomMode() == Editor::ZoomMode::Width && editor.zoomFactor() == 1.0);
        editor.fitToContents();
        CHECK(editor.zoomMode() == Editor::ZoomMode::Page && editor.zoomFactor() == 1.0);
        editor.fitToWidth();
        CHECK(editor.zoomMode() == Editor::ZoomMode::Width && editor.isZoomFit());
        CHECK(zoomed.size() == 3); // Hidden same-factor Width/Page transitions still publish.
        for (const auto &notification : zoomed) CHECK(notification.at(1).toBool());
        CHECK(editor.setExpanded(QStringLiteral("r"), false)); // Refresh must retain pending Width.
        CHECK(editor.setExpanded(QStringLiteral("r"), true));
        host.resize(860, 620);
        host.show();
        pump();
        auto &view = graphics(editor);
        const QRectF bounds = view.scene()->sceneRect();
        CHECK(editor.zoomMode() == Editor::ZoomMode::Width);
        CHECK(std::abs(bounds.width() * editor.zoomFactor() - (view.viewport()->width() - 4)) < 1e-6);
        CHECK(bounds.height() * editor.zoomFactor() > view.viewport()->height());
        CHECK(zoomed.size() == 4);
        const qreal scale = editor.zoomFactor();
        host.resize(980, 720);
        pump();
        CHECK(qFuzzyCompare(scale, editor.zoomFactor()));
        // Manual zoom/reset supersede a hidden fit and its deferred callback.
        for (bool reset : {false, true}) {
            editor.hide();
            editor.fitToWidth();
            if (reset) editor.resetZoom();
            else editor.zoom(1.0);
            const qreal manualScale = editor.zoomFactor();
            editor.show();
            pump();
            CHECK(editor.zoomMode() == Editor::ZoomMode::Custom && !editor.isZoomFit());
            CHECK(qFuzzyCompare(manualScale, editor.zoomFactor()));
        }
        CHECK(editor.toJson() == before && changed.isEmpty());
    }
    {
        RawLabelEditor editor;
        CHECK(editor.newDocument(QStringLiteral("A wide map for equal-scale fit transitions")));
        showEditor(editor, QSize(640, 480));
        const QByteArray before = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged), zoomed(&editor, &Editor::zoomChanged);
        QList<Editor::ZoomMode> observedModes;
        QObject::connect(&editor, &Editor::zoomChanged, &editor,
                         [&](qreal, bool) { observedModes.append(editor.zoomMode()); });
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Uncommitted camera-only draft"));
        editor.fitToContents();
        const qreal scale = editor.zoomFactor();
        CHECK(scale >= 0.1 && scale <= 4.0);
        editor.fitToWidth();
        CHECK(qFuzzyCompare(scale, editor.zoomFactor()));
        editor.fitToContents();
        CHECK(qFuzzyCompare(scale, editor.zoomFactor()));
        editor.zoom(1.0);
        CHECK(qFuzzyCompare(scale, editor.zoomFactor()));
        editor.fitToWidth();
        CHECK(qFuzzyCompare(scale, editor.zoomFactor()));
        CHECK(observedModes == QList<Editor::ZoomMode>({Editor::ZoomMode::Page, Editor::ZoomMode::Width,
            Editor::ZoomMode::Page, Editor::ZoomMode::Custom, Editor::ZoomMode::Width}));
        CHECK(zoomed.size() == 5 && !zoomed.at(3).at(1).toBool());
        CHECK(editor.hasPendingEdit() && activeTopicInput(editor));
        CHECK(editor.toJson() == before && changed.isEmpty());
        topicKey(editor, Qt::Key_Escape);
        editor.resetZoom();
        CHECK(editor.zoomMode() == Editor::ZoomMode::Custom && editor.zoomFactor() == 1.0);
        CHECK(editor.toJson() == before && changed.isEmpty());
    }
}

static void navigation_case() {
    zoom_modes_case();
    shift_scroll_case();
    focus_root_case();
    empty_space_panning_case();
    Json input = editorFixture();
    for (int i = 0; i < 10; ++i) {
        const std::string id = "scroll-" + std::to_string(i);
        input["nodes"].push_back({{"id", id}, {"topic", "Scroll branch " + std::to_string(i)}});
        input["nodes"][0]["children"].push_back(id);
    }
    RawLabelEditor editor;
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

    auto &reset = editAction(editor, "resetZoom");
    auto &zoomIn = editAction(editor, "zoomIn");
    auto &zoomOut = editAction(editor, "zoomOut");
    auto &fit = editAction(editor, "fit");
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
    RawLabelEditor editor;
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
        const Json baseline = expected;
        CHECK(editor.renameNode(root, QStringLiteral("#")));
        setTopic(expected, "r", QStringLiteral("#"));
        changed.clear();
        shortcut(editor, Qt::Key_F2);
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("##"));
        QPointer<QPlainTextEdit> input = &topicInput(editor);
        input->setPlainText(QStringLiteral("#"));
        CHECK(editor.hasPendingEdit());
        // Different raw text with the same parsed topic still advances the accepted draft baseline.
        CHECK(editor.commitActiveEdit(true));
        CHECK(activeTopicInput(editor) == input.data() && topicInput(editor).toPlainText() == QStringLiteral("#"));
        CHECK(!editor.hasPendingEdit() && exported(editor) == expected && changed.isEmpty());
        CHECK(editor.commitActiveEdit(true) && !editor.hasPendingEdit());
        topicKey(editor, Qt::Key_Return);
        CHECK(exported(editor) == expected && changed.isEmpty() && errors.isEmpty());
        CHECK(!activeTopicInput(editor) && editor.undo() && exported(editor) == baseline && !editor.canUndo());
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
    {
        Json expected = reset();
        const Json baseline = expected;
        CHECK(record(expected, "nodes", root).at("tags") == Json::array({"x", "y", "x"}));
        shortcut(editor, Qt::Key_F2);
        QPointer<QPlainTextEdit> input = &topicInput(editor);
        auto capture = [&](const QString &draft, const QString &topic, const Json &tags, int changes) {
            input->setPlainText(draft);
            CHECK(editor.commitActiveEdit(true));
            pump();
            setTopic(expected, "r", topic);
            for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r") entry["tags"] = tags;
            CHECK(input && activeTopicInput(editor) == input.data() && topicInput(editor).toPlainText() == draft);
            CHECK(!editor.hasPendingEdit() && exported(editor) == expected && changed.size() == changes);
            for (int i = 0; i < 2; ++i) CHECK(editor.commitActiveEdit(true));
            CHECK(exported(editor) == expected && changed.size() == changes && !editor.hasPendingEdit());
        };
        capture(QStringLiteral("Roadmap ##literal #todo #x"), QStringLiteral("Roadmap #literal"),
                Json::array({"x", "y", "x", "todo", "x"}), 1);
        // A property edit made between saves is not owned by the inline hashtag draft.
        const Json externalTags = Json::array({"x", "y", "x", "outside", "todo", "x"});
        CHECK(editor.updateNode(root, encoded(Json{{"tags", externalTags}, {"style", {{"color", "#123456"}}}})));
        for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r") {
            entry["tags"] = externalTags;
            entry["style"]["color"] = "#123456";
        }
        CHECK(activeTopicInput(editor) == input.data() && !editor.hasPendingEdit());
        CHECK(exported(editor) == expected && changed.size() == 2);
        capture(QStringLiteral("Roadmap ##literal #next #x"), QStringLiteral("Roadmap #literal"),
                Json::array({"x", "y", "x", "outside", "next", "x"}), 3);
        capture(QStringLiteral("Roadmap ##literal"), QStringLiteral("Roadmap #literal"),
                Json::array({"x", "y", "x", "outside"}), 4);
        capture(QStringLiteral("Roadmap ##literal #final #final"), QStringLiteral("Roadmap #literal"),
                Json::array({"x", "y", "x", "outside", "final", "final"}), 5);
        topicKey(editor, Qt::Key_Return);
        CHECK(!activeTopicInput(editor) && exported(editor) == expected && changed.size() == 5 && errors.isEmpty());
        // Captures and Enter did not duplicate tags or create additional document undo steps.
        for (int i = 0; i < 5; ++i) CHECK(editor.undo());
        CHECK(exported(editor) == baseline && !editor.canUndo());
        for (int i = 0; i < 5; ++i) CHECK(editor.redo());
        CHECK(exported(editor) == expected && !editor.canRedo());
    }
}

static QPoint linkHandlePoint(Editor &editor, const QString &topic) {
    auto &view = graphics(editor);
    auto *node = ownerItem(textItem(editor, topic));
    const QRectF bounds = node->deviceTransform(view.viewportTransform()).mapRect(node->boundingRect());
    for (auto *child : node->childItems()) {
        if (!child->isVisible() || child->cursor().shape() != Qt::CrossCursor) continue;
        const QRectF rect = child->deviceTransform(view.viewportTransform()).mapRect(child->boundingRect());
        if (rect.center().x() > bounds.right() && rect.center().y() < bounds.top()) {
            const QPoint point = rect.center().toPoint();
            CHECK(view.viewport()->rect().contains(rect.toAlignedRect()) && view.itemAt(point) == child);
            return point;
        }
    }
    throw std::runtime_error("Missing selected-node link handle");
}
static void link_interaction_case() {
    {
        // The create-link button stays visible at either end of a tall fitted map.
        Json children = Json::array(), nodes = Json::array();
        for (int i = 0; i < 90; ++i) {
            const std::string id = "n" + std::to_string(i);
            children.push_back(id);
            nodes.push_back(Json{{"id", id}, {"topic", "Row " + std::to_string(i)}});
        }
        nodes.push_back(Json{{"id", "r"}, {"topic", "Tall root"}, {"children", children}});
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(Json{{"schemaVersion", 1}, {"rootId", "r"}, {"nodes", nodes}})));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Outline));
        showEditor(editor);
        CHECK(editor.selectNode(QStringLiteral("r")));
        const Json before = exported(editor);
        editor.fitToContents(); pump();
        linkHandlePoint(editor, QStringLiteral("Tall root"));
        CHECK(editor.selectNode(QStringLiteral("n89")));
        linkHandlePoint(editor, QStringLiteral("Row 89"));
        CHECK(editor.selectNode(QStringLiteral("r")));
        linkHandlePoint(editor, QStringLiteral("Tall root"));
        CHECK(exported(editor) == before);
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(editor);
        trigger(editor, "resetZoom");
        CHECK(editor.selectNode(QStringLiteral("a")));
        auto &view = graphics(editor);
        const QPoint target = labelPoint(editor, QStringLiteral("Beta"));
        const QPoint handle = linkHandlePoint(editor, QStringLiteral("Alpha"));
        const Json original = exported(editor);
        const QTransform zoom = view.transform();
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, handle);
        movePointer(view, target, Qt::LeftButton);
        CHECK(exported(editor) == original && changed.isEmpty());
        CHECK(editor.selectedNodeId() == QStringLiteral("a") && view.transform() == zoom);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);
        pump();
        const QString id = editor.selectedLinkId();
        CHECK(!id.isEmpty() && editor.selectedNodeId().isEmpty() && changed.size() == 1);
        const Json inserted = exported(editor);
        const Json link = record(inserted, "crossLinks", id);
        CHECK(link.at("source") == "a" && link.at("target") == "b" && link.at("directed") == false);
        CHECK(link.at("topic") == "" && inserted.at("nodes") == original.at("nodes"));
        CHECK(topicInput(editor).toPlainText().isEmpty());
        const QString topic = QStringLiteral("#literal ## Unicode \u03a9\nSecond line");
        topicInput(editor).setPlainText(topic);
        CHECK(exported(editor) == inserted && changed.size() == 1);
        topicKey(editor, Qt::Key_Return);
        Json expectedLink = link;
        expectedLink["topic"] = utf8(topic);
        CHECK(record(exported(editor), "crossLinks", id) == expectedLink && changed.size() == 2);
        CHECK(exported(editor).at("nodes") == original.at("nodes"));
        clickLabel(editor, topic, true);
        CHECK(topicInput(editor).toPlainText() == topic);
        topicInput(editor).setPlainText(QStringLiteral("Discard this"));
        topicKey(editor, Qt::Key_Escape);
        CHECK(record(exported(editor), "crossLinks", id) == expectedLink && changed.size() == 2);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Click outside saves"));
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, blankPoint(view));
        pump();
        CHECK(record(exported(editor), "crossLinks", id).at("topic") == "Click outside saves");
        CHECK(changed.size() == 3 && !activeTopicInput(editor));
    }
    {
        // Node and link IDs are separate namespaces; refresh must preserve only the right draft.
        Json data = editorFixture();
        data["crossLinks"][0]["id"] = "a";
        data["crossLinks"][0]["topic"] = "#original ##";
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(data)));
        showEditor(editor);
        CHECK(editor.selectLink(QStringLiteral("a")));
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const Json before = exported(editor);
        shortcut(editor, Qt::Key_F2);
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("#original ##"));
        topicInput(editor).setPlainText(QStringLiteral("#draft ##"));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Left));
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("#draft ##"));
        CHECK(exported(editor) == before && changed.isEmpty());
        topicKey(editor, Qt::Key_Return);
        Json expected = before;
        for (auto &link : expected["crossLinks"]) if (link["id"] == "a") link["topic"] = "#draft ##";
        CHECK(exported(editor) == expected && changed.size() == 1);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Must not rename node"));
        CHECK(editor.selectNode(QStringLiteral("a")));
        CHECK(!activeTopicInput(editor) && exported(editor) == expected);
        CHECK(editor.selectLink(QStringLiteral("a")));
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Stale removed link"));
        CHECK(editor.removeLink(QStringLiteral("a")));
        pump();
        CHECK(!activeTopicInput(editor) && !hasRecord(exported(editor), "crossLinks", QStringLiteral("a")));
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")) == record(before, "nodes", QStringLiteral("a")));
        CHECK(changed.size() == 2);
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        trigger(editor, "resetZoom");
        auto &view = graphics(editor);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        auto start = [&] {
            CHECK(editor.selectNode(QStringLiteral("a")));
            const QPoint target = labelPoint(editor, QStringLiteral("Beta"));
            QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                              linkHandlePoint(editor, QStringLiteral("Alpha")));
            movePointer(view, target, Qt::LeftButton);
            return target;
        };
        QPoint target = start();
        QTest::keyClick(view.viewport(), Qt::Key_Escape);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);
        pump();
        CHECK(editor.selectedNodeId() == QStringLiteral("a") && exported(editor) == before && changed.isEmpty());
        start();
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, blankPoint(view));
        pump();
        CHECK(exported(editor) == before && changed.isEmpty() && !activeTopicInput(editor));
        start();
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                            view.mapFromScene(topicRect(editor, QStringLiteral("Alpha")).center()));
        pump();
        CHECK(exported(editor) == before && changed.isEmpty());
        target = start();
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Left));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);
        pump();
        CHECK(exported(editor) == before && changed.isEmpty() && !activeTopicInput(editor));
        // Cancelling the automatically opened topic retains the newly committed blank link.
        target = start();
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);
        pump();
        const QString id = editor.selectedLinkId();
        CHECK(!id.isEmpty() && topicInput(editor).toPlainText().isEmpty());
        topicInput(editor).setPlainText(QStringLiteral("Uncommitted"));
        topicKey(editor, Qt::Key_Escape);
        CHECK(record(exported(editor), "crossLinks", id).at("topic") == "" && changed.size() == 1);
    }
    for (bool onSelection : {false, true}) {
        // Hosts may close the editor from either notification during link creation.
        auto *editor = new RawLabelEditor;
        QPointer<Editor> alive(editor);
        CHECK(editor->loadJson(encoded(editorFixture())));
        showEditor(*editor);
        trigger(*editor, "resetZoom");
        CHECK(editor->selectNode(QStringLiteral("a")));
        auto &view = graphics(*editor);
        const QPoint target = labelPoint(*editor, QStringLiteral("Beta"));
        const QPoint handle = linkHandlePoint(*editor, QStringLiteral("Alpha"));
        if (onSelection)
            QObject::connect(editor, &Editor::selectionChanged, editor, [editor](const QString &, const QString &link) {
                if (!link.isEmpty()) delete editor;
            });
        else QObject::connect(editor, &Editor::documentChanged, editor, [editor] { delete editor; });
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, handle);
        movePointer(view, target, Qt::LeftButton);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(target), QPointF(view.viewport()->mapToGlobal(target)),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &release);
        CHECK(alive.isNull());
        pump();
    }
}

static void host_commit_case() {
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), root = QStringLiteral("r");
    for (int reaction = 0; reaction < 10; ++reaction) {
        QPointer<MimeEditor> editor = new MimeEditor;
        CHECK(editor->loadJson(encoded(editorFixture())));
        showEditor(*editor);
        CHECK(editor->selectNode(a));
        const Json replacement = exported(*editor);
        QApplication::clipboard()->setText(QStringLiteral("https://example.test/draft-paste"));
        shortcut(*editor, Qt::Key_F2);
        topicInput(*editor).setPlainText(reaction == 1 ? QStringLiteral("Rejected") + QChar::Null
                                                    : QStringLiteral("Committed before paste"));
        int inserted = 0;
        editor->insertion = [&] {
            ++inserted;
            CHECK(!editor->hasPendingEdit());
            CHECK(record(exported(*editor), "nodes", a).at("topic") == "Committed before paste");
        };
        bool reacted = false;
        QObject::connect(editor.data(), &Editor::documentChanged, qApp, [&] {
            if (reacted) return;
            reacted = true;
            if (reaction == 2) CHECK(editor->newDocument(QStringLiteral("Replacement")));
            else if (reaction == 3) { CHECK(editor->loadJson(encoded(replacement))); CHECK(editor->selectNode(a)); }
            else if (reaction == 4) CHECK(editor->selectNode(b));
            else if (reaction == 5) { CHECK(editor->selectNode(b)); CHECK(editor->selectNode(a)); }
            else if (reaction == 6) editor->setReadOnly(true);
            else if (reaction == 7) CHECK(editor->removeNode(a));
            else if (reaction == 8) editor->setEnabled(false);
            else if (reaction == 9) delete editor.data();
        });
        trigger(*editor, "paste");
        CHECK(inserted == (reaction == 0 ? 1 : 0));
        if (reaction == 1) CHECK(editor->hasPendingEdit() && exported(*editor) == replacement);
        if (reaction == 9) CHECK(editor.isNull());
        delete editor.data();
    }
    for (bool link : {false, true}) {
        MimeEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(link ? editor.selectLink(QStringLiteral("l1")) : editor.selectNode(a));
        const Json before = exported(editor);
        const QString pasted = QStringLiteral("https://example.test/native-text-paste");
        QApplication::clipboard()->setText(pasted);
        shortcut(editor, Qt::Key_F2);
        CHECK(editAction(editor, "paste").shortcuts().isEmpty());
        topicInput(editor).selectAll();
        topicKey(editor, Qt::Key_V, Qt::ControlModifier);
        CHECK(topicInput(editor).toPlainText() == pasted && editor.targets.isEmpty());
        CHECK(exported(editor) == before && editor.hasPendingEdit());
        topicKey(editor, Qt::Key_Escape);
        CHECK(!editAction(editor, "paste").shortcuts().isEmpty());
    }
    {
        RawLabelConfig config;
        config.separateNodePropertiesPanel = true;
        MimeEditor editor(config);
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(a));
        QWidget tools;
        auto *layout = new QVBoxLayout(&tools);
        auto *panel = editor.nodePropertiesPanel();
        CHECK(panel);
        layout->addWidget(panel);
        panel->show();
        tools.resize(400, 750);
        tools.show();
        tools.activateWindow();
        auto *url = panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
        CHECK(url);
        url->setFocus();
        pump();
        const QString pasted = QStringLiteral("https://example.test/tools-text-paste");
        QApplication::clipboard()->setText(pasted);
        url->selectAll();
        QTest::keyClick(url, Qt::Key_V, Qt::ControlModifier);
        pump();
        CHECK(url->text() == pasted && editor.targets.isEmpty());
        CHECK(record(exported(editor), "nodes", a).at("topic") == "Alpha");
    }
    {
        QPointer<Editor> editor = new RawLabelEditor;
        QObject::connect(editor.data(), &Editor::readOnlyChanged, qApp, [&](bool readOnly) {
            CHECK(readOnly);
            delete editor.data();
        });
        editor->setReadOnly(true);
        CHECK(editor.isNull());
    }
    for (bool link : {false, true}) {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        editor.resetZoom(); editor.zoom(0.85);
        CHECK(link ? editor.selectLink(QStringLiteral("l1")) : editor.selectNode(a));
        const Json before = exported(editor);
        const QStringList nodes = editor.selectedNodeIds();
        const QString selectedLink = editor.selectedLinkId();
        const QTransform zoom = graphics(editor).transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), pending(&editor, &Editor::pendingEditChanged);
        QSignalSpy selected(&editor, &Editor::selectionChanged), errors(&editor, &Editor::errorOccurred);
        shortcut(editor, Qt::Key_F2);
        QPointer<QPlainTextEdit> input = &topicInput(editor);
        const QString original = input->toPlainText();
        const QString accepted = link ? QStringLiteral("Retained ##literal #link") : QStringLiteral("Retained node");
        input->selectAll(); input->insertPlainText(accepted);
        auto cursor = input->textCursor();
        cursor.setPosition(2); cursor.setPosition(7, QTextCursor::KeepAnchor);
        input->setTextCursor(cursor);
        CHECK(editor.hasPendingEdit() && pending.size() == 1 && pending.back().front().toBool());
        CHECK(editor.commitActiveEdit(true));
        pump();
        Json saved = before;
        if (link) {
            for (auto &entry : saved.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = utf8(accepted);
        } else setTopic(saved, "a", accepted);
        auto retained = [&] {
            CHECK(input && activeTopicInput(editor) == input.data() && input->hasFocus());
            CHECK(input->toPlainText() == accepted && !editor.hasPendingEdit());
            CHECK(input->textCursor().anchor() == 2 && input->textCursor().position() == 7);
            CHECK(editor.selectedText() == accepted.mid(2, 5));
            CHECK(exported(editor) == saved && changed.size() == 1);
            CHECK(pending.size() == 2 && !pending.back().front().toBool());
            CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
            CHECK(selected.isEmpty() && graphics(editor).transform() == zoom);
        };
        retained();
        for (int i = 0; i < 3; ++i) { CHECK(editor.commitActiveEdit(true)); pump(); retained(); }
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Base, QColor(31, 35, 42));
        palette.setColor(QPalette::Text, QColor(230, 232, 235));
        editor.setPalette(palette); pump(); retained();
        QFont font = editor.font(); font.setPixelSize(19);
        editor.setFont(font); pump(); retained();
        editor.reloadImages(); pump(); retained();
        CHECK(errors.isEmpty());

        // The original text undo operation survives saving and appearance-driven scene rebuilds.
        topicKey(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(activeTopicInput(editor) == input.data() && topicInput(editor).toPlainText() == original);
        CHECK(editor.hasPendingEdit() && pending.size() == 3 && pending.back().front().toBool());
        CHECK(exported(editor) == saved && changed.size() == 1);
        topicKey(editor, Qt::Key_Y, Qt::ControlModifier);
        CHECK(topicInput(editor).toPlainText() == accepted && !editor.hasPendingEdit());
        CHECK(pending.size() == 4 && !pending.back().front().toBool());
        CHECK(exported(editor) == saved && changed.size() == 1);
        input->moveCursor(QTextCursor::End);
        QTest::keyClicks(input.data(), " continued");
        CHECK(editor.hasPendingEdit() && pending.size() == 5 && pending.back().front().toBool());
        const QString continued = accepted + QStringLiteral(" continued");
        const QString rejected = continued + QChar(QChar::Null);
        input->setPlainText(rejected);
        cursor = input->textCursor();
        cursor.setPosition(1); cursor.setPosition(5, QTextCursor::KeepAnchor);
        input->setTextCursor(cursor);
        CHECK(!editor.commitActiveEdit(true));
        pump();
        CHECK(input && activeTopicInput(editor) == input.data() && input->hasFocus());
        CHECK(input->toPlainText() == rejected && editor.hasPendingEdit());
        CHECK(input->textCursor().anchor() == 1 && input->textCursor().position() == 5);
        CHECK(exported(editor) == saved && changed.size() == 1 && pending.size() == 5 && !errors.isEmpty());
        CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
        CHECK(selected.isEmpty() && graphics(editor).transform() == zoom);
        input->setPlainText(continued);
        CHECK(editor.commitActiveEdit(true));
        pump();
        if (link) {
            for (auto &entry : saved.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = utf8(continued);
        } else setTopic(saved, "a", continued);
        CHECK(activeTopicInput(editor) == input.data() && topicInput(editor).toPlainText() == continued);
        CHECK(exported(editor) == saved && changed.size() == 2 && !editor.hasPendingEdit());
        CHECK(pending.size() == 6 && !pending.back().front().toBool());
        topicKey(editor, Qt::Key_Return);
        CHECK(!activeTopicInput(editor) && !editor.hasPendingEdit() && exported(editor) == saved && changed.size() == 2);
        // Repeated captures and final Enter made only the two distinct document commands.
        CHECK(editor.undo() && changed.size() == 3);
        Json firstSave = before;
        if (link) {
            for (auto &entry : firstSave.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = utf8(accepted);
        } else setTopic(firstSave, "a", accepted);
        CHECK(exported(editor) == firstSave);
        CHECK(editor.undo() && exported(editor) == before && !editor.canUndo());
        CHECK(editor.redo() && exported(editor) == firstSave);
        CHECK(editor.redo() && exported(editor) == saved && !editor.canRedo());
    }
    for (bool link : {false, true}) for (int reaction : {0, 1, 2}) {
        // Host notifications may synchronously replace the map, lock it, or remove the edited target.
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(link ? editor.selectLink(QStringLiteral("l1")) : editor.selectNode(a));
        Json replacement = exported(editor);
        setTopic(replacement, "a", QStringLiteral("Replacement with reused node id"));
        for (auto &entry : replacement.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = "Replacement link";
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Committed before host reaction"));
        Json observed;
        bool reacted = false;
        QObject::connect(&editor, &Editor::documentChanged, &editor, [&] {
            if (reacted) return;
            reacted = true;
            observed = exported(editor);
            if (reaction == 0) CHECK(editor.loadJson(encoded(replacement)));
            else if (reaction == 1) editor.setReadOnly(true);
            else CHECK(link ? editor.removeLink(QStringLiteral("l1")) : editor.removeNode(a));
        });
        editor.commitActiveEdit(true);
        pump();
        CHECK(reacted && record(observed, link ? "crossLinks" : "nodes", link ? QStringLiteral("l1") : a).at("topic") == "Committed before host reaction");
        CHECK(!activeTopicInput(editor) && !editor.hasPendingEdit());
        if (reaction == 0) CHECK(exported(editor) == replacement && !editor.canUndo() && !editor.canRedo());
        else if (reaction == 1) CHECK(editor.isReadOnly() && exported(editor) == observed);
        else {
            CHECK(!hasRecord(exported(editor), link ? "crossLinks" : "nodes", link ? QStringLiteral("l1") : a));
            CHECK(editor.undo() && exported(editor) == observed);
        }
    }
    for (bool fromPending : {false, true}) {
        QPointer<Editor> editor = new RawLabelEditor;
        CHECK(editor->loadJson(encoded(editorFixture())));
        showEditor(*editor);
        CHECK(editor->selectNode(a));
        shortcut(*editor, Qt::Key_F2);
        topicInput(*editor).setPlainText(QStringLiteral("Host closes after capture"));
        Json observed;
        const auto closeEditor = [&] { observed = exported(*editor); delete editor.data(); };
        if (fromPending) QObject::connect(editor.data(), &Editor::pendingEditChanged, qApp,
                                         [&](bool pending) { if (!pending) closeEditor(); });
        else QObject::connect(editor.data(), &Editor::documentChanged, qApp, closeEditor);
        editor->commitActiveEdit(true);
        pump();
        CHECK(editor.isNull() && record(observed, "nodes", a).at("topic") == "Host closes after capture");
    }
    for (bool link : {false, true}) {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
        showEditor(editor);
        editor.resetZoom();
        editor.zoom(0.85);
        CHECK(link ? editor.selectLink(QStringLiteral("l1")) : editor.selectNode(a));
        const Json before = exported(editor);
        const QString markdown = editor.toMarkdown();
        const auto articles = HtmlPage(editor.toHtml()).values(QStringLiteral("article"));
        QSignalSpy changed(&editor, &Editor::documentChanged), pending(&editor, &Editor::pendingEditChanged);
        QSignalSpy selected(&editor, &Editor::selectionChanged), errors(&editor, &Editor::errorOccurred);
        CHECK(!editor.hasPendingEdit() && editor.commitActiveEdit());
        shortcut(editor, Qt::Key_F2);
        const QString original = topicInput(editor).toPlainText();
        CHECK(!editor.hasPendingEdit() && pending.isEmpty());
        QTest::keyClick(&topicInput(editor), Qt::Key_Left);
        CHECK(!editor.hasPendingEdit() && pending.isEmpty());
        topicInput(editor).selectAll();
        QTest::keyClicks(&topicInput(editor), "Draft");
        CHECK(editor.hasPendingEdit() && pending.size() == 1 && pending.front().front().toBool());
        QTest::keyClicks(&topicInput(editor), " changed again");
        CHECK(pending.size() == 1 && changed.isEmpty());
        topicInput(editor).setPlainText(original);
        CHECK(!editor.hasPendingEdit() && pending.size() == 2 && !pending.back().front().toBool());
        CHECK(exported(editor) == before && changed.isEmpty());

        const QString accepted = link ? QStringLiteral("Accepted #literal link") : QStringLiteral("Accepted node");
        topicInput(editor).setPlainText(accepted);
        CHECK(editor.hasPendingEdit() && pending.size() == 3 && pending.back().front().toBool());
        const QTransform zoom = graphics(editor).transform();
        const QStringList nodes = editor.selectedNodeIds();
        const QString selectedLink = editor.selectedLinkId();
        CHECK(exported(editor) == before && editor.toMarkdown() == markdown);
        CHECK(HtmlPage(editor.toHtml()).values(QStringLiteral("article")) == articles);
        CHECK(changed.isEmpty() && selected.isEmpty());
        CHECK(editor.commitActiveEdit());
        pump();
        Json committed = before;
        if (link) {
            for (auto &entry : committed.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = utf8(accepted);
        } else setTopic(committed, "a", accepted);
        CHECK(exported(editor) == committed && changed.size() == 1);
        CHECK(!editor.hasPendingEdit() && activeTopicInput(editor) == nullptr);
        CHECK(pending.size() == 4 && !pending.back().front().toBool());
        CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
        CHECK(selected.isEmpty() && graphics(editor).transform() == zoom && errors.isEmpty());
        CHECK(editor.commitActiveEdit() && changed.size() == 1 && pending.size() == 4);

        // A rejected synchronous host save keeps the live draft, selection and caret.
        shortcut(editor, Qt::Key_F2);
        const QString rejected = QStringLiteral("Invalid") + QChar(QChar::Null) + QStringLiteral("draft");
        topicInput(editor).setPlainText(rejected);
        auto cursor = topicInput(editor).textCursor();
        cursor.setPosition(2);
        cursor.setPosition(5, QTextCursor::KeepAnchor);
        topicInput(editor).setTextCursor(cursor);
        const auto pendingCount = pending.size();
        auto retained = [&] {
            CHECK(editor.hasPendingEdit() && topicInput(editor).toPlainText() == rejected);
            CHECK(topicInput(editor).textCursor().position() == 5 && topicInput(editor).textCursor().anchor() == 2);
            CHECK(exported(editor) == committed && changed.size() == 1 && pending.size() == pendingCount);
            CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
            CHECK(selected.isEmpty() && graphics(editor).transform() == zoom);
        };
        CHECK(!editor.commitActiveEdit());
        CHECK(!errors.isEmpty() && !editor.lastError().isEmpty());
        retained();
        CHECK(!editor.focusRoot());
        retained();
        trigger(editor, "zoomIn");
        retained();
        if (!link) {
            trigger(editor, "addChild");
            retained();
        }
        const QPoint other = labelPoint(editor, QStringLiteral("Beta"));
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, other);
        pump();
        retained();
        topicKey(editor, Qt::Key_Return);
        retained();
        const QString corrected = link ? QStringLiteral("Corrected link") : QStringLiteral("Corrected node");
        topicInput(editor).setPlainText(corrected);
        CHECK(editor.commitActiveEdit());
        pump();
        if (link) {
            for (auto &entry : committed.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = utf8(corrected);
        } else setTopic(committed, "a", corrected);
        CHECK(exported(editor) == committed && changed.size() == 2);
        CHECK(!editor.hasPendingEdit() && pending.size() == pendingCount + 1 && !pending.back().front().toBool());
        CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
        CHECK(selected.isEmpty() && graphics(editor).transform() == zoom);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Canceled replacement"));
        CHECK(editor.hasPendingEdit());
        topicKey(editor, Qt::Key_Escape);
        CHECK(!editor.hasPendingEdit() && activeTopicInput(editor) == nullptr);
        CHECK(exported(editor) == committed && changed.size() == 2);
        CHECK(pending.size() == pendingCount + 3 && !pending.back().front().toBool());
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor, QSize(1100, 900));
        CHECK(editor.selectNode(a));
        Json expected = exported(editor);
        const QByteArray ownedNode = editor.nodeJson(root);
        CHECK(!ownedNode.isEmpty() && Json::parse(ownedNode.constData()) == record(expected, "nodes", root));
        const Json originalRoot = record(expected, "nodes", root);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        QSignalSpy pending(&editor, &Editor::pendingEditChanged);
        const QTransform zoom = graphics(editor).transform();
        const QByteArray patch = encoded(Json{{"style", {{"color", "#123456"}}}, {"hyperLink", "docs/changed"}});
        CHECK(editor.updateNode(root, patch));
        for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r") {
            entry["style"]["color"] = "#123456";
            entry["hyperLink"] = "docs/changed";
        }
        CHECK(exported(editor) == expected && changed.size() == 1 && selected.isEmpty());
        CHECK(Json::parse(ownedNode.constData()) == originalRoot);
        CHECK(editor.selectedNodeId() == a && graphics(editor).transform() == zoom);
        CHECK(editor.updateNode(root, patch) && exported(editor) == expected && changed.size() == 1);
        CHECK(editor.updateNode(root, encoded(Json{{"style", {{"color", nullptr}}}})));
        for (auto &entry : expected.at("nodes")) if (entry.at("id") == "r") entry["style"].erase("color");
        CHECK(exported(editor) == expected && changed.size() == 2);
        CHECK(record(expected, "nodes", root).at("style") == originalRoot.at("style"));
        rejectUnchanged(editor, [&] { return editor.updateNode(root, QByteArray("{bad")); });
        rejectUnchanged(editor, [&] { return editor.updateNode(root, encoded(Json{{"unknown", true}})); });
        rejectUnchanged(editor, [&] { return editor.updateNode(QStringLiteral("missing"), patch); });
        QSignalSpy errors(&editor, &Editor::errorOccurred);
        CHECK(editor.nodeJson(QStringLiteral("missing")).isEmpty() && !errors.isEmpty());
        CHECK(exported(editor) == expected && pending.isEmpty());

        CHECK(editor.updateNode(a, encoded(Json{{"hyperLink", "docs/link"}, {"note", "First\nSecond"}})));
        const Json beforeCopy = exported(editor);
        changed.clear();
        clickLabel(editor, QStringLiteral("Beta"));
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::ShiftModifier,
                          labelPoint(editor, QStringLiteral("Alpha")));
        pump();
        CHECK(editor.selectedNodeIds() == QStringList({b, a}));
        CHECK(editor.selectedText() == QStringLiteral("Beta\nAlpha"));
        CHECK(editor.selectLink(QStringLiteral("l1")) && editor.selectedText() == QStringLiteral("Related"));
        editor.clearSelection();
        CHECK(editor.selectedText().isEmpty());
        CHECK(editor.selectNode(a));
        expandProperties(editor);
        auto *url = editor.findChild<QLineEdit *>(QStringLiteral("nodeUrl"));
        auto *note = editor.findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
        auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        CHECK(url && note && panel);
        auto *scroll = panel->findChild<QScrollArea *>();
        CHECK(scroll);
        scroll->ensureWidgetVisible(url);
        url->setFocus();
        url->setSelection(5, 4);
        pump();
        CHECK(url->hasFocus() && editor.selectedText() == QStringLiteral("link"));
        url->deselect();
        CHECK(editor.selectedText().isEmpty());
        scroll->ensureWidgetVisible(note);
        note->setFocus();
        note->selectAll();
        pump();
        CHECK(note->hasFocus() && editor.selectedText() == QStringLiteral("First\nSecond"));
        QApplication::clipboard()->clear();
        QTest::keyClick(note, Qt::Key_C, Qt::ControlModifier);
        CHECK(QApplication::clipboard()->text() == QStringLiteral("First\nSecond"));
        QLineEdit foreign(QStringLiteral("Foreign selection"));
        foreign.show(); foreign.activateWindow(); foreign.setFocus(); foreign.selectAll();
        pump();
        CHECK(foreign.hasFocus() && editor.selectedText() == QStringLiteral("Alpha"));
        foreign.hide();
        editor.activateWindow(); graphics(editor).setFocus(); pump();
        CHECK(editor.selectedText() == QStringLiteral("Alpha"));
        selected.clear();
        editor.resetZoom();
        CHECK(graphics(editor).transform().m11() == 1);
        editor.zoom(1.5);
        CHECK(std::abs(graphics(editor).transform().m11() - 1.5) < 0.0001);
        editor.zoom(1e6);
        const QTransform maximum = graphics(editor).transform();
        editor.zoom(2);
        CHECK(graphics(editor).transform() == maximum);
        editor.resetZoom();
        auto *horizontal = graphics(editor).horizontalScrollBar();
        auto *vertical = graphics(editor).verticalScrollBar();
        horizontal->setValue((horizontal->minimum() + horizontal->maximum()) / 2);
        vertical->setValue((vertical->minimum() + vertical->maximum()) / 2);
        const int x = horizontal->value(), y = vertical->value();
        CHECK(horizontal->maximum() > horizontal->minimum() && vertical->maximum() > vertical->minimum());
        editor.scrollSteps(2, -3);
        CHECK(horizontal->value() == qBound(horizontal->minimum(), x + 2 * horizontal->singleStep(), horizontal->maximum()));
        CHECK(vertical->value() == qBound(vertical->minimum(), y - 3 * vertical->singleStep(), vertical->maximum()));
        CHECK(horizontal->value() != x && vertical->value() != y);
        CHECK(exported(editor) == beforeCopy && changed.isEmpty() && selected.isEmpty() && pending.isEmpty());
    }
    for (bool resolve : {true, false}) {
        QTemporaryDir first, second;
        CHECK(first.isValid() && second.isValid());
        const QString rawImage = QString::fromUtf8("images/世界.png");
        const QString rawLink = QStringLiteral("docs/read%20me.txt#section");
        RawLabelConfig config;
        config.resourceBasePath = first.path();
        config.resolveRelativeUrls = resolve;
        RawLabelEditor editor(config);
        Json input = imageDocument(rawImage);
        for (auto &entry : input.at("nodes")) if (entry.at("id") == "r") entry["hyperLink"] = utf8(rawLink);
        QSignalSpy requests(&editor, &Editor::imageRequested);
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor);
        CHECK(QTest::qWaitFor([&] { return requests.size() == 1; }, 5000));
        const QString oldUrl = requests.front().at(0).toString();
        const quint64 oldId = requests.front().at(1).toULongLong();
        const QString firstUrl = QUrl::fromLocalFile(first.filePath(rawImage)).toString(QUrl::FullyEncoded);
        CHECK(oldUrl == (resolve ? firstUrl : rawImage));
        CHECK(resolve ? editor.selectNode(a) : editor.selectLink(QStringLiteral("l1")));
        editor.resetZoom(); editor.zoom(0.9);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        QSignalSpy pending(&editor, &Editor::pendingEditChanged);
        const Json before = exported(editor);
        shortcut(editor, Qt::Key_F2);
        const QString draft = QStringLiteral("Retained refresh draft");
        topicInput(editor).setPlainText(draft);
        auto cursor = topicInput(editor).textCursor();
        cursor.setPosition(3); cursor.setPosition(9, QTextCursor::KeepAnchor);
        topicInput(editor).setTextCursor(cursor);
        const QTransform zoom = graphics(editor).transform();
        const QPointF center = graphics(editor).mapToScene(graphics(editor).viewport()->rect().center());
        auto retained = [&] {
            CHECK(topicInput(editor).toPlainText() == draft && editor.hasPendingEdit());
            CHECK(topicInput(editor).textCursor().anchor() == 3 && topicInput(editor).textCursor().position() == 9);
            CHECK(editor.selectedText() == draft.mid(3, 6));
            CHECK(exported(editor) == before && changed.isEmpty() && selected.isEmpty());
            CHECK(pending.size() == 1 && pending.front().front().toBool());
            CHECK(graphics(editor).transform() == zoom);
            CHECK(QLineF(center, graphics(editor).mapToScene(graphics(editor).viewport()->rect().center())).length() <= 2.0 / zoom.m11());
        };
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Base, QColor(31, 35, 42));
        palette.setColor(QPalette::Text, QColor(230, 232, 235));
        editor.setPalette(palette); pump(); retained();
        QFont font = editor.font(); font.setPixelSize(19);
        editor.setFont(font); pump(); retained();
        const QString newBase = QDir::cleanPath(second.filePath(QStringLiteral("one/../resources")));
        editor.setResourceBasePath(second.filePath(QStringLiteral("one/../resources")));
        CHECK(editor.resourceBasePath() == newBase);
        CHECK(QTest::qWaitFor([&] { return requests.size() == 2; }, 5000));
        retained();
        const QString newUrl = requests.back().at(0).toString();
        const quint64 newId = requests.back().at(1).toULongLong();
        CHECK(newUrl == (resolve ? QUrl::fromLocalFile(QDir(newBase).filePath(rawImage)).toString(QUrl::FullyEncoded) : rawImage));
        CHECK(newId > oldId);
        editor.provideImage(oldUrl, oldId, imagePixels());
        pump();
        checkImageSize(nodeImageRect(editor, QStringLiteral("Image root"), rawImage), QSizeF(120, 90));
        retained();
        editor.provideImage(newUrl, newId, imagePixels());
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, QStringLiteral("Image root"), rawImage); }, 5000));
        retained();
        editor.reloadImages();
        CHECK(QTest::qWaitFor([&] { return requests.size() == 3; }, 5000));
        const quint64 refreshedId = requests.back().at(1).toULongLong();
        CHECK(refreshedId > newId && requests.back().at(0).toString() == newUrl);
        editor.provideImage(newUrl, newId, imagePixels());
        pump();
        checkImageSize(nodeImageRect(editor, QStringLiteral("Image root"), rawImage), QSizeF(120, 90));
        retained();
        editor.provideImage(newUrl, refreshedId, imagePixels());
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, QStringLiteral("Image root"), rawImage); }, 5000));
        retained();
        topicKey(editor, Qt::Key_Escape);
        CHECK(!editor.hasPendingEdit() && pending.size() == 2 && !pending.back().front().toBool());
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated);
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier,
                          nodeLinkPoint(editor, QStringLiteral("Image root")));
        pump();
        const QString resolvedLink = QUrl::fromLocalFile(QDir(newBase).filePath(QStringLiteral("docs/read me.txt"))).toString(QUrl::FullyEncoded) + QStringLiteral("#section");
        CHECK(activated.size() == 1 && activated.front().at(1).toString() == (resolve ? resolvedLink : rawLink));
        CHECK(exported(editor) == before && changed.isEmpty());
        editor.setResourceBasePath(QStringLiteral("assets/../resources"));
        CHECK(editor.resourceBasePath() == QDir::cleanPath(QDir::current().absoluteFilePath(QStringLiteral("resources"))));
        editor.setResourceBasePath(QString());
        CHECK(editor.resourceBasePath() == QDir::cleanPath(QDir::currentPath()));
    }
}

static void read_only_case() {
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(fiveLevelFixture())));
        showEditor(editor);
        editor.setReadOnly(true);
        const QByteArray before = editor.toJson();
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
        visibleLevels(editor, 3);
        CHECK(editor.selectNode(QStringLiteral("g")));
        shortcut(editor, Qt::Key_Space);
        visibleLevels(editor, 4);
        CHECK(editor.selectNode(QStringLiteral("h")));
        CHECK(editAction(editor, "toggleExpanded").isEnabled());
        trigger(editor, "toggleExpanded");
        visibleLevels(editor, 5);
        CHECK(editor.toJson() == before && changed.isEmpty() && undo.isEmpty() && redo.isEmpty());
        rejectUnchanged(editor, [&] { return editor.renameNode(QStringLiteral("g"), QStringLiteral("Forbidden")); });
    }
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), d = QStringLiteral("d"), root = QStringLiteral("r");
    RawLabelConfig config;
    config.confirmSubtreeDeletion = false;
    RawLabelEditor editor(config);
    QObject::connect(&editor, &Editor::imageRequested, &editor,
        [&editor](const QString &url, quint64 id) { editor.provideImage(url, id, imagePixels()); });
    CHECK(editor.loadJson(encoded(imageDocument())));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor, QSize(1100, 900));
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, QStringLiteral("Image root"), QStringLiteral("memory:picture")); }, 5000));
    editor.resetZoom();
    CHECK(editor.selectNode(a));
    const Json before = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged), pending(&editor, &Editor::pendingEditChanged);
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QStringLiteral("Canceled by read-only"));
    CHECK(editor.hasPendingEdit() && pending.size() == 1 && pending.front().front().toBool());
    const QTransform initialZoom = graphics(editor).transform();
    editor.setReadOnly(true);
    pump();
    CHECK(editor.isReadOnly() && editor.isEnabled() && graphics(editor).isEnabled());
    CHECK(!editor.hasPendingEdit() && activeTopicInput(editor) == nullptr && editor.commitActiveEdit());
    CHECK(pending.size() == 2 && !pending.back().front().toBool());
    CHECK(exported(editor) == before && changed.isEmpty());
    CHECK(editor.selectedNodeId() == a && graphics(editor).transform() == initialZoom);
    editor.setReadOnly(true);
    CHECK(pending.size() == 2 && changed.isEmpty());

    auto refused = [&](const std::function<bool()> &operation) {
        const QStringList nodes = editor.selectedNodeIds();
        const QString link = editor.selectedLinkId();
        QSignalSpy selected(&editor, &Editor::selectionChanged);
        CHECK(!operation());
        CHECK(exported(editor) == before && changed.isEmpty() && selected.isEmpty());
        CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == link);
    };
    refused([&] { return !editor.addNode(a, QStringLiteral("Forbidden child")).isEmpty(); });
    refused([&] { return editor.renameNode(a, QStringLiteral("Forbidden rename")); });
    refused([&] { return editor.updateNode(a, encoded(Json{{"style", {{"color", "#123456"}}}})); });
    refused([&] { return editor.removeNode(a); });
    refused([&] { return editor.moveNode(d, b); });
    refused([&] { return !editor.addLink(a, b, false, QStringLiteral("Forbidden link")).isEmpty(); });
    refused([&] { return editor.updateLink(QStringLiteral("l1"), a, d, false, QStringLiteral("Forbidden update")); });
    refused([&] { return editor.removeLink(QStringLiteral("l1")); });
    CHECK(Json::parse(editor.nodeJson(a).constData()) == record(before, "nodes", a));
    QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
    CHECK(editor.setExpanded(a, false));
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(editAction(editor, "toggleExpanded").isEnabled());
    shortcut(editor, Qt::Key_Space);
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
    trigger(editor, "toggleExpanded");
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(editor.setExpanded(a, true));
    CHECK(exported(editor) == before && changed.isEmpty() && undo.isEmpty() && redo.isEmpty());

    dialogs({}, [&] {
        for (const char *name : {"addChild", "addSibling", "addSiblingBefore", "editSelection", "deleteSelection",
                                 "editTopic", "moveNode", "moveUp", "moveDown", "addLink", "editLink",
                                 "toggleBold", "toggleItalic", "resetStyle", "textColor", "fillColor", "editTags", "editIcons", "editNote"}) {
            auto &action = editAction(editor, name);
            CHECK(!action.isEnabled());
            action.trigger();
        }
        for (Qt::Key key : {Qt::Key_Tab, Qt::Key_Insert, Qt::Key_Return, Qt::Key_Delete,
                           Qt::Key_F2, Qt::Key_E, Qt::Key_B, Qt::Key_I, Qt::Key_R}) shortcut(editor, key);
        for (Qt::Key key : {Qt::Key_M, Qt::Key_L, Qt::Key_Up, Qt::Key_Down}) shortcut(editor, key, Qt::ControlModifier);
    });
    CHECK(exported(editor) == before && changed.isEmpty() && !editor.hasPendingEdit() && !activeTopicInput(editor));
    clickLabel(editor, QStringLiteral("Beta"));
    CHECK(editor.selectedNodeId() == b && editor.selectedText() == QStringLiteral("Beta"));
    QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::ShiftModifier,
                      labelPoint(editor, QStringLiteral("Alpha")));
    pump();
    CHECK(editor.selectedNodeIds() == QStringList({b, a}) && editor.selectedText() == QStringLiteral("Beta\nAlpha"));
    CHECK(!editAction(editor, "deleteSelection").isEnabled());
    shortcut(editor, Qt::Key_Delete);
    CHECK(exported(editor) == before && changed.isEmpty());
    CHECK(editor.selectNode(a));
    shortcut(editor, Qt::Key_Right);
    CHECK(editor.selectedNodeId() == d && editor.selectedText() == QStringLiteral("Delta"));
    clickLabel(editor, QStringLiteral("Alpha"));
    clickLabel(editor, QStringLiteral("Alpha"), true);
    CHECK(editor.selectedNodeId() == a && !activeTopicInput(editor));
    auto &view = graphics(editor);
    const QPoint expansion = expansionPoint(editor, QStringLiteral("Alpha"));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, expansion);
    pump();
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(exported(editor) == before && changed.isEmpty() && undo.isEmpty() && redo.isEmpty());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, expansion);
    pump();
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
    const QPoint from = labelPoint(editor, QStringLiteral("Delta"));
    const QPoint to = labelPoint(editor, QStringLiteral("Beta"));
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
    movePointer(view, to, Qt::LeftButton);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, to);
    pump();
    CHECK(exported(editor) == before && changed.isEmpty());
    clickLabel(editor, QStringLiteral("Related"));
    clickLabel(editor, QStringLiteral("Related"), true);
    CHECK(editor.selectedLinkId() == QStringLiteral("l1") && editor.selectedText() == QStringLiteral("Related"));
    CHECK(!activeTopicInput(editor) && !editAction(editor, "editLink").isEnabled());
    for (auto *item : view.scene()->items())
        CHECK(!(item->isVisible() && item->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations) &&
                item->cursor().shape() == Qt::OpenHandCursor));
    CHECK(editor.selectNode(root));
    CHECK(imageHandle(editor) == nullptr);
    expandProperties(editor);
    for (const char *name : {"nodeTags", "nodeIcons", "nodeUrl", "nodeImageUrl"}) {
        auto *input = editor.findChild<QLineEdit *>(QString::fromLatin1(name));
        CHECK(input && (!input->isEnabled() || input->isReadOnly()));
        if (input->isEnabled()) {
            input->setFocus(); input->selectAll();
            const QString text = input->text();
            QTest::keyClicks(input, "Forbidden property");
            CHECK(input->text() == text);
        }
    }
    auto *note = editor.findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
    CHECK(note && (!note->isEnabled() || note->isReadOnly()));
    if (note->isEnabled()) {
        auto *scroll = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"))->findChild<QScrollArea *>();
        CHECK(scroll);
        scroll->ensureWidgetVisible(note);
        note->setFocus(); note->selectAll();
        const QString text = note->toPlainText();
        QTest::keyClicks(note, "Forbidden note");
        CHECK(note->toPlainText() == text);
        QApplication::clipboard()->clear();
        QTest::keyClick(note, Qt::Key_C, Qt::ControlModifier);
        CHECK(QApplication::clipboard()->text() == text && editor.selectedText() == text);
    }
    CHECK(exported(editor) == before && changed.isEmpty());

    // A local file cannot enter the URL-drop mutation path while read-only.
    QTemporaryDir directory;
    CHECK(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("drop.txt")));
    CHECK(file.open(QIODevice::WriteOnly)); file.close();
    QMimeData localFile;
    localFile.setUrls({QUrl::fromLocalFile(file.fileName())});
    const QPoint dropPoint = labelPoint(editor, QStringLiteral("Alpha"));
    QDragEnterEvent enter(dropPoint, Qt::CopyAction, &localFile, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &enter);
    CHECK(!enter.isAccepted());
    QDragMoveEvent move(dropPoint, Qt::CopyAction, &localFile, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &move);
    CHECK(!move.isAccepted());
    QDropEvent drop(QPointF(dropPoint), Qt::CopyAction, &localFile, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &drop);
    CHECK(!drop.isAccepted());
    CHECK(exported(editor) == before && changed.isEmpty());

    // Camera and presentation commands are still usable, without accepting edits.
    CHECK(editor.selectNode(a));
    editor.resetZoom(); editor.zoom(1.25);
    CHECK(std::abs(view.transform().m11() - 1.25) < 0.0001);
    const qreal scale = view.transform().m11();
    trigger(editor, "zoomIn");
    CHECK(view.transform().m11() > scale);
    editor.resetZoom();
    const QPointF center = view.mapToScene(view.viewport()->rect().center());
    editor.scrollSteps(2, 1);
    CHECK(view.mapToScene(view.viewport()->rect().center()) != center);
    const QPoint panStart = blankPoint(view);
    const QPointF beforePan = view.mapToScene(view.viewport()->rect().center());
    dragMiddle(view, panStart, panStart + QPoint(35, 20));
    CHECK(view.mapToScene(view.viewport()->rect().center()) != beforePan);
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Left));
    CHECK(editor.layoutDirection() == Editor::LayoutDirection::Left);
    editor.fitToContents();
    CHECK(editor.focusRoot() && editor.selectedNodeId() == root);
    CHECK(exported(editor) == before && changed.isEmpty() && !editor.hasPendingEdit());

    // Switching the gate on during a preview cancels rather than committing it.
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    enum class Preview { Move, Link, Image, Reconnect };
    for (auto gesture : {Preview::Move, Preview::Link, Preview::Image, Preview::Reconnect}) {
        editor.setReadOnly(false);
        editor.resetZoom();
        QPoint end;
        if (gesture == Preview::Move) {
            CHECK(editor.selectNode(d));
            const QPoint start = labelPoint(editor, QStringLiteral("Delta"));
            end = labelPoint(editor, QStringLiteral("Beta"));
            QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
            movePointer(view, end, Qt::LeftButton);
            CHECK(view.viewport()->cursor().shape() == Qt::DragMoveCursor);
        } else if (gesture == Preview::Link) {
            CHECK(editor.selectNode(a));
            end = labelPoint(editor, QStringLiteral("Beta"));
            QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                              linkHandlePoint(editor, QStringLiteral("Alpha")));
            movePointer(view, end, Qt::LeftButton);
        } else if (gesture == Preview::Image) {
            CHECK(editor.selectNode(root));
            end = startImageResize(editor, QPointF(30, 15));
            checkImageSize(nodeImageRect(editor), QSizeF(150, 75));
        } else {
            CHECK(editor.selectLink(QStringLiteral("l1")));
            end = labelPoint(editor, QStringLiteral("Delta"));
            QGraphicsItem *handle = nullptr;
            for (auto *item : view.scene()->items())
                if (item->isVisible() && item->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations) &&
                    item->cursor().shape() == Qt::OpenHandCursor) { handle = item; break; }
            CHECK(handle);
            view.ensureVisible(handle, 30, 30); pump();
            const QPoint start = handle->deviceTransform(view.viewportTransform()).map(handle->boundingRect().center()).toPoint();
            CHECK(view.itemAt(start) == handle);
            QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
            movePointer(view, end, Qt::LeftButton);
        }
        const QStringList nodes = editor.selectedNodeIds();
        const QString link = editor.selectedLinkId();
        CHECK(exported(editor) == before && changed.isEmpty());
        editor.setReadOnly(true);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, end);
        pump();
        CHECK(editor.isReadOnly() && exported(editor) == before && changed.isEmpty());
        CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == link);
        CHECK(!editor.hasPendingEdit() && !activeTopicInput(editor));
        if (gesture == Preview::Image) checkImageSize(nodeImageRect(editor), QSizeF(120, 60));
    }
    editor.setReadOnly(false);
    CHECK(editor.selectLink(QStringLiteral("l1")));
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QStringLiteral("Canceled link draft"));
    CHECK(editor.hasPendingEdit());
    editor.setReadOnly(true);
    CHECK(!editor.hasPendingEdit() && !activeTopicInput(editor));
    CHECK(editor.selectedLinkId() == QStringLiteral("l1") && exported(editor) == before && changed.isEmpty());
    editor.setReadOnly(false);
    CHECK(!editor.isReadOnly() && editor.selectNode(a));
    CHECK(editAction(editor, "editSelection").isEnabled());
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QStringLiteral("Editing restored"));
    CHECK(editor.commitActiveEdit());
    Json edited = before; setTopic(edited, "a", QStringLiteral("Editing restored"));
    CHECK(exported(editor) == edited && changed.size() == 1);
    editor.setReadOnly(true);
    CHECK(editor.newDocument(QStringLiteral("Host replacement")));
    CHECK(editor.isReadOnly() && changed.size() == 2);
    const Json replacement = exported(editor);
    CHECK(record(replacement, "nodes", QStringLiteral("root")).at("topic") == "Host replacement");
    CHECK(!editor.loadJson(QByteArray("{invalid")) && exported(editor) == replacement && changed.size() == 2);
    CHECK(editor.loadJson(encoded(before)) && exported(editor) == before && changed.size() == 3);
    CHECK(editor.isReadOnly() && !editor.hasPendingEdit());
    CHECK(editor.addNode(root, QStringLiteral("Still forbidden after load")).isEmpty());
    CHECK(exported(editor) == before && changed.size() == 3);
}

static void inline_label_hit_case() {
    for (qreal zoom : {qreal(1), qreal(1.5)}) {
        RawLabelEditor editor;
        CHECK(editor.newDocument(QString()));
        showEditor(editor);
        editor.zoom(zoom);
        pump();
        auto &view = graphics(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const Json original = exported(editor);
        const QPoint blank = blankPoint(view);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, blank);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, blank);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, blank);
        pump();
        CHECK(activeTopicInput(editor) == nullptr && editor.selectedNodeId().isEmpty());
        CHECK(exported(editor) == original && changed.isEmpty());
        const QPoint center = view.mapFromScene(emptyNodeRect(editor).center());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, center);
        pump();
        CHECK(topicInput(editor).toPlainText().isEmpty());
        CHECK(editor.selectedNodeId() == QStringLiteral("root"));
        CHECK(exported(editor) == original && changed.isEmpty());
        topicInput(editor).setPlainText(QStringLiteral("Named empty topic"));
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == "Named empty topic");
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
    }
    const QString topic = QStringLiteral("Label first line\nLabel second line");
    for (int imageState : {0, 1, 2}) for (qreal zoom : {qreal(1), qreal(1.5)}) {
        RawLabelEditor editor;
        if (imageState != 0) {
            QObject::connect(&editor, &Editor::imageRequested, &editor,
                [&editor, imageState](const QString &url, quint64 id) {
                    editor.provideImage(url, id, imageState == 1 ? imagePixels() : QImage());
                });
        }
        CHECK(editor.loadJson(encoded(imageDocument())));
        CHECK(editor.updateNode(QStringLiteral("r"), encoded(Json{
            {"topic", utf8(topic)}, {"icons", Json::array({"Icon row"})}})));
        showEditor(editor);
        editor.zoom(zoom);
        pump();
        auto &view = graphics(editor);
        const Json original = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        auto doubleClick = [&](const QPoint &point) {
            QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, point);
            pump();
        };
        auto noEdit = [&](const QPoint &point) {
            doubleClick(point);
            CHECK(activeTopicInput(editor) == nullptr);
            CHECK(editor.selectedNodeId() == QStringLiteral("r"));
            CHECK(exported(editor) == original && changed.isEmpty());
        };
        // Hit the image body, not its separately guarded resize handle.
        noEdit(view.mapFromScene(nodeImageRect(editor, topic).center()));
        noEdit(labelPoint(editor, QStringLiteral("one")));
        auto *tag = textItem(editor, QStringLiteral("one"));
        const QRectF tagText = tag->sceneBoundingRect();
        const QPoint tagPadding = view.mapFromScene(QPointF(tagText.left() - 3, tagText.center().y()));
        CHECK(view.itemAt(tagPadding) == ownerItem(tag));
        noEdit(tagPadding);
        noEdit(labelPoint(editor, QStringLiteral("Icon row")));
        const QRectF node = topicRect(editor, topic);
        doubleClick(view.mapFromScene(QPointF(node.left() + 6, node.center().y())));
        CHECK(topicInput(editor).toPlainText() == topic);
        CHECK(exported(editor) == original && changed.isEmpty());
        topicKey(editor, Qt::Key_Escape);
        const QPoint expansion = view.mapFromScene(QPointF(node.right() - 15, node.center().y()));
        noEdit(expansion);
        CHECK(!texts(editor, QStringLiteral("Alpha")).isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, expansion);
        CHECK(texts(editor, QStringLiteral("Alpha")).isEmpty());
        noEdit(expansion);
        CHECK(texts(editor, QStringLiteral("Alpha")).isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, expansion);
        CHECK(!texts(editor, QStringLiteral("Alpha")).isEmpty());
        doubleClick(labelPoint(editor, topic));
        CHECK(topicInput(editor).toPlainText() == topic);
        CHECK(exported(editor) == original && changed.isEmpty());
        topicKey(editor, Qt::Key_Escape);
    }
}

static void inline_edit_case() {
    inline_label_hit_case();
    inline_tags_case();
    {
        RawLabelEditor editor;
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
        RawLabelEditor editor;
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
        const QPoint circle = expansionPoint(editor, draft);
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::NoModifier, circle);
        pump();
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 1);
        CHECK(editor.selectNode(QStringLiteral("a")));
        const Json unchanged = exported(editor);
        shortcut(editor, Qt::Key_F2);
        topicKey(editor, Qt::Key_Return, Qt::ControlModifier);
        CHECK(exported(editor) == unchanged && changed.size() == 1);
    }
    {
        RawLabelEditor editor;
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
        const qreal scale = graphics(editor).transform().m11();
        trigger(editor, "zoomIn");
        pump();
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "a \n\tz");
        CHECK(changed.size() == 1 && graphics(editor).transform().m11() > scale);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Before programmatic action"));
        trigger(editor, "toggleExpanded");
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Before programmatic action");
        CHECK(texts(editor, QStringLiteral("Delta")).isEmpty() && changed.size() == 2);
        shortcut(editor, Qt::Key_Home);
        CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    }
    {
        RawLabelConfig config;
        config.shortcuts.acceptTopic = {QKeySequence(QStringLiteral("Return, Ctrl+S")), QKeySequence(Qt::ALT | Qt::Key_Return)};
        config.shortcuts.toggleExpanded.clear();
        config.shortcuts.selectRoot = {QKeySequence(Qt::CTRL | Qt::Key_H), QKeySequence(Qt::ALT | Qt::Key_H)};
        RawLabelEditor editor(config);
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
        CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty() && changed.size() == 2);
        shortcut(editor, Qt::Key_H, Qt::AltModifier);
        CHECK(editor.selectedNodeId() == QStringLiteral("r"));
        CHECK(editor.selectNode(QStringLiteral("a")));
        shortcut(editor, Qt::Key_H, Qt::ControlModifier);
        CHECK(editor.selectedNodeId() == QStringLiteral("r"));
    }
    {
        RawLabelConfig config;
        config.shortcuts.acceptTopic.clear();
        RawLabelEditor editor(config);
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
        RawLabelEditor editor;
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
        CHECK(activeTopicInput(editor) == nullptr && changed.isEmpty());
        CHECK(record(exported(editor), "nodes", QStringLiteral("d")).at("topic") == "Delta");
    }
    {
        QWidget host;
        QVBoxLayout layout(&host);
        RawLabelEditor first(&host), second(&host);
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
        auto editor = std::make_unique<RawLabelEditor>();
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
        RawLabelEditor editor(&host);
        layout.addWidget(&editor);
        host.resize(1000, 700); host.show();
        showEditor(editor);
        QAction save; // Window association, not QObject parenting, owns its shortcut.
        save.setShortcut(QKeySequence::Save);
        save.setShortcutContext(Qt::WindowShortcut);
        host.addAction(&save);
        editor.resetZoom(); // Leave room for the later zoom-in command.
        QShortcut custom(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J), &host);
        custom.setContext(Qt::WindowShortcut);
        Json saved, closed;
        QPointer<QPlainTextEdit> input;
        int captures = 0;
        const auto capture = [&] {
            CHECK(input && activeTopicInput(editor) == input.data() && input->hasFocus());
            CHECK(input->textCursor().anchor() == 2 && input->textCursor().position() == 8);
            CHECK(!editor.hasPendingEdit());
            saved = exported(editor);
            ++captures;
        };
        QObject::connect(&save, &QAction::triggered, &host, capture);
        QObject::connect(&custom, &QShortcut::activated, &host, capture);
        host.observeClose = [&] { closed = exported(editor); };
        QSignalSpy changed(&editor, &Editor::documentChanged);
        shortcut(editor, Qt::Key_F2);
        input = &topicInput(editor);
        input->setPlainText(QStringLiteral("Host save sees draft"));
        auto cursor = input->textCursor();
        cursor.setPosition(2); cursor.setPosition(8, QTextCursor::KeepAnchor);
        input->setTextCursor(cursor);
        topicKey(editor, Qt::Key_S, Qt::ControlModifier);
        CHECK(record(saved, "nodes", QStringLiteral("root")).at("topic") == "Host save sees draft");
        CHECK(captures == 1 && changed.size() == 1 && activeTopicInput(editor) == input.data());
        topicKey(editor, Qt::Key_S, Qt::ControlModifier);
        CHECK(captures == 2 && changed.size() == 1);
        input->setPlainText(QStringLiteral("Custom shortcut sees current draft"));
        cursor = input->textCursor();
        cursor.setPosition(2); cursor.setPosition(8, QTextCursor::KeepAnchor);
        input->setTextCursor(cursor);
        topicKey(editor, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        CHECK(record(saved, "nodes", QStringLiteral("root")).at("topic") == "Custom shortcut sees current draft");
        CHECK(captures == 3 && changed.size() == 2 && activeTopicInput(editor) == input.data());
        topicKey(editor, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        CHECK(captures == 4 && changed.size() == 2);
        input->setPlainText(QStringLiteral("Invalid") + QChar(QChar::Null));
        topicKey(editor, Qt::Key_S, Qt::ControlModifier);
        CHECK(captures == 4 && changed.size() == 2 && exported(editor) == saved);
        CHECK(activeTopicInput(editor) == input.data() && editor.hasPendingEdit());
        input->setPlainText(QStringLiteral("Library command still ends editing"));
        const qreal scale = graphics(editor).transform().m11();
        trigger(editor, "zoomIn");
        CHECK(!activeTopicInput(editor) && !editor.hasPendingEdit() && changed.size() == 3);
        CHECK(graphics(editor).transform().m11() > scale);
        CHECK(record(exported(editor), "nodes", QStringLiteral("root")).at("topic") == "Library command still ends editing");
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Host close sees draft"));
        host.close();
        pump();
        CHECK(record(closed, "nodes", QStringLiteral("root")).at("topic") == "Host close sees draft");
        CHECK(changed.size() == 4 && activeTopicInput(editor) == nullptr);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Host popup sees draft"));
        QMenu popup(&host);
        auto *observe = popup.addAction(QStringLiteral("Observe saved topic"));
        QObject::connect(observe, &QAction::triggered, &host, [&] { saved = exported(editor); });
        popup.popup(host.mapToGlobal(QPoint(30, 30)));
        pump();
        CHECK(activeTopicInput(editor) == nullptr && changed.size() == 5);
        popup.setActiveAction(observe);
        QTest::keyClick(&popup, Qt::Key_Return);
        pump();
        CHECK(record(saved, "nodes", QStringLiteral("root")).at("topic") == "Host popup sees draft");
    }
    {
        RawLabelEditor editor;
        showEditor(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
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

static void shortcut_help_case() {
    auto popup = [](Editor &editor) {
        auto *dialog = editor.findChild<QDialog *>(QStringLiteral("shortcutHelpPopup"));
        return dialog && dialog->isVisible() ? dialog : nullptr;
    };
    auto open = [&](Editor &editor, Qt::Key key = Qt::Key_Question, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        shortcut(editor, key, modifiers);
        CHECK(QTest::qWaitFor([&] { return popup(editor) != nullptr; }, 5000));
        auto *dialog = popup(editor);
        auto *browser = dialog->findChild<QTextBrowser *>(QStringLiteral("shortcutHelpBrowser"));
        CHECK(browser && QApplication::activePopupWidget() == dialog);
        CHECK(QTest::qWaitFor([&] { return browser->hasFocus(); }, 5000));
        CHECK(dialog->screen()->availableGeometry().contains(dialog->frameGeometry()));
        return browser;
    };
    auto escape = [&](Editor &editor) {
        CHECK(popup(editor) != nullptr);
        auto *browser = popup(editor)->findChild<QTextBrowser *>();
        QTest::keyClick(browser, Qt::Key_Escape);
        pump();
        CHECK(!popup(editor) && graphics(editor).hasFocus());
    };
    auto outside = [&](Editor &editor, QWidget &target, QPoint point = QPoint(4, 4)) {
        auto *dialog = popup(editor);
        CHECK(dialog && target.windowHandle());
        const QPoint global = target.mapToGlobal(point);
        // Exercise outside-click handling independently of toolbar width and screen placement.
        if (dialog->frameGeometry().contains(global)) dialog->move(global + QPoint(24, 24));
        // Use the window-system path, not QWidget delivery that bypasses popup grabs.
        QTest::mouseClick(target.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        pump();
        CHECK(!popup(editor));
    };
    using Shortcuts = m3::qt::EditorConfig::Shortcuts;
    QList<QKeySequence> Shortcuts::*const members[] = {
        &Shortcuts::addChild, &Shortcuts::addSibling, &Shortcuts::addSiblingBefore, &Shortcuts::editSelection,
        &Shortcuts::acceptTopic, &Shortcuts::deleteSelection, &Shortcuts::toggleExpanded, &Shortcuts::moveNode,
        &Shortcuts::moveUp, &Shortcuts::moveDown, &Shortcuts::addLink, &Shortcuts::selectParent,
        &Shortcuts::selectChild, &Shortcuts::previousSibling, &Shortcuts::nextSibling, &Shortcuts::selectRoot,
        &Shortcuts::clearSelection, &Shortcuts::zoomIn, &Shortcuts::zoomOut, &Shortcuts::resetZoom, &Shortcuts::fit,
        &Shortcuts::toggleBold, &Shortcuts::toggleItalic, &Shortcuts::resetStyle, &Shortcuts::textColor,
        &Shortcuts::fillColor, &Shortcuts::editTags, &Shortcuts::editIcons, &Shortcuts::editNote, &Shortcuts::editTopic, &Shortcuts::showHelp, &Shortcuts::toggleProperties
    };
    RawLabelConfig config;
    QList<QKeySequence> expectedKeys;
    for (size_t index = 0; index < std::size(members); ++index) {
        const QKeySequence binding(Qt::CTRL | Qt::ALT | (Qt::Key_F1 + int(index)));
        config.shortcuts.*members[index] = {binding};
        expectedKeys.append(binding);
    }
    // Multiple bindings and HTML-sensitive glyphs must survive rich-text rendering.
    const QKeySequence less(Qt::CTRL | Qt::Key_Less), ampersand(Qt::ALT | Qt::Key_Ampersand);
    config.shortcuts.toggleBold.append(less);
    config.shortcuts.toggleItalic.append(ampersand);
    expectedKeys.append(less); expectedKeys.append(ampersand);
    const QKeySequence newline(Qt::SHIFT | Qt::Key_Return), keypadNewline(Qt::SHIFT | Qt::Key_Enter);
    config.shortcuts.acceptTopic.append(newline); config.shortcuts.acceptTopic.append(keypadNewline);
    {
        RawLabelEditor editor(config);
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor, QSize(760, 440));
        shortcut(editor, Qt::Key_Question);
        CHECK(!popup(editor));
        auto configuredHelp = [&] { return open(editor, Qt::Key_F31, Qt::ControlModifier | Qt::AltModifier); };
        auto *browser = configuredHelp();
        const QString original = browser->toPlainText();
        const QStringList lines = original.split(QLatin1Char('\n'));
        for (const auto &binding : expectedKeys) {
            const QString key = binding.toString(QKeySequence::NativeText);
            CHECK(lines.contains(key));
            CHECK(browser->document()->find(key).charFormat().fontWeight() >= QFont::Bold);
        }
        CHECK(lines.count(newline.toString(QKeySequence::NativeText)) == 1);
        CHECK(lines.count(keypadNewline.toString(QKeySequence::NativeText)) == 1);
        escape(editor);
        editor.clearSelection();
        CHECK(configuredHelp()->toPlainText() == original);
        escape(editor);
        CHECK(editor.selectLink(QStringLiteral("l1")));
        CHECK(configuredHelp()->toPlainText() == original);
        escape(editor);
        CHECK(editor.selectedLinkId() == QStringLiteral("l1"));
        CHECK(editor.selectNode(QStringLiteral("a")));
        const auto zoom = graphics(editor).transform();
        const Json saved = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        CHECK(configuredHelp()->toPlainText() == original);
        escape(editor);
        CHECK(exported(editor) == saved && changed.isEmpty() && selected.isEmpty() && graphics(editor).transform() == zoom);
        CHECK(!editor.renameNode(QStringLiteral("missing"), QStringLiteral("Missing")));
        const QString error = editor.lastError();
        QSignalSpy errors(&editor, &Editor::errorOccurred);
        CHECK(configuredHelp()->toPlainText() == original && editor.lastError() == error && errors.isEmpty());
        escape(editor);
        CHECK(exported(editor) == saved && changed.isEmpty() && selected.isEmpty());
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor, QSize(760, 440));
        const Json saved = exported(editor);
        const auto zoom = graphics(editor).transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        shortcut(editor, Qt::Key_Slash);
        CHECK(!popup(editor));
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Question, Qt::NoModifier, QStringLiteral("?"), true);
        QCoreApplication::sendEvent(graphics(editor).viewport(), &repeat);
        CHECK(!popup(editor));
        for (auto modifiers : {Qt::NoModifier, Qt::ShiftModifier}) {
            auto *browser = open(editor, Qt::Key_Question, modifiers);
            CHECK(browser->toPlainText().split(QLatin1Char('\n')).contains(QKeySequence(Qt::Key_Question).toString(QKeySequence::NativeText)));
            QTest::keyClicks(browser, "bir");
            QTest::keyClick(browser, Qt::Key_Return);
            CHECK(popup(editor) && exported(editor) == saved && changed.isEmpty() && selected.isEmpty());
            CHECK(browser->verticalScrollBar()->maximum() > 0);
            QTest::keyClick(browser, Qt::Key_End, Qt::ControlModifier);
            CHECK(browser->verticalScrollBar()->value() > 0);
            escape(editor);
            CHECK(editor.selectedNodeId() == QStringLiteral("a") && graphics(editor).transform() == zoom);
        }
        open(editor);
        outside(editor, editor);
        CHECK(exported(editor) == saved && changed.isEmpty() && selected.isEmpty());
        open(editor);
        escape(editor);
        shortcut(editor, Qt::Key_E);
        QTest::keyClicks(&topicInput(editor), "draft?"); pump();
        CHECK(!popup(editor) && topicInput(editor).toPlainText() == QStringLiteral("draft?"));
        CHECK(exported(editor) == saved && changed.isEmpty());
        topicKey(editor, Qt::Key_Escape);
        shortcut(editor, Qt::Key_T);
        auto *tags = editor.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        CHECK(tags && tags->hasFocus());
        QTest::keyClicks(tags, "?"); pump();
        CHECK(tags->hasFocus() && tags->text() == QStringLiteral("?") && !popup(editor));
        CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("tags") == Json::array({"?"}));
        CHECK(changed.size() == 1 && selected.isEmpty());
        const Json typed = exported(editor);
        open(editor);
        escape(editor);
        CHECK(exported(editor) == typed && changed.size() == 1);
    }
    {
        RawLabelConfig disabled;
        disabled.shortcuts.showHelp.clear();
        RawLabelEditor unbound(disabled), defaults;
        showEditor(unbound, QSize(760, 440));
        const Json saved = exported(unbound);
        QSignalSpy changed(&unbound, &Editor::documentChanged);
        shortcut(unbound, Qt::Key_Question);
        CHECK(!popup(unbound) && exported(unbound) == saved && changed.isEmpty());
        showEditor(defaults, QSize(760, 440));
        auto *browser = open(defaults);
        const auto lines = browser->toPlainText().split(QLatin1Char('\n'));
        CHECK(lines.contains(QKeySequence(Qt::Key_B).toString(QKeySequence::NativeText)));
        CHECK(!lines.contains(expectedKeys.front().toString(QKeySequence::NativeText)));
        CHECK(!popup(unbound));
        outside(defaults, unbound);
        // No deferred owner-focus callback may steal a subsequently activated host.
        unbound.activateWindow(); graphics(unbound).setFocus(); pump();
        CHECK(graphics(unbound).hasFocus() && !popup(defaults) && !popup(unbound));
        CHECK(exported(unbound) == saved && changed.isEmpty());
        shortcut(unbound, Qt::Key_Question);
        CHECK(!popup(unbound) && !popup(defaults));
    }
    {
        RawLabelConfig partial;
        partial.shortcuts.toggleBold.clear(); partial.shortcuts.acceptTopic.clear();
        RawLabelEditor editor(partial);
        showEditor(editor, QSize(760, 440));
        const auto lines = open(editor)->toPlainText().split(QLatin1Char('\n'));
        CHECK(!lines.contains(QKeySequence(Qt::Key_B).toString(QKeySequence::NativeText)));
        CHECK(!lines.contains(QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText)));
        escape(editor);
    }
    {
        QPointer<Editor> editor = new RawLabelEditor;
        showEditor(*editor, QSize(760, 440));
        open(*editor);
        const QPointer<QDialog> dialog = popup(*editor);
        delete editor.data(); pump();
        CHECK(editor.isNull() && dialog.isNull() && QApplication::activePopupWidget() == nullptr);
    }
}

static void configuration_case() {
    {
        // Exercise production defaults, independently of the raw-label fixtures above.
        Editor editor;
        Json input = Json::parse(R"({"schemaVersion":1,"rootId":"r","orphanIds":["o","p"],"nodes":[
            {"id":"r","topic":"Main root","children":["a","b"]},
            {"id":"a","topic":"Alpha","children":["d1"]},
            {"id":"b","topic":"2. Authored numeric prefix"},
            {"id":"o","topic":"First orphan","children":["x","y"]},
            {"id":"x","topic":"Orphan first"},{"id":"y","topic":"Orphan second"},
            {"id":"p","topic":"Second orphan","children":["z"]},
            {"id":"z","topic":"Other first"}],
            "crossLinks":[{"id":"l","source":"a","target":"b","directed":false,"topic":"Authored link"}]})");
        for (int depth = 1; depth <= 8; ++depth) {
            Json node{{"id", "d" + std::to_string(depth)}, {"topic", "Deep " + std::to_string(depth)}};
            if (depth < 8) node["children"] = Json::array({"d" + std::to_string(depth + 1)});
            input["nodes"].push_back(std::move(node));
        }
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor);
        for (int depth = 1; depth <= 8; ++depth)
            CHECK(editor.setExpanded(QStringLiteral("d%1").arg(depth), true));
        CHECK(editor.setExpanded(QStringLiteral("b"), true));
        const Json baseline = exported(editor);
        const QString markdown = editor.toMarkdown();
        auto label = [&](const QString &text) { CHECK(texts(editor, text).size() == 1); };
        auto roots = [&] {
            label(QStringLiteral("Main root"));
            label(QStringLiteral("First orphan"));
            label(QStringLiteral("Second orphan"));
        };
        auto numbered = [&] {
            roots();
            label(QStringLiteral("1. Alpha"));
            label(QStringLiteral("2. 2. Authored numeric prefix"));
            label(QStringLiteral("1. Orphan first"));
            label(QStringLiteral("2. Orphan second"));
            label(QStringLiteral("1. Other first"));
            QString number = QStringLiteral("1.");
            for (int depth = 1; depth <= 8; ++depth) {
                number += QStringLiteral("1.");
                label(number + QStringLiteral(" Deep %1").arg(depth));
            }
        };
        auto semantic = [&] {
            CHECK(exported(editor) == baseline && editor.toMarkdown() == markdown);
            const auto outline = editor.outline();
            CHECK(outline.size() == baseline.at("nodes").size());
            for (const auto &entry : outline)
                CHECK(entry.topic == qs(record(baseline, "nodes", entry.id).at("topic")));
            CHECK(markdown.contains(QStringLiteral("## <a id=\"m3-node-61\"></a>Alpha\n")));
            CHECK(!markdown.contains(QStringLiteral("1. Alpha")));
        };
        numbered(); semantic();
        CHECK(!editor.canUndo() && !editor.canRedo());
        const QString deep = QStringLiteral("1.1.1.1.1.1.1.1.1. Deep 8");
        const qreal numberedWidth = topicRect(editor, deep).width();
        CHECK(topicRect(editor, deep).contains(textItem(editor, deep)->sceneBoundingRect()));
        const HtmlPage html(editor.toHtml());
        CHECK(html.classValues(QStringLiteral("m3-topic"), html.node(QStringLiteral("a"))) == QStringList{QStringLiteral("1. Alpha")});
        CHECK(html.classValues(QStringLiteral("m3-topic"), html.node(QStringLiteral("d8"))) == QStringList{deep});
        CHECK(html.classValues(QStringLiteral("m3-article-topic"), html.article(QStringLiteral("a"))) == QStringList{QStringLiteral("Alpha")});
        CHECK(html.classValues(QStringLiteral("m3-article-topic"), html.article(QStringLiteral("b"))) == QStringList{QStringLiteral("2. Authored numeric prefix")});
        const auto deepTopics = html.classes(QStringLiteral("m3-topic"), html.node(QStringLiteral("d8")));
        CHECK(deepTopics.size() == 1);
        auto *deepLabel = textItem(editor, deep);
        auto *deepOwner = ownerItem(deepLabel);
        nearHtml(htmlRect(html.elements[deepTopics.front()]), deepLabel->sceneBoundingRect().translated(
            -deepOwner->mapToScene(deepOwner->shape()).boundingRect().topLeft()));
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        QSignalSpy pending(&editor, &Editor::pendingEditChanged), undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
        for (const auto &pattern : {QStringLiteral("1.1"), QStringLiteral("1.1."), QStringLiteral("1.1)"), QStringLiteral("invalid")}) {
            editor.setSectionNumbering(true, pattern);
            roots();
            const QString suffix = pattern == QStringLiteral("1.1") ? QString() :
                pattern == QStringLiteral("1.1)") ? QStringLiteral(")") : QStringLiteral(".");
            label(QStringLiteral("1") + suffix + QStringLiteral(" Alpha"));
            label(QStringLiteral("1.1") + suffix + QStringLiteral(" Deep 1"));
            label(QStringLiteral("1.1.1.1.1.1.1.1.1") + suffix + QStringLiteral(" Deep 8"));
            semantic();
        }
        // Invalid and canonical patterns normalize to the same state, including during a fold.
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        editor.setSectionNumbering(true, QStringLiteral("1.1)"));
        CHECK(texts(editor, QStringLiteral("1.1) Deep 1")).isEmpty());
        label(QStringLiteral("2) 2. Authored numeric prefix"));
        CHECK(editor.setExpanded(QStringLiteral("a"), true));
        label(QStringLiteral("1.1.1.1.1.1.1.1.1) Deep 8"));
        editor.setSectionNumbering(false, QStringLiteral("1.1)"));
        roots(); label(QStringLiteral("Alpha")); label(QStringLiteral("2. Authored numeric prefix"));
        CHECK(topicRect(editor, QStringLiteral("Deep 8")).width() < numberedWidth);
        editor.setSectionNumbering(true, QStringLiteral("invalid"));
        numbered(); semantic();
        auto *unchangedLabel = textItem(editor, QStringLiteral("1. Alpha"));
        editor.setSectionNumbering(true, QStringLiteral("1.1."));
        CHECK(textItem(editor, QStringLiteral("1. Alpha")) == unchangedLabel);
        CHECK(changed.isEmpty() && selected.isEmpty() && pending.isEmpty() && undo.isEmpty() && redo.isEmpty());
        CHECK(!editor.canUndo() && !editor.canRedo());

        auto structural = [&](const std::function<bool()> &operation, const std::function<void()> &after) {
            CHECK(operation());
            const Json moved = exported(editor);
            after();
            CHECK(editor.undo()); numbered(); semantic();
            CHECK(!editor.canUndo() && editor.canRedo());
            editor.setSectionNumbering(false, QStringLiteral("1.1"));
            editor.setSectionNumbering(true, QStringLiteral("1.1."));
            CHECK(editor.redo() && exported(editor) == moved); after();
            CHECK(editor.undo()); numbered(); semantic();
        };
        structural([&] { return editor.moveNode(QStringLiteral("b"), QStringLiteral("r"), 0); }, [&] {
            roots(); label(QStringLiteral("1. 2. Authored numeric prefix"));
            label(QStringLiteral("2. Alpha")); label(QStringLiteral("2.1. Deep 1"));
        });
        structural([&] { return editor.moveNode(QStringLiteral("a"), QStringLiteral("o"), 1); }, [&] {
            roots(); label(QStringLiteral("1. 2. Authored numeric prefix"));
            label(QStringLiteral("1. Orphan first")); label(QStringLiteral("2. Alpha"));
            label(QStringLiteral("2.1. Deep 1")); label(QStringLiteral("3. Orphan second"));
        });
        structural([&] { return editor.moveNode(QStringLiteral("a"), {}); }, [&] {
            roots(); label(QStringLiteral("Alpha")); label(QStringLiteral("1. Deep 1"));
            label(QStringLiteral("1.1.1.1.1.1.1.1. Deep 8"));
            label(QStringLiteral("1. 2. Authored numeric prefix"));
        });
        structural([&] { return editor.moveNode(QStringLiteral("o"), QStringLiteral("b")); }, [&] {
            label(QStringLiteral("Main root")); label(QStringLiteral("Second orphan"));
            label(QStringLiteral("2.1. First orphan")); label(QStringLiteral("2.1.1. Orphan first"));
            label(QStringLiteral("2.1.2. Orphan second")); label(QStringLiteral("1. Other first"));
        });

        // Refresh must retain semantic draft seeds, local undo, focus, caret and accepted snapshots.
        for (bool link : {false, true}) {
            CHECK(editor.loadJson(encoded(input)));
            CHECK(editor.renameNode(QStringLiteral("r"), QStringLiteral("History marker")));
            CHECK(editor.renameNode(QStringLiteral("r"), QStringLiteral("Future marker")));
            CHECK(editor.undo());
            CHECK(editor.canUndo() && editor.canRedo());
            CHECK(link ? editor.selectLink(QStringLiteral("l")) : editor.selectNode(QStringLiteral("b")));
            const Json before = exported(editor);
            const QString beforeMarkdown = editor.toMarkdown();
            const QStringList nodeSelection = editor.selectedNodeIds();
            const QString linkSelection = editor.selectedLinkId();
            shortcut(editor, Qt::Key_F2);
            QPointer<QPlainTextEdit> draft = &topicInput(editor);
            const QString original = link ? QStringLiteral("Authored link") : QStringLiteral("2. Authored numeric prefix");
            CHECK(draft->toPlainText() == original && !editor.hasPendingEdit());
            editor.setSectionNumbering(false, QStringLiteral("1.1)"));
            CHECK(activeTopicInput(editor) == draft.data() && draft->toPlainText() == original && !editor.hasPendingEdit());
            editor.setSectionNumbering(true, QStringLiteral("1.1."));
            const QString edited = link ? QStringLiteral("7. Link draft #literal") : QStringLiteral("8. Authored draft");
            draft->selectAll(); draft->insertPlainText(edited);
            auto cursor = draft->textCursor();
            cursor.setPosition(2); cursor.setPosition(7, QTextCursor::KeepAnchor);
            draft->setTextCursor(cursor);
            const QTransform zoom = graphics(editor).transform();
            const QPointF center = graphics(editor).mapToScene(graphics(editor).viewport()->rect().center());
            changed.clear(); selected.clear(); pending.clear(); undo.clear(); redo.clear();
            auto retained = [&] {
                pump();
                CHECK(draft && activeTopicInput(editor) == draft.data() && draft->hasFocus());
                CHECK(draft->toPlainText() == edited && editor.hasPendingEdit());
                CHECK(draft->textCursor().anchor() == 2 && draft->textCursor().position() == 7);
                CHECK(draft->document()->isUndoAvailable() && editor.selectedText() == edited.mid(2, 5));
                CHECK(exported(editor) == before && editor.toMarkdown() == beforeMarkdown);
                CHECK(editor.selectedNodeIds() == nodeSelection && editor.selectedLinkId() == linkSelection);
                CHECK(editor.canUndo() && editor.canRedo());
                CHECK(changed.isEmpty() && selected.isEmpty() && pending.isEmpty() && undo.isEmpty() && redo.isEmpty());
                CHECK(graphics(editor).transform() == zoom);
                CHECK(QLineF(center, graphics(editor).mapToScene(graphics(editor).viewport()->rect().center())).length() <= 2.0 / zoom.m11());
                CHECK(texts(editor, QStringLiteral("1.1.1. Deep 2")).isEmpty());
            };
            editor.setSectionNumbering(false, QStringLiteral("1.1)")); retained();
            editor.setSectionNumbering(true, QStringLiteral("1.1)")); retained();
            editor.setStyle({QString(), link ? 22 : 18}); retained();
            editor.setSectionNumbering(true, QStringLiteral("1.1")); retained();
            editor.setSectionNumbering(true, QStringLiteral("invalid")); retained();
            editor.setSectionNumbering(true, QStringLiteral("1.1.")); retained();
            topicKey(editor, Qt::Key_Z, Qt::ControlModifier);
            CHECK(draft->toPlainText() == original && !editor.hasPendingEdit());
            topicKey(editor, Qt::Key_Y, Qt::ControlModifier);
            CHECK(draft->toPlainText() == edited && editor.hasPendingEdit());
            cursor = draft->textCursor();
            cursor.setPosition(2); cursor.setPosition(7, QTextCursor::KeepAnchor);
            draft->setTextCursor(cursor);
            CHECK(editor.commitActiveEdit(true));
            CHECK(draft && activeTopicInput(editor) == draft.data() && draft->hasFocus());
            CHECK(draft->toPlainText() == edited && !editor.hasPendingEdit());
            CHECK(draft->textCursor().anchor() == 2 && draft->textCursor().position() == 7);
            CHECK(changed.size() == 1 && editor.canUndo() && !editor.canRedo());
            Json saved = before;
            if (link) saved["crossLinks"][0]["topic"] = utf8(edited);
            else setTopic(saved, "b", edited);
            CHECK(exported(editor) == saved);
            editor.setSectionNumbering(false, QStringLiteral("1.1)"));
            editor.setSectionNumbering(true, QStringLiteral("1.1)"));
            CHECK(draft && activeTopicInput(editor) == draft.data() && draft->toPlainText() == edited);
            CHECK(draft->hasFocus() && draft->textCursor().anchor() == 2 && draft->textCursor().position() == 7);
            CHECK(!editor.hasPendingEdit() && editor.commitActiveEdit(true) && changed.size() == 1);
            draft->selectAll(); draft->insertPlainText(QStringLiteral("Canceled replacement"));
            editor.setSectionNumbering(true, QStringLiteral("1.1."));
            topicKey(editor, Qt::Key_Escape);
            CHECK(!activeTopicInput(editor) && exported(editor) == saved && changed.size() == 1);
            label(link ? edited : QStringLiteral("2. ") + edited);
            CHECK(editor.undo() && exported(editor) == before);
            CHECK(editor.undo() && exported(editor) == baseline && !editor.canUndo());
            CHECK(editor.redo() && editor.redo() && exported(editor) == saved);
        }
    }
    for (int levels : {1, 2, 4, 10, 0, -1}) {
        RawLabelConfig config;
        config.initialVisibleLevels = levels;
        RawLabelEditor editor(config);
        config.initialVisibleLevels = 1; // Construction copies policy; later host changes are irrelevant.
        CHECK(editor.loadJson(encoded(fiveLevelFixture())));
        showEditor(editor);
        visibleLevels(editor, levels <= 0 ? 5 : std::min(levels, 5));
        CHECK(exported(editor) == nativeDocument(fiveLevelFixture()));
    }
    // Legacy folding is accepted on complete records but never overrides editor policy.
    for (bool legacy : {false, true}) {
        Json native = fiveLevelFixture();
        for (auto &node : native["nodes"]) node["expanded"] = node.at("id") == "r" ? legacy : !legacy;
        Json tree;
        for (const char *id : {"i", "h", "g", "c", "r"}) {
            Json parent = record(native, "nodes", QString::fromLatin1(id));
            parent.erase("children");
            if (!tree.is_null()) parent["children"] = Json::array({tree});
            tree = std::move(parent);
        }
        Json mindElixir{{"nodeData", tree}};
        for (Json *node = &mindElixir["nodeData"]; node;) {
            if (node->contains("note")) {
                (*node)["memo"] = (*node)["note"];
                node->erase("note");
            }
            node = node->contains("children") ? &(*node)["children"][0] : nullptr;
        }
        for (const Json &input : {native, tree, mindElixir}) {
            RawLabelEditor editor;
            CHECK(editor.loadJson(encoded(input)));
            showEditor(editor);
            for (int level = 1; level <= 5; ++level)
                CHECK(texts(editor, QStringLiteral("Level %1").arg(level)).isEmpty() == (level > 3));
            const Json saved = exported(editor);
            CHECK(saved.at("nodes").size() == 5 && editor.outline().size() == 5);
            for (const auto &node : saved.at("nodes")) CHECK(!node.contains("expanded"));
            CHECK(record(saved, "nodes", QStringLiteral("g")).at("children") == Json({"h"}));
            CHECK(record(saved, "nodes", QStringLiteral("i")).at("note") == "Retained hidden note");
            rejectUnchanged(editor, [&] { return editor.updateNode(QStringLiteral("g"), encoded(Json{{"expanded", true}})); });
            rejectUnchanged(editor, [&] { return editor.updateNode(QStringLiteral("g"), encoded(Json{{"topic", "Rejected"}, {"expanded", false}})); });
            CHECK(editor.setExpanded(QStringLiteral("g"), true));
            CHECK(!texts(editor, QStringLiteral("Level 4")).isEmpty());
            CHECK(texts(editor, QStringLiteral("Level 5")).isEmpty());
            Json invalid = input;
            if (invalid.contains("nodes")) invalid["nodes"][0]["expanded"] = "false";
            else if (invalid.contains("nodeData")) invalid["nodeData"]["expanded"] = "false";
            else invalid["expanded"] = "false";
            rejectUnchanged(editor, [&] { return editor.loadJson(encoded(invalid)); });
            CHECK(!texts(editor, QStringLiteral("Level 4")).isEmpty());
            CHECK(texts(editor, QStringLiteral("Level 5")).isEmpty());
        }
    }
    const Json styledDocument = Json::parse(R"({"schemaVersion":1,"rootId":"r","nodes":[
        {"id":"r","topic":"Configured root\nSecond line","children":["a","b"],"tags":["style-tag"],"icons":["ABC"]},
        {"id":"a","topic":"Inherited text\nSecond line"},
        {"id":"b","topic":"Pixel override","style":{"fontSize":24}}],
        "crossLinks":[{"id":"l","source":"a","target":"b","directed":false,"topic":"Styled link"}]})");
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto families = QFontDatabase::families();
#else
    const auto families = QFontDatabase().families();
#endif
    CHECK(!families.isEmpty());
    const QString family = families.front().section(QLatin1Char('['), 0, 0).trimmed();
    const QString fallback = QStringLiteral("  m3-unavailable-font-7EAF, , %1  ").arg(family.toLower());
    {
        RawLabelConfig config;
        config.style = {fallback, 18};
        RawLabelEditor editor(config);
        CHECK(textItem(editor, QStringLiteral("Central topic"))->font().pointSize() == 18);
        CHECK(editor.loadJson(encoded(styledDocument)));
        showEditor(editor);
        const QByteArray json = editor.toJson();
        const QString markdown = editor.toMarkdown();
        QSignalSpy changed(&editor, &Editor::documentChanged), pending(&editor, &Editor::pendingEditChanged);
        const auto unchanged = [&] {
            CHECK(editor.toJson() == json && editor.toMarkdown() == markdown);
            CHECK(changed.isEmpty() && pending.isEmpty() && !editor.canUndo() && !editor.canRedo());
            CHECK(textItem(editor, QStringLiteral("Pixel override"))->font().pixelSize() == 24);
        };
        const auto configuredFonts = [&] {
            for (const auto &text : {QStringLiteral("Configured root\nSecond line"),
                                    QStringLiteral("Inherited text\nSecond line"), QStringLiteral("Styled link"),
                                    QStringLiteral("ABC")}) {
                const QFont font = textItem(editor, text)->font();
                CHECK(font.family() == family && font.pointSize() == 18 && font.pixelSize() == -1);
            }
            const QFont tagFont = textItem(editor, QStringLiteral("style-tag"))->font();
            CHECK(tagFont.family() == family && qAbs(tagFont.pointSizeF() - 18 * 0.85) < 0.01);
            CHECK(textItem(editor, QStringLiteral("Configured root\nSecond line"))->font().bold());
            CHECK(!textItem(editor, QStringLiteral("Inherited text\nSecond line"))->font().bold());
            unchanged();
        };
        configuredFonts();
        const QSizeF largeMap = HtmlPage(editor.toHtml()).size();
        const qreal largeHeight = topicRect(editor, QStringLiteral("Inherited text\nSecond line")).height();
        const QSizeF pixelSize = topicRect(editor, QStringLiteral("Pixel override")).size();
        editor.setStyle({fallback, 9});
        pump();
        CHECK(topicRect(editor, QStringLiteral("Inherited text\nSecond line")).height() < largeHeight);
        CHECK(topicRect(editor, QStringLiteral("Pixel override")).size() == pixelSize);
        CHECK(HtmlPage(editor.toHtml()).size().height() < largeMap.height());
        unchanged();
        editor.setStyle(config.style);
        QFont hostFont = editor.font();
        hostFont.setFamily(families.back().section(QLatin1Char('['), 0, 0).trimmed());
        hostFont.setPointSize(13);
        hostFont.setItalic(true);
        editor.setFont(hostFont);
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Base, QColor(31, 35, 42));
        palette.setColor(QPalette::Text, QColor(230, 232, 235));
        editor.setPalette(palette);
        pump();
        configuredFonts();
        CHECK(editor.font() == hostFont);
        CHECK(textItem(editor, QStringLiteral("Inherited text\nSecond line"))->font().italic());
        CHECK(textItem(editor, QStringLiteral("Styled link"))->defaultTextColor() == palette.color(QPalette::Text));
        CHECK(graphics(editor).backgroundBrush().color() == palette.color(QPalette::Base));
        editor.setStyle({});
        pump();
        CHECK(graphics(editor).font() == hostFont);
        unchanged();
        // Family and size inherit independently; an unavailable list is not a platform fallback.
        editor.setStyle({QStringLiteral("m3-unavailable-font-7EAF"), 18});
        CHECK(graphics(editor).font().family() == hostFont.family() && graphics(editor).font().pointSize() == 18);
        editor.setStyle({fallback, 0});
        CHECK(graphics(editor).font().family() == family && graphics(editor).font().pointSize() == 13);
        hostFont.setPointSize(15);
        editor.setFont(hostFont);
        CHECK(graphics(editor).font().family() == family && graphics(editor).font().pointSize() == 15);
        editor.setStyle({fallback, 999});
        CHECK(graphics(editor).font().pointSize() == 256);
        editor.setStyle({QString(), -1});
        CHECK(graphics(editor).font() == hostFont);
        unchanged();
    }
    for (bool link : {false, true}) {
        RawLabelConfig config;
        config.style = {fallback, 12};
        RawLabelEditor editor(config);
        CHECK(editor.loadJson(encoded(styledDocument)));
        const Json baseline = exported(editor);
        CHECK(editor.renameNode(QStringLiteral("r"), QStringLiteral("History marker")));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Left));
        showEditor(editor);
        editor.resetZoom(); editor.zoom(0.85);
        CHECK(link ? editor.selectLink(QStringLiteral("l")) : editor.selectNode(QStringLiteral("a")));
        const Json before = exported(editor);
        const QString markdown = editor.toMarkdown();
        const QStringList nodes = editor.selectedNodeIds();
        const QString selectedLink = editor.selectedLinkId();
        shortcut(editor, Qt::Key_F2);
        QPointer<QPlainTextEdit> input = &topicInput(editor);
        const QString original = input->toPlainText();
        const QString draft = link ? QStringLiteral("Link draft #literal") : QStringLiteral("Node draft retained");
        input->selectAll(); input->insertPlainText(draft);
        auto cursor = input->textCursor();
        cursor.setPosition(2); cursor.setPosition(7, QTextCursor::KeepAnchor);
        input->setTextCursor(cursor);
        const QTransform zoom = graphics(editor).transform();
        const QPointF center = graphics(editor).mapToScene(graphics(editor).viewport()->rect().center());
        QSignalSpy changed(&editor, &Editor::documentChanged), pending(&editor, &Editor::pendingEditChanged);
        QSignalSpy selected(&editor, &Editor::selectionChanged), errors(&editor, &Editor::errorOccurred);
        const auto retained = [&] {
            CHECK(input && activeTopicInput(editor) == input.data() && input->hasFocus());
            CHECK(input->toPlainText() == draft && editor.hasPendingEdit());
            CHECK(input->textCursor().anchor() == 2 && input->textCursor().position() == 7);
            CHECK(editor.selectedText() == draft.mid(2, 5) && input->document()->isUndoAvailable());
            CHECK(exported(editor) == before && editor.toMarkdown() == markdown);
            CHECK(changed.isEmpty() && pending.isEmpty() && selected.isEmpty() && errors.isEmpty());
            CHECK(editor.canUndo() && !editor.canRedo());
            CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
            CHECK(editor.layoutDirection() == Editor::LayoutDirection::Left && graphics(editor).transform() == zoom);
            CHECK(QLineF(center, graphics(editor).mapToScene(graphics(editor).viewport()->rect().center())).length() <= 2.0 / zoom.m11());
            CHECK(textItem(editor, QStringLiteral("Pixel override"))->font().pixelSize() == 24);
        };
        for (int points : {18, 22}) {
            editor.setStyle({fallback, points});
            pump(); retained();
            CHECK(input->font().family() == family && input->font().pointSize() == points);
            CHECK(textItem(editor, QStringLiteral("History marker"))->font().pointSize() == points);
        }
        QFont hostFont = editor.font(); hostFont.setPointSize(10);
        editor.setFont(hostFont);
        QPalette palette = editor.palette(); palette.setColor(QPalette::Text, QColor(20, 60, 90));
        editor.setPalette(palette);
        pump(); retained();
        CHECK(input->font().pointSize() == 22 && input->palette() == graphics(editor).palette());
        topicKey(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(input && input->toPlainText() == original && !editor.hasPendingEdit());
        CHECK(exported(editor) == before && changed.isEmpty());
        topicKey(editor, Qt::Key_Y, Qt::ControlModifier);
        CHECK(input->toPlainText() == draft && editor.hasPendingEdit());
        topicKey(editor, Qt::Key_Escape);
        CHECK(editor.undo() && exported(editor) == baseline && !editor.canUndo());
    }
    {
        RawLabelEditor editor;
        showEditor(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        const Json before = exported(editor);
        editor.setAutoRandomBranchColor(false);
        editor.setConfirmSubtreeDeletion(false);
        CHECK(exported(editor) == before && changed.isEmpty());
        trigger(editor, "addChild");
        const QString plain = editor.selectedNodeId();
        topicKey(editor, Qt::Key_Escape);
        CHECK(!record(exported(editor), "nodes", plain).at("style").contains("branchColor"));
        CHECK(editor.selectNode(QStringLiteral("root")));
        editor.setAutoRandomBranchColor(true);
        trigger(editor, "addChild");
        const QString colored = editor.selectedNodeId();
        topicKey(editor, Qt::Key_Escape);
        const QString color = qs(record(exported(editor), "nodes", colored).at("style").at("branchColor"));
        QStringList palette;
        for (auto *button : editor.findChildren<QToolButton *>())
            if (button->property("color").isValid()) palette.append(button->property("color").toString());
        CHECK(QColor(color).isValid() && palette.contains(color));
        const Json withColor = exported(editor);
        const auto count = changed.size();
        editor.setAutoRandomBranchColor(false);
        editor.setConfirmSubtreeDeletion(true);
        CHECK(exported(editor) == withColor && changed.size() == count);
        const QString uncolored = editor.addNode(QStringLiteral("root"), QStringLiteral("Uncolored again"));
        CHECK(!uncolored.isEmpty() && !record(exported(editor), "nodes", uncolored).at("style").contains("branchColor"));
        CHECK(record(exported(editor), "nodes", colored) == record(withColor, "nodes", colored));
        const QString child = editor.addNode(plain, QStringLiteral("Descendant"));
        CHECK(!child.isEmpty() && editor.selectNode(plain));
        const Json subtree = exported(editor);
        const auto commands = changed.size();
        dialogs({deleteResponse(false)}, [&] { trigger(editor, "deleteSelection"); });
        CHECK(exported(editor) == subtree && changed.size() == commands);
        dialogs({deleteResponse(true)}, [&] { trigger(editor, "deleteSelection"); });
        CHECK(!hasRecord(exported(editor), "nodes", plain) && !hasRecord(exported(editor), "nodes", child));
        CHECK(changed.size() == commands + 1);
        dialogs({}, [&] { CHECK(editor.removeNode(uncolored)); });
        CHECK(!hasRecord(exported(editor), "nodes", uncolored));
        editor.setConfirmSubtreeDeletion(false);
        CHECK(editor.selectNode(colored));
        dialogs({}, [&] { trigger(editor, "deleteSelection"); });
        CHECK(!hasRecord(exported(editor), "nodes", colored));
    }
    {
        QTemporaryDir first, second;
        CHECK(first.isValid() && second.isValid());
        const QString imageReference = QString::fromUtf8("images/世界.png");
        const QString linkReference = QStringLiteral("docs/read me.txt#section");
        Json input = imageDocument(imageReference);
        for (auto &node : input.at("nodes")) if (node.at("id") == "r") node["hyperLink"] = utf8(linkReference);
        const Json literal = nativeDocument(input);
        auto fileUrl = [](const QString &path) { return QUrl::fromLocalFile(QDir::cleanPath(path)).toString(QUrl::FullyEncoded); };
        auto checkLink = [&](Editor &target, const QString &expected) {
            QSignalSpy activated(&target, &Editor::nodeLinkActivated);
            const QPoint point = nodeLinkPoint(target, QStringLiteral("Image root"));
            QTest::mouseClick(graphics(target).viewport(), Qt::LeftButton, Qt::NoModifier, point);
            pump();
            CHECK(activated.size() == 1 && activated.front().at(0).toString() == QStringLiteral("r"));
            CHECK(activated.front().at(1).toString() == expected);
        };
        RawLabelConfig firstConfig, secondConfig;
        firstConfig.resourceBasePath = first.path();
        secondConfig.resourceBasePath = second.path();
        RawLabelEditor left(firstConfig), right(secondConfig);
        QSignalSpy leftRequests(&left, &Editor::imageRequested), rightRequests(&right, &Editor::imageRequested);
        CHECK(left.loadJson(encoded(input)) && right.loadJson(encoded(input)));
        showEditor(left);
        showEditor(right);
        CHECK(QTest::qWaitFor([&] { return leftRequests.size() == 1 && rightRequests.size() == 1; }, 5000));
        CHECK(leftRequests.front().at(0).toString() == fileUrl(first.filePath(imageReference)));
        CHECK(rightRequests.front().at(0).toString() == fileUrl(second.filePath(imageReference)));
        checkLink(left, fileUrl(first.filePath(QStringLiteral("docs/read me.txt"))) + QStringLiteral("#section"));
        checkLink(right, fileUrl(second.filePath(QStringLiteral("docs/read me.txt"))) + QStringLiteral("#section"));
        CHECK(exported(left) == literal && exported(right) == literal);
        CHECK(left.toMarkdown() == right.toMarkdown());
        CHECK(HtmlPage(left.toHtml()).values(QStringLiteral("article")) == HtmlPage(right.toHtml()).values(QStringLiteral("article")));
        const HtmlPage unresolved(right.toHtml());
        const auto placeholder = unresolved.classes(QStringLiteral("m3-article-image-placeholder"), unresolved.article(QStringLiteral("r")));
        CHECK(placeholder.size() == 1);
        CHECK(unresolved.elements[placeholder.front()].attributes.value(QStringLiteral("data-image-state")) == QStringLiteral("loading"));
        CHECK(unresolved.classes(QStringLiteral("m3-article-image")).empty());
        CHECK(!unresolved.elements[unresolved.article(QStringLiteral("r"))].text.contains(imageReference));
        CHECK(!left.toMarkdown().contains(first.path()) && !left.toHtml().contains(first.path()));

        struct RestoreDirectory {
            QString path = QDir::currentPath();
            ~RestoreDirectory() { QDir::setCurrent(path); }
        } restore;
        CHECK(QDir::setCurrent(first.path()));
        RawLabelEditor captured;
        RawLabelConfig relativeConfig;
        relativeConfig.resourceBasePath = QStringLiteral("assets/../resources");
        RawLabelEditor relative(relativeConfig);
        const QString capturedBase = QDir::cleanPath(first.path());
        const QString relativeBase = QDir::cleanPath(first.filePath(QStringLiteral("resources")));
        CHECK(captured.resourceBasePath() == capturedBase && relative.resourceBasePath() == relativeBase);
        CHECK(QDir::setCurrent(second.path()));
        for (auto *target : {&captured, &relative}) {
            const QString base = target == &captured ? capturedBase : relativeBase;
            QSignalSpy requests(target, &Editor::imageRequested);
            CHECK(target->loadJson(encoded(input)));
            showEditor(*target);
            CHECK(QTest::qWaitFor([&] { return requests.size() == 1; }, 5000));
            const QString expected = fileUrl(QDir(base).filePath(imageReference));
            CHECK(requests.front().at(0).toString() == expected);
            const quint64 oldId = requests.front().at(1).toULongLong();
            target->reloadImages();
            CHECK(QTest::qWaitFor([&] { return requests.size() == 2; }, 5000));
            CHECK(requests.back().at(0).toString() == expected && requests.back().at(1).toULongLong() > oldId);
            checkLink(*target, fileUrl(QDir(base).filePath(QStringLiteral("docs/read me.txt"))) + QStringLiteral("#section"));
            CHECK(exported(*target) == literal && target->resourceBasePath() == base);
            CHECK(target->newDocument());
            CHECK(target->loadJson(encoded(input)) && target->resourceBasePath() == base);
        }
        // References are URLs: delimiters survive; literal filename delimiters are encoded.
        const QString absolute = first.filePath(QString::fromUtf8("absolute 世界.png"));
        const std::vector<std::pair<QString, QString>> references{
            {QString::fromUtf8("../images/世界.png"), fileUrl(first.filePath(QString::fromUtf8("../images/世界.png")))},
            {QString::fromUtf8("file:images/世界.png"), fileUrl(first.filePath(imageReference))},
            {QStringLiteral("images/a b.png?version=2#part"), fileUrl(first.filePath(QStringLiteral("images/a b.png"))) + QStringLiteral("?version=2#part")},
            {QStringLiteral("images/hash%23question%3Fpercent%25.png"), fileUrl(first.filePath(QStringLiteral("images/hash#question?percent%.png")))},
            {absolute, fileUrl(absolute)},
            {QUrl::fromLocalFile(absolute).toString(QUrl::FullyEncoded), fileUrl(absolute)},
            {QStringLiteral("https://example.invalid/a%20b.png?q=1#part"), QStringLiteral("https://example.invalid/a%20b.png?q=1#part")},
            {QStringLiteral("http://example.invalid/image.png"), QStringLiteral("http://example.invalid/image.png")},
            {QString::fromUtf8("opaque:世界/<literal>?q=1"), QString::fromUtf8("opaque:世界/<literal>?q=1")},
            {QStringLiteral("images/bad%escape.png"), QStringLiteral("images/bad%escape.png")}
        };
        for (const auto &reference : references) {
            Json document = imageDocument(reference.first);
            for (auto &node : document.at("nodes")) if (node.at("id") == "r") node["hyperLink"] = utf8(reference.first);
            QSignalSpy requests(&left, &Editor::imageRequested);
            CHECK(left.loadJson(encoded(document)));
            CHECK(QTest::qWaitFor([&] { return requests.size() == 1; }, 5000));
            CHECK(requests.front().at(0).toString() == reference.second);
            checkLink(left, reference.second);
            CHECK(exported(left) == nativeDocument(document));
        }
    }
    auto configured = [] {
        RawLabelConfig config;
        config.shortcuts.addChild = {QKeySequence(Qt::CTRL | Qt::Key_J), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_J)};
        config.shortcuts.editSelection = {QKeySequence(Qt::CTRL | Qt::Key_R)};
        config.shortcuts.acceptTopic = {QKeySequence(Qt::ALT | Qt::Key_Return)};
        config.shortcuts.deleteSelection = {QKeySequence(Qt::CTRL | Qt::Key_D)};
        config.shortcuts.toggleExpanded.clear();
        config.confirmSubtreeDeletion = false;
        return std::make_unique<RawLabelEditor>(config);
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
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(changed.size() == 5);
    trigger(editor, "toggleExpanded");
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(changed.size() == 5);
    CHECK(editor.selectNode(child));
    dialogs({}, [&] { shortcut(editor, Qt::Key_D, Qt::ControlModifier); });
    CHECK(!hasRecord(exported(editor), "nodes", child) && !hasRecord(exported(editor), "nodes", grandchild));
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && changed.size() == 6);
    // Deletion confirmation policy never permits removing the root.
    const Json retained = exported(editor);
    dialogs({}, [&] { shortcut(editor, Qt::Key_D, Qt::ControlModifier); });
    CHECK(exported(editor) == retained && changed.size() == 6);
    RawLabelEditor independent;
    CHECK(independent.loadJson(encoded(editorFixture())));
    showEditor(independent);
    dialogs({}, [&] { shortcut(independent, Qt::Key_J, Qt::ControlModifier); });
    CHECK(exported(independent) == before);
    shortcut(independent, Qt::Key_Insert);
    topicInput(independent).setPlainText(QStringLiteral("Default binding"));
    topicKey(independent, Qt::Key_Return);
    CHECK(record(exported(independent), "nodes", independent.selectedNodeId()).at("topic") == "Default binding");
    CHECK(exported(editor) == retained && changed.size() == 6);
}

static void node_shortcuts_case() {
    auto jsonNode = [](Json &doc, const char *id) -> Json & {
        for (auto &entry : doc.at("nodes")) if (entry.at("id") == id) return entry;
        throw std::runtime_error("Missing shortcut fixture node");
    };
    auto button = [](Editor &editor, const char *name) {
        auto *result = editor.findChild<QToolButton *>(QString::fromLatin1(name));
        CHECK(result != nullptr);
        return result;
    };
    auto focused = [](Editor &editor, const char *name) {
        auto *target = editor.findChild<QWidget *>(QString::fromLatin1(name));
        auto *scroll = editor.findChild<QScrollArea *>();
        CHECK(target && scroll && target->isVisible() && target->hasFocus());
        CHECK(scroll->viewport()->rect().contains(target->mapTo(scroll->viewport(), target->rect().center())));
    };
    const Qt::Key letters[] = {Qt::Key_B, Qt::Key_I, Qt::Key_R, Qt::Key_C, Qt::Key_F,
                               Qt::Key_T, Qt::Key_O, Qt::Key_N, Qt::Key_E, Qt::Key_P};
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor, QSize(760, 440));
        auto &view = graphics(editor);
        auto *toggle = button(editor, "nodePropertiesToggle");
        auto *scroll = editor.findChild<QScrollArea *>();
        CHECK(scroll != nullptr);
        toggle->setChecked(true);
        view.setFocus();
        Json expected = exported(editor);
        const auto zoom = view.transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        CHECK(panel != nullptr);
        const QRectF expandedPanel = QRectF(QRect(view.viewport()->mapFromGlobal(panel->mapToGlobal(QPoint())), panel->size()))
            .adjusted(-8, -8, 8, 8);
        // No shortcut() helper: the first P must leave focus ready for the second P.
        for (bool expanded : {false, true}) {
            const QPointF beforeCenter = view.mapToScene(view.viewport()->rect().center());
            auto *node = ownerItem(textItem(editor, QStringLiteral("Alpha")));
            QRectF envelope = view.viewportTransform().mapRect(node->sceneBoundingRect());
            for (auto *child : node->childItems()) {
                if (child->isVisible())
                    envelope = envelope.united(child->deviceTransform(view.viewportTransform()).mapRect(child->boundingRect()));
            }
            const bool occluded = envelope.intersects(expandedPanel);
            QTest::keyClick(view.viewport(), Qt::Key_P); pump();
            CHECK(toggle->isChecked() == expanded && scroll->isVisible() == expanded && view.hasFocus());
            CHECK(editor.selectedNodeId() == QStringLiteral("a") && selected.isEmpty());
            CHECK(exported(editor) == expected && changed.isEmpty() && view.transform() == zoom);
            if (!expanded || !occluded) CHECK(view.mapToScene(view.viewport()->rect().center()) == beforeCenter);
            else CHECK(!view.viewportTransform().mapRect(topicRect(editor, QStringLiteral("Alpha"))).intersects(expandedPanel));
        }
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_P, Qt::NoModifier, QStringLiteral("p"), true);
        QCoreApplication::sendEvent(view.viewport(), &repeat);
        CHECK(toggle->isChecked() && scroll->isVisible() && changed.isEmpty());
        shortcut(editor, Qt::Key_Question);
        auto *popup = editor.findChild<QDialog *>(QStringLiteral("shortcutHelpPopup"));
        CHECK(popup && popup->isVisible());
        auto *browser = popup->findChild<QTextBrowser *>();
        CHECK(browser && browser->hasFocus());
        CHECK(browser->toPlainText().split(QLatin1Char('\n')).contains(QKeySequence(Qt::Key_P).toString(QKeySequence::NativeText)));
        QTest::keyClick(browser, Qt::Key_P); pump();
        CHECK(toggle->isChecked() && popup->isVisible());
        QTest::keyClick(browser, Qt::Key_Escape); pump();
        shortcut(editor, Qt::Key_T);
        auto *tags = editor.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        CHECK(tags && tags->hasFocus());
        QTest::keyClicks(tags, "p"); pump();
        jsonNode(expected, "a")["tags"] = Json::array({"p"});
        CHECK(tags->hasFocus() && toggle->isChecked() && scroll->isVisible());
        CHECK(exported(editor) == expected && changed.size() == 1);
        shortcut(editor, Qt::Key_E);
        QTest::keyClicks(&topicInput(editor), "p"); pump();
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("p") && toggle->isChecked());
        CHECK(exported(editor) == expected && changed.size() == 1 && selected.isEmpty());
        topicKey(editor, Qt::Key_Escape);
    }
    {
        RawLabelEditor editor;
        Json input = editorFixture();
        jsonNode(input, "a")["style"] = {{"fontSize", 22}, {"fontWeight", "normal"},
            {"fontStyle", "normal"}, {"color", "#2980b9"}, {"background", "#ffffff"},
            {"opaque", {{"nested", Json::array({1, true, "keep"})}}}};
        jsonNode(input, "a")["tags"] = Json::array({"keep"});
        jsonNode(input, "a")["note"] = "Keep note";
        jsonNode(input, "a")["image"] = {{"url", "memory:keep"}, {"width", 40}, {"height", 20}};
        CHECK(editor.loadJson(encoded(input)) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor);
        auto *toggle = button(editor, "nodePropertiesToggle");
        toggle->setChecked(false);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        const auto zoom = graphics(editor).transform();
        auto unchangedSurface = [&] {
            CHECK(!toggle->isChecked() && graphics(editor).hasFocus());
            CHECK(editor.selectedNodeId() == QStringLiteral("a") && selected.isEmpty());
            CHECK(graphics(editor).transform() == zoom);
        };
        for (bool enabled : {true, false}) {
            const auto count = changed.size();
            shortcut(editor, Qt::Key_B);
            jsonNode(expected, "a")["style"]["fontWeight"] = enabled ? "bold" : "normal";
            CHECK(exported(editor) == expected && changed.size() == count + 1);
            CHECK(textItem(editor, QStringLiteral("Alpha"))->font().bold() == enabled);
            unchangedSurface();
        }
        for (bool enabled : {true, false}) {
            const auto count = changed.size();
            shortcut(editor, Qt::Key_I);
            jsonNode(expected, "a")["style"]["fontStyle"] = enabled ? "italic" : "normal";
            CHECK(exported(editor) == expected && changed.size() == count + 1);
            CHECK(textItem(editor, QStringLiteral("Alpha"))->font().italic() == enabled);
            unchangedSurface();
        }
        // Holding a toggle key must not repeatedly mutate the document.
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"), true);
        QCoreApplication::sendEvent(graphics(editor).viewport(), &repeat);
        CHECK(exported(editor) == expected && changed.size() == 4);
        shortcut(editor, Qt::Key_R);
        for (const char *key : {"fontSize", "fontWeight", "fontStyle", "color", "background"})
            jsonNode(expected, "a")["style"].erase(key);
        CHECK(exported(editor) == expected && changed.size() == 5);
        unchangedSurface();
        shortcut(editor, Qt::Key_R);
        CHECK(exported(editor) == expected && changed.size() == 5);
        unchangedSurface();
        CHECK(editor.selectNode(QStringLiteral("r")));
        const QString rootTopic = qs(record(expected, "nodes", QStringLiteral("r")).at("topic"));
        CHECK(textItem(editor, rootTopic)->font().bold());
        shortcut(editor, Qt::Key_B);
        jsonNode(expected, "r")["style"]["fontWeight"] = "normal";
        CHECK(!textItem(editor, rootTopic)->font().bold());
        CHECK(exported(editor) == expected && changed.size() == 6);
        shortcut(editor, Qt::Key_R);
        jsonNode(expected, "r")["style"].erase("fontWeight");
        CHECK(textItem(editor, rootTopic)->font().bold());
        CHECK(exported(editor) == expected && changed.size() == 7);
        QFont font = editor.font();
        font.setItalic(true);
        editor.setFont(font);
        pump();
        CHECK(textItem(editor, rootTopic)->font().italic());
        shortcut(editor, Qt::Key_I);
        jsonNode(expected, "r")["style"]["fontStyle"] = "normal";
        CHECK(!textItem(editor, rootTopic)->font().italic());
        CHECK(exported(editor) == expected && changed.size() == 8);
        shortcut(editor, Qt::Key_R);
        jsonNode(expected, "r")["style"].erase("fontStyle");
        CHECK(textItem(editor, rootTopic)->font().italic());
        CHECK(exported(editor) == expected && changed.size() == 9);
        CHECK(!toggle->isChecked() && graphics(editor).hasFocus());
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor, QSize(760, 440));
        auto *toggle = button(editor, "nodePropertiesToggle");
        auto *scroll = editor.findChild<QScrollArea *>();
        CHECK(scroll != nullptr);
        toggle->setChecked(false);
        Json expected = exported(editor);
        const auto zoom = graphics(editor).transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        for (const auto &mode : {std::make_pair(Qt::Key_C, "nodeTextColor"), std::make_pair(Qt::Key_F, "nodeFillColor")}) {
            toggle->setChecked(false);
            shortcut(editor, mode.first);
            focused(editor, mode.second);
            CHECK(toggle->isChecked() && button(editor, mode.second)->isChecked());
            CHECK(exported(editor) == expected);
            const auto count = changed.size();
            shortcut(editor, mode.first);
            focused(editor, mode.second);
            CHECK(button(editor, mode.second)->isChecked() && changed.size() == count);
            const QRectF rect = topicRect(editor, QStringLiteral("Alpha"));
            const QImage before = paintScene(editor, rect);
            QTest::keyClicks(button(editor, mode.second), "22");
            pump();
            jsonNode(expected, "a")["style"][mode.first == Qt::Key_C ? "color" : "background"] = "#e74c3c";
            CHECK(exported(editor) == expected && changed.size() == count + 1);
            CHECK(textItem(editor, QStringLiteral("Alpha"))->defaultTextColor() == QColor(QStringLiteral("#e74c3c")));
            if (mode.first == Qt::Key_F) {
                const QImage painted = paintScene(editor, rect);
                CHECK(painted != before);
                CHECK(painted.pixelColor(painted.width() / 2, painted.height() - 6) == QColor(QStringLiteral("#e74c3c")));
            }
            // An already-expanded card scrolled to Note must reveal the color mode again.
            shortcut(editor, Qt::Key_N);
            focused(editor, "nodeNote");
            CHECK(scroll->verticalScrollBar()->value() > 0);
            shortcut(editor, mode.first);
            focused(editor, mode.second);
            CHECK(button(editor, mode.second)->isChecked());
            CHECK(exported(editor) == expected && changed.size() == count + 1);
        }
        CHECK(changed.size() == 2 && selected.isEmpty());
        CHECK(editor.selectedNodeId() == QStringLiteral("a") && graphics(editor).transform() == zoom);
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor);
        expandProperties(editor);
        Json expected = exported(editor);
        auto &view = graphics(editor);
        const auto zoom = view.transform();
        QSignalSpy selected(&editor, &Editor::selectionChanged);
        auto type = [&](const char *digits) { QTest::keyClicks(QApplication::focusWidget(), digits); pump(); };

        // Unhandled digits bubble from the canvas, without focusing Text or Fill.
        view.setFocus();
        type("22");
        jsonNode(expected, "a")["style"]["color"] = "#e74c3c";
        CHECK(exported(editor) == expected && view.hasFocus());
        type("11");
        jsonNode(expected, "a")["style"].erase("color");
        CHECK(exported(editor) == expected && view.hasFocus());

        // Other panel buttons also leave numeric input to the editor fallback.
        button(editor, "nodeFillColor")->click();
        button(editor, "nodeBold")->setFocus();
        type("46");
        jsonNode(expected, "a")["style"]["background"] = "#a9d6f5";
        CHECK(exported(editor) == expected && button(editor, "nodeBold")->hasFocus());
        type("11");
        jsonNode(expected, "a")["style"].erase("background");
        CHECK(exported(editor) == expected);
        view.setFocus();
        type("2");
        button(editor, "nodeBold")->setFocus();
        type("6");
        CHECK(exported(editor) == expected);
        CHECK(selected.isEmpty() && view.transform() == zoom);

        // A real input consumes digits instead of passing them to swatch selection.
        auto *note = editor.findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
        CHECK(note);
        note->setFocus();
        note->selectAll();
        type("1146");
        jsonNode(expected, "a")["note"] = "1146";
        CHECK(exported(editor) == expected);

        // Hidden or collapsed properties must not change color from the canvas.
        button(editor, "nodePropertiesToggle")->setChecked(false);
        view.setFocus();
        type("22");
        CHECK(exported(editor) == expected);
        button(editor, "nodePropertiesToggle")->setChecked(true);
        editor.clearSelection();
        view.setFocus();
        type("22");
        CHECK(exported(editor) == expected && editor.selectedNodeId().isEmpty());
        CHECK(editor.selectLink(QStringLiteral("l1")));
        type("22");
        CHECK(exported(editor) == expected && editor.selectedLinkId() == QStringLiteral("l1"));
    }
    {
        RawLabelConfig config;
        config.shortcuts.zoomIn = {QKeySequence(Qt::Key_2)};
        RawLabelEditor editor(config);
        showEditor(editor);
        expandProperties(editor);
        shortcut(editor, Qt::Key_0, Qt::ControlModifier);
        const Json expected = exported(editor);
        auto &view = graphics(editor);
        const auto zoom = view.transform();
        view.setFocus();
        QTest::keyClicks(view.viewport(), "22");
        CHECK(view.transform().m11() > zoom.m11() && exported(editor) == expected);
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        auto type = [&](const char *digits) { QTest::keyClicks(QApplication::focusWidget(), digits); pump(); };
        shortcut(editor, Qt::Key_C);
        type("1");
        QKeyEvent repeat(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier, QStringLiteral("1"), true);
        QCoreApplication::sendEvent(QApplication::focusWidget(), &repeat);
        CHECK(exported(editor) == expected && changed.isEmpty());
        type("2");
        jsonNode(expected, "a")["style"]["color"] = "#ffffff";
        CHECK(exported(editor) == expected && changed.size() == 1);
        type("46");
        jsonNode(expected, "a")["style"]["color"] = "#a9d6f5";
        CHECK(exported(editor) == expected && changed.size() == 2);
        type("11");
        jsonNode(expected, "a")["style"].erase("color");
        CHECK(exported(editor) == expected && changed.size() == 3);
        type("11");
        CHECK(exported(editor) == expected && changed.size() == 3);
        shortcut(editor, Qt::Key_F);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_3, Qt::KeypadModifier);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_1, Qt::KeypadModifier);
        jsonNode(expected, "a")["style"]["background"] = "#27ae60";
        CHECK(exported(editor) == expected && changed.size() == 4);
        type("11");
        jsonNode(expected, "a")["style"].erase("background");
        CHECK(exported(editor) == expected && changed.size() == 5);

        // Invalid digits, modified keys and unrelated keys discard incomplete codes.
        type("09529");
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 5);
        type("2");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_X);
        type("6");
        type("2");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_1, Qt::ControlModifier);
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 5);

        // Focus and mouse input must not carry a row into a different action.
        type("2");
        button(editor, "nodeDefaultColor")->setFocus();
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 5);
        type("2");
        QTest::mouseClick(button(editor, "nodeDefaultColor"), Qt::LeftButton);
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 5);
        type("2");
        button(editor, "nodeTextColor")->setChecked(true);
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 5);

        // Selection changes and same-ID document replacement cancel pending input.
        type("2");
        CHECK(editor.selectNode(QStringLiteral("b")));
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 5);
        type("2");
        CHECK(editor.loadJson(encoded(expected)));
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 6);
        CHECK(editor.selectNode(QStringLiteral("a")));
        shortcut(editor, Qt::Key_C);
        type("2");
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
        CHECK(!button(editor, "nodePropertiesToggle")->isChecked() && graphics(editor).hasFocus());
        shortcut(editor, Qt::Key_C);
        type("6");
        CHECK(exported(editor) == expected && changed.size() == 6);
        button(editor, "nodeColor_e74c3c")->setFocus();
        type("22");
        jsonNode(expected, "a")["style"]["color"] = "#e74c3c";
        CHECK(exported(editor) == expected && changed.size() == 7);
        type("22");
        CHECK(exported(editor) == expected && changed.size() == 7);

        // Numeric codes are plain text outside color controls, even after a prefix.
        type("2");
        shortcut(editor, Qt::Key_T);
        auto *tags = editor.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        CHECK(tags);
        tags->selectAll();
        type("1146");
        jsonNode(expected, "a")["tags"] = Json::array({"1146"});
        CHECK(exported(editor) == expected);
        shortcut(editor, Qt::Key_E);
        QTest::keyClicks(&topicInput(editor), "1146");
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("1146"));
        CHECK(exported(editor) == expected);
        topicKey(editor, Qt::Key_Escape);
    }
    {
        RawLabelEditor editor;
        Json input = editorFixture();
        jsonNode(input, "a")["tags"] = Json::array({"seed"});
        jsonNode(input, "a")["icons"] = Json::array({"seed"});
        jsonNode(input, "a")["note"] = "seed";
        CHECK(editor.loadJson(encoded(input)) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor, QSize(760, 440));
        auto *toggle = button(editor, "nodePropertiesToggle");
        auto *tags = editor.findChild<QLineEdit *>(QStringLiteral("nodeTags"));
        auto *icons = editor.findChild<QLineEdit *>(QStringLiteral("nodeIcons"));
        auto *note = editor.findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
        CHECK(tags && icons && note);
        tags->setCursorPosition(2); icons->setCursorPosition(2);
        auto cursor = note->textCursor(); cursor.setPosition(2); note->setTextCursor(cursor);
        Json expected = exported(editor);
        const auto direction = editor.layoutDirection();
        const auto zoom = graphics(editor).transform();
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        for (const auto &entry : {std::make_pair(Qt::Key_T, "nodeTags"), std::make_pair(Qt::Key_O, "nodeIcons"),
                                  std::make_pair(Qt::Key_N, "nodeNote")}) {
            toggle->setChecked(false);
            shortcut(editor, entry.first);
            focused(editor, entry.second);
            CHECK(toggle->isChecked() && exported(editor) == expected && changed.isEmpty());
            CHECK(selected.isEmpty() && editor.layoutDirection() == direction && graphics(editor).transform() == zoom);
            if (entry.first == Qt::Key_T) CHECK(tags->cursorPosition() == 2 && !tags->hasSelectedText());
            if (entry.first == Qt::Key_N) CHECK(note->textCursor().position() == 2 && !note->textCursor().hasSelection());
            if (entry.first == Qt::Key_O) {
                CHECK(icons->cursorPosition() == 2 && !icons->hasSelectedText());
                auto *popup = editor.findChild<QWidget *>(QStringLiteral("emojiPopup"));
                CHECK(popup && popup->isVisible());
                QTest::keyClick(icons, Qt::Key_Escape); pump();
                CHECK(!popup->isVisible() && icons->hasFocus() && toggle->isChecked());
                QTest::keyClick(icons, Qt::Key_Escape); pump();
                CHECK(!toggle->isChecked() && graphics(editor).hasFocus());
            }
        }
        QTest::keyClicks(note, "b"); pump();
        jsonNode(expected, "a")["note"] = "sebed";
        CHECK(note->hasFocus() && exported(editor) == expected && changed.size() == 1);
        shortcut(editor, Qt::Key_T);
        QTest::keyClicks(tags, "i"); pump();
        jsonNode(expected, "a")["tags"] = Json::array({"seied"});
        CHECK(tags->hasFocus() && exported(editor) == expected && changed.size() == 2);
        CHECK(selected.isEmpty() && editor.layoutDirection() == direction && graphics(editor).transform() == zoom);
    }
    {
        RawLabelConfig config;
        config.shortcuts.acceptTopic = {QKeySequence(Qt::ALT | Qt::Key_Return)};
        RawLabelEditor editor(config);
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor);
        button(editor, "nodePropertiesToggle")->setChecked(false);
        Json expected = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        const auto zoom = graphics(editor).transform();
        shortcut(editor, Qt::Key_E);
        CHECK(topicInput(editor).textCursor().selectedText() == QStringLiteral("Alpha"));
        QTest::keyClicks(&topicInput(editor), "bircftone"); pump();
        CHECK(topicInput(editor).toPlainText() == QStringLiteral("bircftone"));
        CHECK(exported(editor) == expected && changed.isEmpty());
        CHECK(!button(editor, "nodePropertiesToggle")->isChecked());
        topicKey(editor, Qt::Key_Escape);
        CHECK(!activeTopicInput(editor) && exported(editor) == expected && changed.isEmpty());
        shortcut(editor, Qt::Key_E);
        QTest::keyClicks(&topicInput(editor), "New topic");
        topicKey(editor, Qt::Key_Return, Qt::AltModifier);
        jsonNode(expected, "a")["topic"] = "New topic";
        CHECK(!activeTopicInput(editor) && exported(editor) == expected && changed.size() == 1);
        shortcut(editor, Qt::Key_F2);
        CHECK(topicInput(editor).textCursor().selectedText() == QStringLiteral("New topic"));
        topicKey(editor, Qt::Key_Escape);
        CHECK(selected.isEmpty() && graphics(editor).transform() == zoom);
        CHECK(editor.selectLink(QStringLiteral("l1")));
        dialogs({}, [&] { shortcut(editor, Qt::Key_E); });
        CHECK(!activeTopicInput(editor) && exported(editor) == expected && changed.size() == 1);
        shortcut(editor, Qt::Key_F2);
        auto &input = topicInput(editor);
        CHECK(input.toPlainText() == QStringLiteral("Related"));
        input.selectAll(); QTest::keyClicks(&input, "bircftone");
        CHECK(input.hasFocus() && input.toPlainText() == QStringLiteral("bircftone"));
        CHECK(exported(editor) == expected);
        topicKey(editor, Qt::Key_Escape);
        CHECK(exported(editor) == expected && changed.size() == 1);
    }
    {
        RawLabelEditor editor, independent;
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        CHECK(independent.loadJson(encoded(editorFixture())) && independent.selectNode(QStringLiteral("a")));
        showEditor(independent); showEditor(editor);
        const Json other = exported(independent);
        QSignalSpy otherChanged(&independent, &Editor::documentChanged);
        auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        CHECK(panel != nullptr);
        button(editor, "nodePropertiesToggle")->setChecked(false);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        for (bool link : {false, true}) {
            if (link) CHECK(editor.selectLink(QStringLiteral("l1")));
            else editor.clearSelection();
            QSignalSpy selected(&editor, &Editor::selectionChanged);
            dialogs({}, [&] { for (auto key : letters) shortcut(editor, key); });
            CHECK(!panel->isVisible() && !activeTopicInput(editor));
            CHECK(exported(editor) == before && changed.isEmpty() && errors.isEmpty() && selected.isEmpty());
            CHECK(editor.selectedNodeId().isEmpty());
            CHECK(editor.selectedLinkId() == (link ? QStringLiteral("l1") : QString()));
        }
        CHECK(editor.selectNode(QStringLiteral("a")));
        shortcut(editor, Qt::Key_B);
        CHECK(textItem(editor, QStringLiteral("Alpha"))->font().bold());
        CHECK(exported(independent) == other && otherChanged.isEmpty());
        CHECK(!activeTopicInput(independent));
        Json expected = exported(editor);
        changed.clear();
        QSignalSpy selected(&editor, &Editor::selectionChanged);
        shortcut(editor, Qt::Key_T);
        auto *scroll = editor.findChild<QScrollArea *>();
        CHECK(scroll != nullptr);
        struct Field { const char *widget; const char *key; };
        for (const auto &field : {Field{"nodeTags", "tags"}, Field{"nodeIcons", "icons"},
                                  Field{"nodeUrl", "hyperLink"}, Field{"nodeImageUrl", "image"}}) {
            auto *input = editor.findChild<QLineEdit *>(QString::fromLatin1(field.widget));
            CHECK(input != nullptr);
            scroll->ensureWidgetVisible(input); input->setFocus(Qt::OtherFocusReason); pump();
            focused(editor, field.widget);
            const auto count = changed.size();
            QTest::keyClicks(input, "bircftone"); pump();
            CHECK(input->hasFocus() && input->text() == QStringLiteral("bircftone"));
            if (std::string(field.key) == "image")
                jsonNode(expected, "a")[field.key] = {{"url", "bircftone"}, {"width", 0}, {"height", 0}};
            else if (std::string(field.key) == "tags" || std::string(field.key) == "icons")
                jsonNode(expected, "a")[field.key] = Json::array({"bircftone"});
            else jsonNode(expected, "a")[field.key] = "bircftone";
            CHECK(exported(editor) == expected && changed.size() == count + 9);
            CHECK(!activeTopicInput(editor) && selected.isEmpty());
        }
        shortcut(editor, Qt::Key_N);
        auto *note = editor.findChild<QPlainTextEdit *>(QStringLiteral("nodeNote"));
        CHECK(note != nullptr);
        const auto count = changed.size();
        QTest::keyClicks(note, "bircftone"); pump();
        jsonNode(expected, "a")["note"] = "bircftone";
        CHECK(note->hasFocus() && exported(editor) == expected && changed.size() == count + 9);
        dialogs({[&](QDialog *dialog) {
            auto *parent = dialog->findChild<QComboBox *>(QStringLiteral("newParent"));
            CHECK(parent != nullptr);
            dialog->activateWindow(); parent->setFocus();
            CHECK(QTest::qWaitFor([&] { return parent->hasFocus(); }, 5000));
            QTest::keyClick(parent, Qt::Key_B);
            CHECK(parent->hasFocus() && parent->currentData().toString() == QStringLiteral("b"));
            CHECK(exported(editor) == expected);
            dialog->reject();
        }}, [&] { shortcut(editor, Qt::Key_M, Qt::ControlModifier); });
        CHECK(exported(editor) == expected && changed.size() == count + 9 && errors.isEmpty() && selected.isEmpty());
        CHECK(exported(independent) == other && otherChanged.isEmpty());
    }
    {
        RawLabelConfig config;
        config.shortcuts.toggleBold = {QKeySequence(Qt::CTRL | Qt::Key_B)};
        config.shortcuts.editTopic = {QKeySequence(Qt::CTRL | Qt::Key_E)};
        config.shortcuts.editNote = {QKeySequence(Qt::CTRL | Qt::Key_N)};
        config.shortcuts.toggleProperties = {QKeySequence(Qt::CTRL | Qt::Key_P)};
        config.shortcuts.toggleItalic.clear();
        RawLabelEditor editor(config), defaults;
        CHECK(editor.loadJson(encoded(editorFixture())) && editor.selectNode(QStringLiteral("a")));
        CHECK(defaults.loadJson(encoded(editorFixture())) && defaults.selectNode(QStringLiteral("a")));
        showEditor(defaults); showEditor(editor);
        auto *toggle = button(editor, "nodePropertiesToggle");
        toggle->setChecked(false);
        Json expected = exported(editor);
        const Json other = exported(defaults);
        QSignalSpy changed(&editor, &Editor::documentChanged), otherChanged(&defaults, &Editor::documentChanged);
        for (auto key : {Qt::Key_B, Qt::Key_E, Qt::Key_N, Qt::Key_I, Qt::Key_P}) shortcut(editor, key);
        CHECK(exported(editor) == expected && changed.isEmpty());
        CHECK(!toggle->isChecked() && !activeTopicInput(editor) && graphics(editor).hasFocus());
        for (bool expanded : {true, false}) {
            shortcut(editor, Qt::Key_P, Qt::ControlModifier);
            CHECK(toggle->isChecked() == expanded && graphics(editor).hasFocus());
        }
        CHECK(exported(editor) == expected && changed.isEmpty());
        shortcut(editor, Qt::Key_B, Qt::ControlModifier);
        jsonNode(expected, "a")["style"]["fontWeight"] = "bold";
        CHECK(exported(editor) == expected && changed.size() == 1 && textItem(editor, QStringLiteral("Alpha"))->font().bold());
        shortcut(editor, Qt::Key_N, Qt::ControlModifier);
        focused(editor, "nodeNote");
        shortcut(editor, Qt::Key_E, Qt::ControlModifier);
        CHECK(topicInput(editor).textCursor().selectedText() == QStringLiteral("Alpha"));
        topicKey(editor, Qt::Key_Escape);
        shortcut(editor, Qt::Key_F2);
        CHECK(topicInput(editor).textCursor().selectedText() == QStringLiteral("Alpha"));
        topicKey(editor, Qt::Key_Escape);
        CHECK(exported(editor) == expected && changed.size() == 1);
        CHECK(exported(defaults) == other && otherChanged.isEmpty());
        shortcut(defaults, Qt::Key_B);
        shortcut(defaults, Qt::Key_I);
        CHECK(textItem(defaults, QStringLiteral("Alpha"))->font().bold());
        CHECK(textItem(defaults, QStringLiteral("Alpha"))->font().italic());
        shortcut(defaults, Qt::Key_N); focused(defaults, "nodeNote");
        shortcut(defaults, Qt::Key_E);
        CHECK(topicInput(defaults).textCursor().selectedText() == QStringLiteral("Alpha"));
        topicKey(defaults, Qt::Key_Escape);
        CHECK(otherChanged.size() == 2 && exported(editor) == expected && changed.size() == 1);
    }
    {
        RawLabelConfig config;
        config.shortcuts.resetStyle.clear(); config.shortcuts.textColor.clear(); config.shortcuts.fillColor.clear();
        config.shortcuts.editTags.clear(); config.shortcuts.editIcons.clear(); config.shortcuts.toggleProperties.clear();
        RawLabelEditor editor(config);
        Json input = editorFixture();
        jsonNode(input, "a")["style"] = {{"fontWeight", "bold"}, {"color", "#2980b9"}};
        CHECK(editor.loadJson(encoded(input)) && editor.selectNode(QStringLiteral("a")));
        showEditor(editor);
        auto *toggle = button(editor, "nodePropertiesToggle"); toggle->setChecked(false);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        for (auto key : {Qt::Key_R, Qt::Key_C, Qt::Key_F, Qt::Key_T, Qt::Key_O, Qt::Key_P}) {
            shortcut(editor, key);
            CHECK(exported(editor) == before && changed.isEmpty());
            CHECK(!toggle->isChecked() && graphics(editor).hasFocus() && !activeTopicInput(editor));
        }
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        Json replacement = exported(editor);
        jsonNode(replacement, "r")["style"] = {{"fontWeight", "bold"}, {"fontStyle", "italic"},
            {"fontSize", 24}, {"color", "#2980b9"}, {"background", "#ffffff"}, {"opaque", Json::array({true, 4})}};
        bool replaced = false;
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QObject::connect(&editor, &Editor::documentChanged, &editor, [&] {
            if (replaced) return;
            replaced = true;
            CHECK(editor.loadJson(encoded(replacement)));
        });
        shortcut(editor, Qt::Key_B);
        CHECK(replaced && exported(editor) == replacement && changed.size() == 2);
        const QString topic = qs(record(replacement, "nodes", QStringLiteral("r")).at("topic"));
        CHECK(textItem(editor, topic)->font().italic() && textItem(editor, topic)->font().bold());
        shortcut(editor, Qt::Key_I);
        jsonNode(replacement, "r")["style"]["fontStyle"] = "normal";
        CHECK(exported(editor) == replacement && changed.size() == 3);
        CHECK(!textItem(editor, topic)->font().italic());
        shortcut(editor, Qt::Key_R);
        for (const char *key : {"fontSize", "fontWeight", "fontStyle", "color", "background"})
            jsonNode(replacement, "r")["style"].erase(key);
        CHECK(exported(editor) == replacement && changed.size() == 4);
        CHECK(textItem(editor, topic)->font().bold());
    }
    {
        QPointer<Editor> editor = new RawLabelEditor;
        CHECK(editor->loadJson(encoded(editorFixture())) && editor->selectNode(QStringLiteral("a")));
        showEditor(*editor);
        auto *viewport = graphics(*editor).viewport();
        QObject::connect(editor, &Editor::documentChanged, qApp, [&] { delete editor.data(); });
        QKeyEvent press(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        QCoreApplication::sendEvent(viewport, &press);
        CHECK(editor.isNull());
        pump();
    }
    {
        QPointer<Editor> editor = new RawLabelEditor;
        showEditor(*editor);
        shortcut(*editor, Qt::Key_F);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_2);
        QObject::connect(editor, &Editor::documentChanged, qApp, [&] { delete editor.data(); });
        QKeyEvent press(QEvent::KeyPress, Qt::Key_2, Qt::NoModifier, QStringLiteral("2"));
        QCoreApplication::sendEvent(QApplication::focusWidget(), &press);
        CHECK(editor.isNull());
        pump();
    }
}

static void shortcuts_case() {
    RawLabelEditor editor;
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
    CHECK(changed.size() == 8);
    shortcut(editor, Qt::Key_Right);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.lastError().isEmpty());
    shortcut(editor, Qt::Key_Space);
    CHECK(exported(editor) == inserted && changed.size() == 8);
    CHECK(editor.selectLink(QStringLiteral("l1")));
    shortcut(editor, Qt::Key_Home);
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && editor.selectedLinkId().isEmpty());
    shortcut(editor, Qt::Key_Escape);
    CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
    dialogs({}, [&] { shortcut(editor, Qt::Key_Return); shortcut(editor, Qt::Key_Tab); });
    CHECK(exported(editor) == inserted && changed.size() == 8);
    CHECK(editor.selectNode(QStringLiteral("a")));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    CHECK(editor.layoutDirection() == Editor::LayoutDirection::Right);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && changed.size() == 8);
    auto &view = graphics(editor);
    shortcut(editor, Qt::Key_0, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 1.0) < 1e-6);
    shortcut(editor, Qt::Key_Equal, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 1.2) < 1e-6);
    shortcut(editor, Qt::Key_Minus, Qt::ControlModifier);
    CHECK(std::abs(view.transform().m11() - 1.0) < 1e-6);
    CHECK(exported(editor) == inserted && changed.size() == 8);
}

static void controls_case() {
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
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
        }
        assertTreeConnectors(editor, rectangles);
        return rectangles;
    };
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Outline));
    const auto outlineNodes = checkOutline(initialRows);
    const auto widths = std::minmax_element(outlineNodes.begin(), outlineNodes.end(),
        [](const QRectF &a, const QRectF &b) { return a.width() < b.width(); });
    CHECK(widths.second->width() > widths.first->width() + 1);
    CHECK(editor.selectedNodeId() == QStringLiteral("a") && editor.selectedLinkId().isEmpty());
    CHECK(exported(editor) == beforeLayout && changed.isEmpty() && layoutSelection.isEmpty());
    for (auto direction : {Editor::LayoutDirection::Left, Editor::LayoutDirection::Right,
                           Editor::LayoutDirection::Balanced}) {
        CHECK(editor.setLayoutDirection(direction));
        CHECK(editor.layoutDirection() == direction);
        if (direction == Editor::LayoutDirection::Left)
            CHECK(topicRect(editor, QStringLiteral("Alpha")).center().x() < topicRect(editor, rootTopic).center().x());
        if (direction == Editor::LayoutDirection::Right)
            CHECK(topicRect(editor, QStringLiteral("Alpha")).center().x() > topicRect(editor, rootTopic).center().x());
        assertTreeConnectors(editor, {topicRect(editor, rootTopic), topicRect(editor, QStringLiteral("Alpha")),
                                     topicRect(editor, QStringLiteral("Delta")), topicRect(editor, QStringLiteral("Beta")),
                                     emptyNodeRect(editor)});
    }
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Outline));
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
    CHECK(texts(editor, QStringLiteral("Double click topic")).isEmpty());
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
    const auto foldedNodes = checkOutline({{rootTopic, 0}, {QStringLiteral("Alpha"), 1},
                                          {QStringLiteral("Beta"), 1}, {QString(), 1}});
    CHECK(foldedNodes == std::vector<QRectF>({expandedNodes[0], expandedNodes[1], expandedNodes[4], expandedNodes[5]}));
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    shortcut(editor, Qt::Key_Space);
    CHECK(!texts(editor, QStringLiteral("Double click topic")).isEmpty());
    CHECK(checkOutline(expandedRows) == expandedNodes);
    CHECK(editor.selectedNodeId() == QStringLiteral("a"));
    CHECK(changed.size() == 4);

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
    CHECK(editor.selectedNodeId() == QStringLiteral("d") && changed.size() == 5);
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
    CHECK(texts(editor, QStringLiteral("Double click topic")).isEmpty());
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
    CHECK(topicInput(editor).toPlainText() == QStringLiteral("UI self link"));
    topicKey(editor, Qt::Key_Escape);
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QStringLiteral("Canceled link"));
    topicKey(editor, Qt::Key_Escape);
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
    }}, [&] { trigger(editor, "editLink"); });
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

    RawLabelEditor other;
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

    RawLabelEditor collapsed;
    CHECK(collapsed.loadJson(encoded(editorFixture())));
    showEditor(collapsed);
    CHECK(collapsed.selectNode(QStringLiteral("a")));
    trigger(collapsed, "toggleExpanded");
    QSignalSpy collapsedChanges(&collapsed, &Editor::documentChanged);
    shortcut(collapsed, Qt::Key_Insert);
    const QString newChild = collapsed.selectedNodeId();
    CHECK(record(exported(collapsed), "nodes", newChild).at("topic") == "" && collapsedChanges.size() == 1);
    topicInput(collapsed).setPlainText(QStringLiteral("Added while collapsed"));
    topicKey(collapsed, Qt::Key_Return);
    const Json collapsedDoc = exported(collapsed);
    CHECK(record(collapsedDoc, "nodes", QStringLiteral("a")).at("children").back() == utf8(newChild));
    CHECK(record(collapsedDoc, "nodes", newChild).at("topic") == "Added while collapsed");
    CHECK(collapsed.selectedNodeId() == newChild && collapsedChanges.size() == 2);
    CHECK(!texts(collapsed, QStringLiteral("Added while collapsed")).isEmpty());
    QTest::mouseClick(graphics(collapsed).viewport(), Qt::LeftButton, Qt::NoModifier,
                      expansionPoint(collapsed, QStringLiteral("Alpha")));
    pump();
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

    RawLabelEditor canceled;
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
static void demo_controls_case() {
    RawLabelConfig config;
    config.confirmSubtreeDeletion = false;
    DemoWindow window(config);
    auto &editor = embedded(window);
    CHECK(editor.loadJson(encoded(editorFixture())));
    window.setWindowModified(false);
    showDemo(window);
    QApplication::setActiveWindow(&window);
    auto &view = graphics(editor);
    view.setFocus(); pump();
    auto *editing = window.findChild<QToolBar *>(QStringLiteral("mindMapEditToolBar"));
    auto *viewBar = window.findChild<QToolBar *>(QStringLiteral("mindMapViewToolBar"));
    auto *picker = window.findChild<QComboBox *>(QStringLiteral("layoutDirection"));
    CHECK(editing && viewBar && picker && editor.findChildren<QToolBar *>().isEmpty());
    CHECK(!editor.isAncestorOf(editing) && !editor.isAncestorOf(viewBar));
    const auto button = [&](const char *name) {
        auto *command = editor.commandAction(QString::fromLatin1(name));
        CHECK(command);
        QWidget *widget = editing->widgetForAction(command);
        if (!widget) widget = viewBar->widgetForAction(command);
        auto *result = qobject_cast<QToolButton *>(widget);
        CHECK(result);
        return result;
    };
    for (const char *name : {"undo", "redo", "addChild", "addSibling", "addSiblingBefore",
            "editSelection", "deleteSelection", "toggleExpanded", "moveNode", "moveUp",
            "moveDown", "addLink", "editLink", "zoomIn", "zoomOut", "resetZoom", "fitWidth", "fit", "selectRoot"})
        button(name);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QSignalSpy layouts(&editor, &Editor::layoutDirectionChanged);
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    CHECK(picker->currentData().toInt() == int(Editor::LayoutDirection::Right) && layouts.size() == 1);
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right) && layouts.size() == 1);
    CHECK(!editor.setLayoutDirection(static_cast<Editor::LayoutDirection>(91)));
    CHECK(picker->currentData().toInt() == int(Editor::LayoutDirection::Right) && layouts.size() == 1);
    CHECK(changed.isEmpty());

    CHECK(editor.selectNode(QStringLiteral("a")));
    trigger(editor, "editSelection");
    const QString rejected = QStringLiteral("Rejected") + QChar(0) + QStringLiteral("draft");
    topicInput(editor).setPlainText(rejected);
    const auto before = exported(editor);
    QSignalSpy errors(&editor, &Editor::errorOccurred);
    picker->setCurrentIndex(picker->findData(int(Editor::LayoutDirection::Left)));
    CHECK(errors.size() == 1 && changed.isEmpty() && layouts.size() == 1);
    CHECK(picker->currentData().toInt() == int(Editor::LayoutDirection::Right));
    CHECK(exported(editor) == before && topicInput(editor).toPlainText() == rejected);
    topicInput(editor).setPlainText(QStringLiteral("Accepted by demo picker"));
    picker->setCurrentIndex(picker->findData(int(Editor::LayoutDirection::Left)));
    CHECK(editor.layoutDirection() == Editor::LayoutDirection::Left && layouts.size() == 2);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Accepted by demo picker");
    CHECK(changed.size() == 1 && !activeTopicInput(editor) && window.isWindowModified());
    const auto accepted = exported(editor);
    trigger(editor, "editSelection");
    topicInput(editor).setPlainText(QStringLiteral("Uncommitted host draft"));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Outline));
    CHECK(picker->currentData().toInt() == int(Editor::LayoutDirection::Outline));
    CHECK(topicInput(editor).toPlainText() == QStringLiteral("Uncommitted host draft"));
    CHECK(exported(editor) == accepted && changed.size() == 1);
    topicKey(editor, Qt::Key_Escape);

    trigger(editor, "editSelection");
    topicInput(editor).setPlainText(QStringLiteral("Accepted by demo zoom"));
    const auto scale = view.transform().m11();
    QTest::mouseClick(button("zoomIn"), Qt::LeftButton); pump();
    CHECK(view.transform().m11() > scale);
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Accepted by demo zoom");
    CHECK(!activeTopicInput(editor) && button("undo")->isEnabled());
    QTest::mouseClick(button("undo"), Qt::LeftButton); pump();
    CHECK(exported(editor) == accepted && button("redo")->isEnabled());
    QTest::mouseClick(button("redo"), Qt::LeftButton); pump();
    CHECK(record(exported(editor), "nodes", QStringLiteral("a")).at("topic") == "Accepted by demo zoom");

    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Balanced));
    const auto beforePickerKeys = exported(editor);
    picker->setFocus(); pump();
    QTest::keyClick(picker, Qt::Key_Down); QTest::keyClick(picker, Qt::Key_Tab); pump();
    CHECK(editor.layoutDirection() == Editor::LayoutDirection::Right);
    picker->setFocus(); QTest::keyClick(picker, Qt::Key_O); pump();
    CHECK(picker->hasFocus() && editor.layoutDirection() == Editor::LayoutDirection::Outline);
    CHECK(exported(editor) == beforePickerKeys && !activeTopicInput(editor));

    editor.setReadOnly(true);
    CHECK(!button("addChild")->isEnabled() && !button("undo")->isEnabled() && !button("redo")->isEnabled());
    QTest::mouseClick(button("addChild"), Qt::LeftButton); pump();
    CHECK(exported(editor) == beforePickerKeys);
    picker->setCurrentIndex(picker->findData(int(Editor::LayoutDirection::Left)));
    CHECK(editor.layoutDirection() == Editor::LayoutDirection::Left && exported(editor) == beforePickerKeys);
    editor.clearSelection(); picker->setFocus();
    const auto zoom = view.transform();
    QTest::mouseClick(button("selectRoot"), Qt::LeftButton); pump();
    CHECK(editor.selectedNodeId() == QStringLiteral("r") && view.hasFocus() && view.transform() == zoom);
    const auto rootTopic = qs(record(beforePickerKeys, "nodes", QStringLiteral("r")).at("topic"));
    CHECK(QLineF(view.mapFromScene(topicRect(editor, rootTopic).center()), view.viewport()->rect().center()).length() <= 2);
    editor.setReadOnly(false);

    CHECK(editor.selectNode(QStringLiteral("a")));
    trigger(editor, "showHelp");
    QPointer<QDialog> help = editor.findChild<QDialog *>(QStringLiteral("shortcutHelpPopup"));
    CHECK(help && help->isVisible());
    const QPoint point = button("addChild")->mapTo(&window, button("addChild")->rect().center());
    const QPoint global = window.mapToGlobal(point);
    if (help->frameGeometry().contains(global)) help->move(global + QPoint(24, 24));
    QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, point); pump();
    CHECK(!help || !help->isVisible());
    CHECK(exported(editor) == beforePickerKeys && !activeTopicInput(editor));
    QApplication::setActiveWindow(&window);
    QTest::mouseClick(button("addChild"), Qt::LeftButton); pump();
    const QString child = editor.selectedNodeId();
    CHECK(child != QStringLiteral("a") && activeTopicInput(editor));
    topicInput(editor).setPlainText(QStringLiteral("Demo toolbar child"));
    topicKey(editor, Qt::Key_Return);
    CHECK(record(exported(editor), "nodes", child).at("topic") == "Demo toolbar child");
    QTest::mouseClick(button("deleteSelection"), Qt::LeftButton); pump();
    CHECK(!hasRecord(exported(editor), "nodes", child));
}

class ImageHttpServer final : public QTcpServer {
public:
    QByteArray png, alternate, oversizedPixels;
    QPointer<QTcpSocket> delayed;
    qsizetype overflowBytes = 0;
    bool overflowDisconnected = false, declaredDisconnected = false;
    explicit ImageHttpServer(QByteArray image, QByteArray other, QByteArray large)
        : png(std::move(image)), alternate(std::move(other)), oversizedPixels(std::move(large)) {
        CHECK(listen(QHostAddress::LocalHost, 0));
        QObject::connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto *socket = nextPendingConnection();
                socket->setParent(this);
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket,
                    [this, socket, headers = QByteArray(), handled = false]() mutable {
                    if (handled) return;
                    headers += socket->readAll();
                    if (!headers.contains("\r\n\r\n")) return;
                    handled = true;
                    const QByteArray path = headers.split(' ').value(1);
                    if (path == "/delay") { delayed = socket; return; }
                    if (path == "/404") { respond(socket, QByteArray("missing"), 404); return; }
                    if (path == "/corrupt") { respond(socket, QByteArray("not an image")); return; }
                    if (path == "/large") { respond(socket, oversizedPixels); return; }
                    if (path == "/declared") {
                        QObject::connect(socket, &QTcpSocket::disconnected, this, [this] { declaredDisconnected = true; });
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 16777217\r\n\r\n");
                        return;
                    }
                    if (path == "/chunked") {
                        overflowBytes = 0;
                        overflowDisconnected = false;
                        QObject::connect(socket, &QTcpSocket::disconnected, this, [this] { overflowDisconnected = true; });
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n");
                        auto *timer = new QTimer(socket);
                        timer->setInterval(0);
                        QObject::connect(timer, &QTimer::timeout, socket, [this, socket, timer] {
                            if (socket->state() != QAbstractSocket::ConnectedState) { timer->stop(); return; }
                            while (socket->bytesToWrite() <= 256 * 1024) {
                                // Fill a bounded send window rather than pacing one chunk per test wait tick.
                                // A valid PNG prefix exposes an incorrect unbounded read/decode.
                                const QByteArray chunk = overflowBytes == 0 ? png : QByteArray(64 * 1024, 'x');
                                socket->write(QByteArray::number(chunk.size(), 16) + "\r\n");
                                socket->write(chunk);
                                socket->write("\r\n");
                                overflowBytes += chunk.size();
                                if (overflowBytes >= 32 * 1024 * 1024) {
                                    timer->stop();
                                    socket->write("0\r\n\r\n");
                                    socket->disconnectFromHost();
                                    break;
                                }
                            }
                        });
                        timer->start();
                        return;
                    }
                    if (path == "/redirect") {
                        socket->write("HTTP/1.1 302 Found\r\nLocation: /ok\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        socket->disconnectFromHost();
                        return;
                    }
                    respond(socket, path == "/alternate" ? alternate : png);
                });
            }
        });
    }
    QString url(const QString &path) const {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path);
    }
    void respond(QTcpSocket *socket, const QByteArray &bytes, int status = 200) {
        CHECK(socket != nullptr);
        socket->write("HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK\r\n" : " Not Found\r\n") +
                      "Content-Type: image/png\r\nContent-Length: " + QByteArray::number(bytes.size()) +
                      "\r\nConnection: close\r\n\r\n");
        socket->write(bytes);
        socket->disconnectFromHost();
    }
};
static void demo_images_case() {
    QTemporaryDir first, second;
    CHECK(first.isValid() && second.isValid());
    const QString topic = QStringLiteral("Image root");
    const QImage pixels = imagePixels();
    const QColor green(13, 181, 67), purple(197, 29, 163);
    const QImage other = imagePixels(green, purple);
    auto pngBytes = [](const QImage &image) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        CHECK(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"));
        return bytes;
    };
    const QByteArray png = pngBytes(pixels), alternate = pngBytes(other);
    // A valid image exceeds the pixel budget by one column while its encoding stays small.
    QImage large(4097, 4096, QImage::Format_Mono);
    large.setColor(0, qRgb(231, 37, 53));
    large.setColor(1, qRgb(29, 71, 223));
    large.fill(0);
    const QByteArray oversizedPixels = pngBytes(large);
    CHECK(oversizedPixels.size() < 16 * 1024 * 1024);
    writeFile(first.filePath(QStringLiteral("image.png")), png);
    writeFile(second.filePath(QStringLiteral("image.png")), alternate);
    const QString unicodeFile = first.filePath(QString::fromUtf8("絵 世界.png"));
    writeFile(unicodeFile, png);
    writeFile(first.filePath(QStringLiteral("corrupt.png")), QByteArray("not an image"));
    writeFile(first.filePath(QStringLiteral("large.png")), oversizedPixels);
    writeFile(first.filePath(QStringLiteral("long.png")), png + QByteArray(16 * 1024 * 1024, 'x'));
    const QString mapPath = first.filePath(QStringLiteral("map.json"));
    RawLabelConfig config;
    config.resourceBasePath = first.path();
    DemoWindow window(config);
    showDemo(window);
    auto &editor = embedded(window);
    auto openReference = [&](const QString &reference) {
        writeFile(mapPath, encoded(imageDocument(reference)));
        CHECK(window.openFile(mapPath));
        CHECK(!window.isWindowModified());
    };
    for (const QString &reference : {QStringLiteral("image.png"), QUrl::fromLocalFile(unicodeFile).toString(QUrl::FullyEncoded), unicodeFile}) {
        openReference(reference);
        const Json before = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, reference); }, 5000));
        checkImageLayout(editor, QSizeF(120, 60), reference);
        CHECK(exported(editor) == before && changed.isEmpty() && !window.isWindowModified());
    }
    openReference(QStringLiteral("image.png"));
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, QStringLiteral("image.png")); }, 5000));
    const Json relativeDocument = exported(editor);
    const QString movedMap = second.filePath(QStringLiteral("saved.json"));
    dialogs({[&](QDialog *dialog) {
        auto *file = qobject_cast<QFileDialog *>(dialog);
        CHECK(file != nullptr);
        auto *name = file->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        CHECK(name != nullptr);
        name->setText(movedMap);
        static_cast<QDialog *>(file)->accept();
    }}, [&] { hostAction(window, QKeySequence::SaveAs).trigger(); });
    CHECK(sameFile(window.currentFilePath(), movedMap));
    CHECK(fileDocument(movedMap) == relativeDocument);
    editor.reloadImages();
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, QStringLiteral("image.png")); }, 5000));
    CHECK(editor.resourceBasePath() == QDir::cleanPath(first.path()) && !window.isWindowModified());
    RawLabelConfig secondConfig;
    secondConfig.resourceBasePath = second.path();
    DemoWindow secondWindow(secondConfig);
    CHECK(secondWindow.openFile(movedMap));
    showDemo(secondWindow);
    CHECK(QTest::qWaitFor([&] {
        return imageHasColors(embedded(secondWindow), topic, QStringLiteral("image.png"), green, purple);
    }, 5000));
    CHECK(exported(embedded(secondWindow)) == relativeDocument && !secondWindow.isWindowModified());
    window.activateWindow();
    auto waitUnavailable = [&](const QString &reference, const QImage &pending) {
        CHECK(QTest::qWaitFor([&] {
            // Drain newly posted transfer events in a bounded slice: qWaitFor's
            // single event pass plus sleep otherwise artificially throttles 16 MiB.
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            return paintScene(editor, nodeImageRect(editor, topic, reference)) != pending;
        }, 5000));
        CHECK(imageHandle(editor) == nullptr && !imageHasColors(editor, topic, reference));
        CHECK(editor.lastError().isEmpty() && QApplication::activeModalWidget() == nullptr);
    };
    for (const QString &reference : {QStringLiteral("missing.png"), QStringLiteral("corrupt.png"),
                                     QStringLiteral("large.png"), QStringLiteral("long.png"), QStringLiteral("data:image/png;base64,AA==")}) {
        openReference(reference);
        const Json before = exported(editor);
        const QImage pending = paintScene(editor, nodeImageRect(editor, topic, reference));
        QSignalSpy changed(&editor, &Editor::documentChanged);
        waitUnavailable(reference, pending);
        CHECK(exported(editor) == before && changed.isEmpty() && !window.isWindowModified());
    }

    ImageHttpServer server(png, alternate, oversizedPixels);
    const QString webMap = first.filePath(QStringLiteral("web.json"));
    auto *field = editor.findChild<QLineEdit *>(QStringLiteral("nodeImageUrl"));
    CHECK(field != nullptr);
    auto enterUrl = [&](const QString &source) {
        CHECK(editor.selectNode(QStringLiteral("r")));
        field->setText(source);
        CHECK(window.isWindowModified());
        CHECK(window.saveFile(webMap));
        CHECK(!window.isWindowModified());
    };
    const QString webUrl = server.url(QStringLiteral("/ok"));
    enterUrl(webUrl);
    const Json webDocument = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, webUrl); }, 5000));
    CHECK(exported(editor) == webDocument && changed.isEmpty() && !window.isWindowModified());
    shortcut(editor, Qt::Key_0, Qt::ControlModifier);
    const QPoint end = startImageResize(editor, QPointF(60, 30));
    CHECK(exported(editor) == webDocument && changed.isEmpty());
    releaseImageResize(editor, end);
    checkImageLayout(editor, QSizeF(180, 90), webUrl);
    CHECK(changed.size() == 1 && window.isWindowModified());
    CHECK(window.saveFile(webMap));
    const Json resized = fileDocument(webMap);
    CHECK(record(resized, "nodes", QStringLiteral("r")).at("image") ==
          Json({{"url", utf8(webUrl)}, {"width", 180}, {"height", 90}}));
    CHECK(window.openFile(webMap));
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, webUrl); }, 5000));
    checkImageLayout(editor, QSizeF(180, 90), webUrl);
    CHECK(exported(editor) == resized && !window.isWindowModified());

    for (const QString &path : {QStringLiteral("/404"), QStringLiteral("/corrupt"), QStringLiteral("/large"),
                                QStringLiteral("/declared"), QStringLiteral("/chunked")}) {
        const QString source = server.url(path);
        enterUrl(source);
        const Json before = exported(editor);
        const QImage pending = paintScene(editor, nodeImageRect(editor, topic, source));
        changed.clear();
        waitUnavailable(source, pending);
        CHECK(exported(editor) == before && changed.isEmpty() && !window.isWindowModified());
        if (path == QStringLiteral("/declared")) CHECK(QTest::qWaitFor([&] { return server.declaredDisconnected; }, 5000));
        if (path == QStringLiteral("/chunked")) {
            CHECK(QTest::qWaitFor([&] { return server.overflowDisconnected; }, 5000));
            CHECK(server.overflowBytes > 16 * 1024 * 1024 && server.overflowBytes < 32 * 1024 * 1024);
        }
    }
    const QString redirected = server.url(QStringLiteral("/redirect"));
    enterUrl(redirected);
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, redirected); }, 5000));
    CHECK(!window.isWindowModified());
    const QString delayed = server.url(QStringLiteral("/delay"));
    enterUrl(delayed);
    CHECK(QTest::qWaitFor([&] { return !server.delayed.isNull(); }, 5000));
    const QString current = server.url(QStringLiteral("/alternate"));
    enterUrl(current);
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, current, green, purple); }, 5000));
    const Json currentDocument = exported(editor);
    changed.clear();
    server.respond(server.delayed, png);
    CHECK(QTest::qWaitFor([&] { return server.delayed.isNull(); }, 5000));
    pump();
    CHECK(imageHasColors(editor, topic, current, green, purple));
    CHECK(exported(editor) == currentDocument && changed.isEmpty() && !window.isWindowModified());
    CHECK(window.newFile());
    CHECK(editor.resourceBasePath() == QDir::cleanPath(first.path()));
    CHECK(window.openFile(webMap) && editor.resourceBasePath() == QDir::cleanPath(first.path()));
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, topic, current, green, purple); }, 5000));
    // A pending socket is canceled with its window; no raw editor callback survives.
    auto dying = std::make_unique<DemoWindow>(config);
    CHECK(embedded(*dying).loadJson(encoded(imageDocument(delayed))));
    CHECK(QTest::qWaitFor([&] { return !server.delayed.isNull(); }, 5000));
    dying.reset();
    CHECK(QTest::qWaitFor([&] { return server.delayed.isNull(); }, 5000));
}
#endif

#ifdef M3_QT_TEST_DEMO
static void demo_html_case() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("original.map.json"));
    const QString outputStem = directory.filePath(QStringLiteral("combined"));
    const QString outputPath = outputStem + QStringLiteral(".html");
    const QByteArray original = encoded(editorFixture());
    writeFile(inputPath, original);
    DemoWindow window;
    showDemo(window);
    auto &editor = embedded(window);
    auto &action = textAction(window, {QStringLiteral("Export HTML")});
    dialogs({[&](QDialog *dialog) {
        auto *file = qobject_cast<QFileDialog *>(dialog);
        CHECK(file != nullptr && file->selectedFiles().size() == 1);
        CHECK(QFileInfo(file->selectedFiles().front()).fileName() == QStringLiteral("Untitled.html"));
        file->reject();
    }}, [&] { action.trigger(); });
    CHECK(window.openFile(inputPath));
    CHECK(editor.renameNode(QStringLiteral("r"), QString::fromUtf8("HTML saved 世界")));
    CHECK(editor.setExpanded(QStringLiteral("a"), false));
    const auto live = editor.toJson();
    const QString current = window.currentFilePath(), title = window.windowTitle();
    const QString node = editor.selectedNodeId(), link = editor.selectedLinkId();
    QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
    const auto unchanged = [&] {
        CHECK(editor.toJson() == live && window.currentFilePath() == current && window.windowTitle() == title);
        CHECK(window.isWindowModified() && editor.selectedNodeId() == node && editor.selectedLinkId() == link);
        CHECK(changed.isEmpty() && selected.isEmpty() && readFile(inputPath) == original);
    };
    dialogs({[&](QDialog *dialog) {
        auto *file = qobject_cast<QFileDialog *>(dialog);
        CHECK(file != nullptr && file->acceptMode() == QFileDialog::AcceptSave);
        CHECK(file->fileMode() == QFileDialog::AnyFile && !file->testOption(QFileDialog::DontConfirmOverwrite));
        CHECK(file->selectedFiles().size() == 1);
        CHECK(sameFile(file->selectedFiles().front(), directory.filePath(QStringLiteral("original.map.html"))));
        auto *name = file->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        CHECK(name != nullptr);
        name->setText(outputStem);
        CHECK(QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection));
    }}, [&] { action.trigger(); });
    const QByteArray bytes = readFile(outputPath);
    CHECK(!QFileInfo::exists(outputStem));
    const HtmlPage page(QString::fromUtf8(bytes));
    CHECK(page.values(QStringLiteral("title")) == QStringList{QString::fromUtf8("HTML saved 世界")});
    CHECK(page.classValues(QStringLiteral("m3-article-topic")).contains(QStringLiteral("Delta")));
    checkHtmlDocument(page);
    sameHtmlMap(page, HtmlPage(editor.toHtml()));
    unchanged();
    const QString status = window.statusBar()->currentMessage();
    dialogs({fileResponse(QString(), false)}, [&] { action.trigger(); });
    CHECK(readFile(outputPath) == bytes && window.statusBar()->currentMessage() == status);
    unchanged();
    for (const auto &badPath : {QString(), directory.path()}) {
        window.statusBar()->clearMessage();
        CHECK(!window.exportHtmlFile(badPath));
        CHECK(!window.statusBar()->currentMessage().isEmpty() && readFile(outputPath) == bytes);
        unchanged();
    }
    writeFile(outputPath, QByteArray("sentinel export\n"));
    shortcut(editor, Qt::Key_F2);
    auto *draft = &topicInput(editor);
    draft->setPlainText(QStringLiteral("UNCOMMITTED DRAFT"));
    CHECK(window.exportHtmlFile(outputPath));
    CHECK(readFile(outputPath) == bytes);
    CHECK(activeTopicInput(editor) == draft && draft->hasFocus());
    CHECK(draft->toPlainText() == QStringLiteral("UNCOMMITTED DRAFT"));
    unchanged();
    topicKey(editor, Qt::Key_Escape);
    hostAction(window, QKeySequence::Save).trigger();
    CHECK(!window.isWindowModified() && window.currentFilePath() == current);
    CHECK(fileDocument(inputPath) == exported(editor) && readFile(outputPath) == bytes);
}

static void demo_markdown_case() {
    QTemporaryDir directory;
    CHECK(directory.isValid());
    const QString inputPath = directory.filePath(QStringLiteral("original.map.json"));
    const QString outputStem = directory.filePath(QStringLiteral("exported"));
    const QString outputPath = outputStem + QStringLiteral(".md");
    const QString unrelatedPath = directory.filePath(QStringLiteral("unrelated.txt"));
    const QByteArray original = encoded(editorFixture());
    const QByteArray sentinel("Keep unrelated bytes.\n");
    writeFile(inputPath, original);
    writeFile(unrelatedPath, sentinel);
    DemoWindow window;
    showDemo(window);
    auto &editor = embedded(window);
    auto &action = textAction(window, {QStringLiteral("Export Markdown")});
    CHECK(action.shortcut().isEmpty());
    const QString untitledTitle = window.windowTitle();
    const auto sample = exported(editor);
    const QString cleanPath = directory.filePath(QStringLiteral("clean.md"));
    dialogs({[&](QDialog *dialog) {
        auto *file = qobject_cast<QFileDialog *>(dialog);
        CHECK(file != nullptr && file->acceptMode() == QFileDialog::AcceptSave);
        CHECK(file->fileMode() == QFileDialog::AnyFile);
        CHECK(file->defaultSuffix() == QStringLiteral("md"));
        CHECK(!file->testOption(QFileDialog::DontConfirmOverwrite));
        CHECK(file->selectedFiles().size() == 1);
        CHECK(QFileInfo(file->selectedFiles().front()).fileName() == QStringLiteral("Untitled.md"));
        auto *name = file->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        CHECK(name != nullptr);
        name->setText(cleanPath);
        fileResponse(cleanPath, true)(dialog);
    }}, [&] { action.trigger(); });
    CHECK(!window.isWindowModified() && window.currentFilePath().isEmpty());
    CHECK(window.windowTitle() == untitledTitle && exported(editor) == sample);

    CHECK(window.openFile(inputPath));
    CHECK(editor.selectNode(QStringLiteral("r")));
    shortcut(editor, Qt::Key_F2);
    topicInput(editor).setPlainText(QString::fromUtf8("Markdown smoke 世界"));
    topicKey(editor, Qt::Key_Return);
    CHECK(activeTopicInput(editor) == nullptr && window.isWindowModified());
    const auto live = exported(editor);
    const QString current = window.currentFilePath(), title = window.windowTitle();
    const QString selectedNode = editor.selectedNodeId(), selectedLink = editor.selectedLinkId();
    QSignalSpy changed(&editor, &Editor::documentChanged);
    QSignalSpy selected(&editor, &Editor::selectionChanged);
    const auto unchanged = [&] {
        CHECK(exported(editor) == live && window.currentFilePath() == current);
        CHECK(window.isWindowModified() && window.windowTitle() == title);
        CHECK(editor.selectedNodeId() == selectedNode && editor.selectedLinkId() == selectedLink);
        CHECK(changed.isEmpty() && selected.isEmpty());
        CHECK(readFile(inputPath) == original && readFile(unrelatedPath) == sentinel);
    };
    dialogs({[&](QDialog *dialog) {
        auto *file = qobject_cast<QFileDialog *>(dialog);
        CHECK(file != nullptr && file->selectedFiles().size() == 1);
        CHECK(sameFile(file->selectedFiles().front(), directory.filePath(QStringLiteral("original.map.md"))));
        CHECK(file->selectedNameFilter() == QStringLiteral("Markdown files (*.md)"));
        auto *name = file->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
        CHECK(name != nullptr);
        name->setText(outputStem);
        const QByteArray screenshot = qgetenv("M3_QT_SCREENSHOT");
        if (!screenshot.isEmpty()) CHECK(file->grab().save(QString::fromLocal8Bit(screenshot)));
        // Invoke the dialog override through its metaobject, including suffix and overwrite handling.
        CHECK(QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection));
    }}, [&] { action.trigger(); });
    const QByteArray bytes = readFile(outputPath);
    CHECK(!QFileInfo::exists(outputStem));
    CHECK(bytes.startsWith("# <a id=\"m3-node-72\"></a>Markdown smoke 世界\n"));
    CHECK(bytes.endsWith('\n') && !bytes.endsWith("\n\n") && !bytes.contains('\r'));
    unchanged();
    const QString exportStatus = window.statusBar()->currentMessage();
    CHECK(!exportStatus.isEmpty());
    dialogs({fileResponse(QString(), false)}, [&] { action.trigger(); });
    CHECK(window.statusBar()->currentMessage() == exportStatus && readFile(outputPath) == bytes);
    unchanged();

    for (const QString &badPath : {QString(), directory.path()}) {
        window.statusBar()->clearMessage();
        CHECK(!window.exportMarkdownFile(badPath));
        CHECK(!window.statusBar()->currentMessage().isEmpty());
        CHECK(readFile(outputPath) == bytes);
        unchanged();
    }
    CHECK(window.exportMarkdownFile(outputPath));
    CHECK(readFile(outputPath) == bytes);
    unchanged();
    hostAction(window, QKeySequence::Save).trigger();
    CHECK(!window.isWindowModified() && window.currentFilePath() == current);
    CHECK(fileDocument(inputPath) == live && readFile(outputPath) == bytes);
    CHECK(readFile(unrelatedPath) == sentinel);
}

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
    RawLabelEditor editor;
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

static Json foldingFixture() {
    return Json::parse(R"json({
        "schemaVersion":1,"rootId":"53f7ebf31cde25df",
        "crossLinks":[{"directed":false,"icon":"","id":"5203211b-f5a3-4dc5-b0cf-2207db4c4555","source":"54c54ce77c9fb334","style":{},"target":"53f7ebf31cde25df","topic":"Very good"}],
        "nodes":[
            {"children":["53f7ee14ad5cfe73","53f814646ada783d","54d500c5bf4fb1b3","0907a3dc-10ba-4c96-9faa-d426bd57915d"],"hyperLink":"","icons":[],"id":"53f7ebf31cde25df","image":null,"note":"","style":{"background":"#8e44ad","color":"#e74c3c","fontStyle":"italic","fontWeight":"bold"},"tags":[],"topic":"VNote"},
            {"children":[],"hyperLink":"","icons":[],"id":"53f7ee14ad5cfe73","image":null,"note":"","style":{},"tags":[],"topic":"haha"},
            {"children":["54c54ce77c9fb334","57fe23a342aa9934","059e9ab4-a2fc-4f5d-a153-418057246bc4"],"hyperLink":"","icons":["\ud83d\ude03"],"id":"53f814646ada783d","image":null,"note":"","style":{"background":"#95a5a6","color":"#16a085"},"tags":[],"topic":"haha"},
            {"children":[],"hyperLink":"","icons":[],"id":"54c54ce77c9fb334","image":null,"note":"","style":{"background":"#2ecc71","color":"#2980b9"},"tags":[],"topic":"haha"},
            {"children":["57fe23f7c15f111a","57fe25185cc996fe"],"hyperLink":"","icons":[],"id":"57fe23a342aa9934","image":null,"note":"","style":{"color":"#f39c12"},"tags":[],"topic":"new node"},
            {"children":[],"hyperLink":"https://bing.com","icons":[],"id":"57fe23f7c15f111a","image":null,"note":"Very interesting question here is about the samkkdjfkdljfkjdf","style":{},"tags":[],"topic":"haha"},
            {"children":["57fe25b07a7a6eef","57fe263af1d4a0ce"],"hyperLink":"","icons":[],"id":"57fe25185cc996fe","image":null,"note":"","style":{},"tags":[],"topic":"new node"},
            {"children":[],"hyperLink":"","icons":[],"id":"57fe25b07a7a6eef","image":null,"note":"","style":{},"tags":[],"topic":"new node"},
            {"children":[],"hyperLink":"","icons":[],"id":"57fe263af1d4a0ce","image":null,"note":"","style":{},"tags":[],"topic":"new node"},
            {"children":[],"hyperLink":"","icons":[],"id":"059e9ab4-a2fc-4f5d-a153-418057246bc4","image":null,"note":"","style":{},"tags":[],"topic":"It is very simple question"},
            {"children":[],"hyperLink":"","icons":["\ud83d\ude06","\ud83d\ude02"],"id":"54d500c5bf4fb1b3","image":null,"note":"","style":{"fontStyle":"normal","fontWeight":"normal"},"tags":[],"topic":"a very simple question"},
            {"children":[],"hyperLink":"","icons":[],"id":"0907a3dc-10ba-4c96-9faa-d426bd57915d","image":null,"note":"","style":{"branchColor":"#e74c3c"},"tags":[],"topic":""}
        ]
    })json");
}

static void folding_positions() {
    const QString root = QStringLiteral("53f7ebf31cde25df");
    const QString branch = QStringLiteral("53f814646ada783d");
    const QString branchIcon = QString::fromUtf8("\xf0\x9f\x98\x83");
    const Json input = foldingFixture();
    for (auto direction : {Editor::LayoutDirection::Balanced, Editor::LayoutDirection::Right,
                           Editor::LayoutDirection::Left, Editor::LayoutDirection::Outline}) {
        RawLabelConfig config;
        config.initialVisibleLevels = 0;
        RawLabelEditor editor(config);
        CHECK(editor.loadJson(encoded(input)));
        CHECK(editor.setLayoutDirection(direction));
        showEditor(editor, QSize(1101, 901));
        auto &view = graphics(editor);
        // Keep the root visible, but deliberately away from the viewport center.
        editor.scrollSteps(1, 1);
        pump();
        const Json original = exported(editor);
        const QTransform zoom = view.transform();
        const QPointF origin = view.viewportTransform().map(QPointF());
        using LabelRectangle = std::pair<QString, QRectF>;
        auto rectangles = [&] {
            std::vector<LabelRectangle> result;
            for (auto *item : view.scene()->items()) {
                auto *label = dynamic_cast<QGraphicsTextItem *>(item);
                if (!label || !label->isVisible() || !label->parentItem()) continue;
                // Node and cross-link labels both remain at their restored geometry.
                result.emplace_back(label->toPlainText(), label->sceneBoundingRect());
            }
            return result;
        };
        const auto initial = rectangles();
        auto stable = [&](bool allVisible) {
            pump();
            CHECK(view.transform() == zoom);
            CHECK(QLineF(view.viewportTransform().map(QPointF()), origin).length() <= 1.0);
            const auto current = rectangles();
            if (allVisible) CHECK(current.size() == initial.size());
            for (const auto &entry : current) {
                // Routing labels can change when obstacles are hidden; nodes cannot.
                if (entry.first == QStringLiteral("Very good")) continue;
                CHECK(std::find(initial.begin(), initial.end(), entry) != initial.end());
            }
        };
        for (int cycle = 0; cycle < 3; ++cycle) {
            CHECK(editor.setExpanded(branch, false));
            stable(false);
            CHECK(texts(editor, QStringLiteral("Very good")).isEmpty());
            CHECK(texts(editor, QStringLiteral("It is very simple question")).isEmpty());
            CHECK(editor.setExpanded(branch, true));
            stable(true);
            CHECK(editor.setExpanded(root, false));
            stable(false);
            CHECK(texts(editor, branchIcon).isEmpty());
            CHECK(editor.setExpanded(root, true));
            stable(true);
            CHECK(exported(editor) == original);
        }
        CHECK(!editor.canUndo() && !editor.canRedo());
        CHECK(editor.updateNode(root, encoded(Json{{"note", "Semantic history"}})));
        CHECK(editor.setExpanded(branch, false));
        stable(false);
        CHECK(editor.undo() && exported(editor) == original);
        stable(false);
        CHECK(texts(editor, QStringLiteral("It is very simple question")).isEmpty());
        CHECK(editor.redo());
        stable(false);
        CHECK(editor.setExpanded(branch, true));
        stable(true);

        // Reopening and explicitly folding reserves the same hidden geometry.
        RawLabelEditor reopened(config);
        CHECK(reopened.loadJson(encoded(input)));
        CHECK(reopened.setExpanded(branch, false));
        CHECK(reopened.setLayoutDirection(direction));
        showEditor(reopened);
        CHECK(topicRect(reopened, branchIcon) == topicRect(editor, branchIcon));
        CHECK(reopened.setExpanded(branch, true));
        CHECK(topicRect(reopened, branchIcon) == topicRect(editor, branchIcon));
    }

    // Actual affordance and keyboard paths also keep a panned, fractional-zoom map steady.
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(input)));
    showEditor(editor, QSize(1101, 901));
    CHECK(editor.selectNode(branch));
    editor.zoom(1.375);
    editor.scrollSteps(1, 0);
    pump();
    auto &view = graphics(editor);
    const QRectF parent = topicRect(editor, branchIcon);
    const QPointF before = view.viewportTransform().map(parent.center());
    const QPoint circle = expansionPoint(editor, branchIcon);
    CHECK(view.viewport()->rect().contains(circle));
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, circle);
    pump();
    CHECK(texts(editor, QStringLiteral("It is very simple question")).isEmpty());
    CHECK(QLineF(view.viewportTransform().map(topicRect(editor, branchIcon).center()), before).length() <= 1.0);
    shortcut(editor, Qt::Key_Space);
    CHECK(!texts(editor, QStringLiteral("It is very simple question")).isEmpty());
    CHECK(QLineF(view.viewportTransform().map(topicRect(editor, branchIcon).center()), before).length() <= 1.0);
    CHECK(std::abs(editor.zoomFactor() - 1.375) < 1e-6);
}

static void collapse_hover_scene() {
    for (bool readOnly : {false, true}) for (qreal zoom : {qreal(1), qreal(1.5)}) {
        RawLabelEditor editor;
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::Highlight, QColor(30, 95, 210));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        editor.setPalette(palette);
        CHECK(editor.loadJson(encoded(editorFixture())));
        editor.setReadOnly(readOnly);
        showEditor(editor);
        editor.zoom(zoom);
        CHECK(editor.revealNode(QStringLiteral("a")));
        pump();
        auto &view = graphics(editor);
        const QRectF node = topicRect(editor, QStringLiteral("Alpha"));
        const QPoint button = expansionPoint(editor, QStringLiteral("Alpha"));
        const QPointF center = view.mapToScene(button);
        const Json original = exported(editor);
        auto checkHover = [&](bool hovered) {
            const QImage image = paintScene(editor, node);
            const QColor actual = image.pixelColor(imagePoint(image, node, center + QPointF(3, -3)));
            const QColor expected = palette.color(hovered ? QPalette::Highlight : QPalette::Base);
            CHECK(std::abs(actual.red() - expected.red()) <= 2 &&
                  std::abs(actual.green() - expected.green()) <= 2 &&
                  std::abs(actual.blue() - expected.blue()) <= 2);
        };
        movePointer(view, blankPoint(view), Qt::NoButton);
        checkHover(false);
        movePointer(view, button, Qt::NoButton);
        checkHover(true);
        for (bool expanded : {false, true}) {
            // Qt delivers the second rapid press as a double-click, not another press.
            if (expanded) QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, button);
            else QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, button);
            checkHover(true);
            QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, button);
            pump();
            checkHover(true);
            CHECK(texts(editor, QStringLiteral("Delta")).isEmpty() == !expanded);
            CHECK(exported(editor) == original && !activeTopicInput(editor));
        }
        movePointer(view, blankPoint(view), Qt::NoButton);
        checkHover(false);
        movePointer(view, button, Qt::NoButton);
        checkHover(true);
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(view.viewport(), &leave);
        checkHover(false);
        CHECK(editor.setExpanded(QStringLiteral("a"), false));
        checkHover(false);
        movePointer(view, button, Qt::NoButton);
        checkHover(true);
        editor.hide();
        CHECK(editor.setExpanded(QStringLiteral("a"), true));
        checkHover(false);
    }
}

static void collapse_scene() {
    collapse_hover_scene();
    RawLabelEditor editor;
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

    folding_positions();
}

static void graph_edits_scene() {
    RawLabelEditor editor;
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

static void multi_selection_case() {
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), d = QStringLiteral("d"), r = QStringLiteral("r");
    const QString alpha = QStringLiteral("Alpha"), beta = QStringLiteral("Beta"), delta = QStringLiteral("Delta");
    auto toggle = [](Editor &editor, const QString &topic, Qt::KeyboardModifiers modifiers) {
        QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, modifiers, labelPoint(editor, topic));
        pump();
    };
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(a));
        const Json original = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), selected(&editor, &Editor::selectionChanged);
        toggle(editor, beta, Qt::ControlModifier);
        CHECK(editor.selectedNodeIds() == QStringList({a, b}));
        CHECK(editor.selectedNodeId().isEmpty() && editor.selectedLinkId().isEmpty());
        CHECK(ownerItem(textItem(editor, alpha))->isSelected() && ownerItem(textItem(editor, beta))->isSelected());
        CHECK(!editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"))->isVisible());
        toggle(editor, delta, Qt::ShiftModifier);
        CHECK(editor.selectedNodeIds() == QStringList({a, b, d}));
        CHECK(selected.size() == 2 && selected.back().at(0).toString().isEmpty());
        for (const char *name : {"addChild", "addSibling", "addSiblingBefore", "editSelection", "editTopic", "moveNode",
                                "moveUp", "moveDown", "toggleExpanded", "addLink", "editLink", "toggleBold", "toggleItalic",
                                "resetStyle", "textColor", "fillColor", "editTags", "editIcons", "editNote", "toggleProperties"})
            CHECK(!editAction(editor, name).isEnabled());
        CHECK(editAction(editor, "deleteSelection").isEnabled());
        shortcut(editor, Qt::Key_F2);
        shortcut(editor, Qt::Key_B);
        shortcut(editor, Qt::Key_Space);
        CHECK(!activeTopicInput(editor) && exported(editor) == original && changed.isEmpty());

        // Right-click a member must not silently turn bulk Delete into singleton Delete.
        auto &view = graphics(editor);
        const QPoint point = labelPoint(editor, beta);
        QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier, point);
        CHECK(editor.selectedNodeIds() == QStringList({a, b, d}));
        bool visited = false;
        QApplication::clipboard()->clear();
        QTimer dismiss;
        dismiss.setInterval(0);
        QObject::connect(&dismiss, &QTimer::timeout, [&] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                visited = true;
                dismiss.stop();
                auto &copy = editAction(editor, "copy");
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(&copy).center());
                menu->close();
            }
        });
        dismiss.start();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, view.viewport()->mapToGlobal(point));
        QCoreApplication::sendEvent(view.viewport(), &context);
        dismiss.stop();
        CHECK(visited && editor.selectedNodeIds() == QStringList({a, b, d}));
        CHECK(QApplication::clipboard()->text() == QStringList({alpha, beta, delta}).join(QLatin1Char('\n')));
        CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Outline));
        CHECK(editor.selectedNodeIds() == QStringList({a, b, d}));
        QPalette palette = editor.palette();
        palette.setColor(QPalette::Highlight, QColor(75, 90, 150));
        editor.setPalette(palette);
        pump();
        CHECK(editor.selectedNodeIds() == QStringList({a, b, d}));
        CHECK(ownerItem(textItem(editor, alpha))->isSelected() && ownerItem(textItem(editor, delta))->isSelected());
        CHECK(exported(editor) == original && changed.isEmpty());
        toggle(editor, beta, Qt::ShiftModifier);
        CHECK(editor.selectedNodeIds() == QStringList({a, d}));
        toggle(editor, delta, Qt::ControlModifier);
        CHECK(editor.selectedNodeId() == a);
        toggle(editor, alpha, Qt::ControlModifier);
        CHECK(editor.selectedNodeIds().isEmpty());
        CHECK(editor.selectLink(QStringLiteral("l1")));
        toggle(editor, beta, Qt::ShiftModifier);
        CHECK(editor.selectedNodeIds() == QStringList({b}) && editor.selectedLinkId().isEmpty());
        toggle(editor, alpha, Qt::ControlModifier);
        clickLabel(editor, delta);
        CHECK(editor.selectedNodeIds() == QStringList({d}));
        toggle(editor, beta, Qt::ControlModifier);
        CHECK(editor.selectLink(QStringLiteral("l1")) && editor.selectedNodeIds().isEmpty());
        toggle(editor, alpha, Qt::ControlModifier);
        toggle(editor, beta, Qt::ShiftModifier);
        shortcut(editor, Qt::Key_Escape);
        CHECK(editor.selectedNodeIds().isEmpty());

        CHECK(editor.selectNode(d));
        toggle(editor, beta, Qt::ControlModifier);
        CHECK(editor.setExpanded(a, false));
        CHECK(editor.selectedNodeIds() == QStringList({b}));
        CHECK(editor.setExpanded(a, true));
        toggle(editor, delta, Qt::ShiftModifier);
        CHECK(!editor.loadJson(QByteArray("invalid")));
        CHECK(editor.selectedNodeIds() == QStringList({b, d}));
        CHECK(editor.removeNode(a));
        CHECK(editor.selectedNodeIds() == QStringList({b}));
    }
    {
        RawLabelConfig config;
        config.confirmSubtreeDeletion = false;
        RawLabelEditor editor(config);
        Json input = editorFixture();
        for (auto &node : input["nodes"]) if (node["id"] == "a") {
            node["hyperLink"] = "https://example.org";
            node["tags"] = {"tag"};
            node["image"] = {{"url", "memory:picture"}, {"width", 120}, {"height", 60}};
        }
        QObject::connect(&editor, &Editor::imageRequested, &editor, [&editor](const QString &url, quint64 request) {
            editor.provideImage(url, request, imagePixels());
        });
        CHECK(editor.loadJson(encoded(input)));
        showEditor(editor);
        CHECK(editor.selectNode(a));
        CHECK(imageHandle(editor, alpha));
        toggle(editor, beta, Qt::ControlModifier);
        CHECK(!imageHandle(editor, alpha));
        QSignalSpy activated(&editor, &Editor::nodeLinkActivated), changed(&editor, &Editor::documentChanged);
        const Json original = exported(editor);
        auto &view = graphics(editor);
        auto urlPoint = [&] {
            for (auto *child : ownerItem(textItem(editor, alpha))->childItems())
                if (child->isVisible() && child->cursor().shape() == Qt::PointingHandCursor)
                    return view.mapFromScene(child->sceneBoundingRect().center());
            throw std::runtime_error("Missing URL indicator");
        };
        // Nested URL and expansion hits are selection toggles, not actions.
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ControlModifier, urlPoint());
        CHECK(editor.selectedNodeIds() == QStringList({b}));
        const QPoint expand = expansionPoint(editor, alpha);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::ShiftModifier, expand);
        CHECK(editor.selectedNodeIds() == QStringList({b, a}));
        CHECK(exported(editor) == original && activated.isEmpty());
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, urlPoint());
        CHECK(activated.isEmpty() && exported(editor) == original);
        CHECK(editor.selectNode(b));
        toggle(editor, alpha, Qt::ShiftModifier);
        const QPoint from = labelPoint(editor, alpha), to = labelPoint(editor, beta);
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
        movePointer(view, to, Qt::LeftButton);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, to);
        CHECK(exported(editor) == original && changed.isEmpty());
        CHECK(editor.selectNode(b));
        toggle(editor, alpha, Qt::ControlModifier);
        QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, labelPoint(editor, alpha));
        QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, labelPoint(editor, alpha));
        CHECK(!activeTopicInput(editor));
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, labelPoint(editor, alpha));
        CHECK(!activeTopicInput(editor) && exported(editor) == original);
    }
    {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(d));
        toggle(editor, alpha, Qt::ControlModifier);
        toggle(editor, beta, Qt::ShiftModifier);
        const Json original = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged), errors(&editor, &Editor::errorOccurred);
        dialogs({deleteResponse(false)}, [&] { shortcut(editor, Qt::Key_Delete); });
        CHECK(exported(editor) == original && changed.isEmpty());
        CHECK(editor.selectedNodeIds() == QStringList({d, a, b}));
        dialogs({deleteResponse(true)}, [&] { shortcut(editor, Qt::Key_Delete); });
        auto expected = load(original);
        ok(m3_mindmap_remove_subtree(expected.get(), "a"));
        ok(m3_mindmap_remove_subtree(expected.get(), "b"));
        CHECK(exported(editor) == document(expected));
        CHECK(changed.size() == 1 && errors.isEmpty() && editor.selectedNodeId() == r);
    }
    {
        RawLabelConfig config;
        config.confirmSubtreeDeletion = false;
        RawLabelEditor editor(config);
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(d));
        toggle(editor, beta, Qt::ControlModifier);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        shortcut(editor, Qt::Key_Delete);
        CHECK(editor.selectedNodeId() == a && changed.size() == 1);
        CHECK(!hasRecord(exported(editor), "nodes", d) && !hasRecord(exported(editor), "nodes", b));
        toggle(editor, qs(record(exported(editor), "nodes", r).at("topic")), Qt::ShiftModifier);
        const Json before = exported(editor);
        CHECK(!editAction(editor, "deleteSelection").isEnabled());
        shortcut(editor, Qt::Key_Delete);
        CHECK(exported(editor) == before && changed.size() == 1);
        CHECK(editor.selectedNodeIds() == QStringList({a, r}));
        CHECK(editor.newDocument() && editor.selectedNodeIds() == QStringList({QStringLiteral("root")}));
    }
}

static void history_commands_case() {
    const QString root = QStringLiteral("r"), a = QStringLiteral("a"), b = QStringLiteral("b");
    const QString c = QStringLiteral("c"), d = QStringLiteral("d"), link = QStringLiteral("l1");
    QByteArray png;
    QBuffer buffer(&png);
    CHECK(buffer.open(QIODevice::WriteOnly) && imagePixels().save(&buffer, "PNG"));
    const QString embedded = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
    Json input = editorFixture();
    setTopic(input, "c", QStringLiteral("Gamma"));
    for (auto &entry : input.at("nodes")) if (entry.at("id") == "a") {
        entry["style"] = {{"custom", {{"integer", UINT64_C(9007199254740993)},
                                    {"nested", Json::array({true, "opaque", nullptr})}}}};
        entry["image"] = {{"url", utf8(embedded)}, {"width", 120}, {"height", 60}};
        entry["note"] = "Retain memo\n世界";
        entry["icons"] = {"bookmark"};
    }
    RawLabelConfig config;
    config.confirmSubtreeDeletion = false;
    config.autoRandomBranchColor = false;
    RawLabelEditor editor(config);
    QObject::connect(&editor, &Editor::imageRequested, &editor,
        [&editor](const QString &url, quint64 id) { editor.provideImage(url, id, imagePixels()); });
    CHECK(editor.loadJson(encoded(input)));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor, QSize(1100, 900));
    CHECK(QTest::qWaitFor([&] { return imageHasColors(editor, QStringLiteral("Alpha"), embedded); }, 5000));
    const Json baseline = exported(editor);
    CHECK(!editor.canUndo() && !editor.canRedo());
    QSignalSpy changed(&editor, &Editor::documentChanged);
    int commands = 0;
    auto roundTrip = [&](const std::function<void()> &operation) {
        const Json before = exported(editor);
        const QStringList beforeNodes = editor.selectedNodeIds();
        const QString beforeLink = editor.selectedLinkId();
        const auto count = changed.size();
        operation();
        pump();
        const Json after = exported(editor);
        CHECK(after != before && changed.size() == count + 1);
        const QStringList afterNodes = editor.selectedNodeIds();
        const QString afterLink = editor.selectedLinkId();
        CHECK(editor.canUndo() && !editor.canRedo());
        // Camera is live view state, not a property of the restored revision.
        editor.resetZoom();
        editor.zoom(1.1);
        auto &view = graphics(editor);
        view.centerOn(view.scene()->itemsBoundingRect().center() + QPointF(43, -31));
        pump();
        const QTransform zoom = view.transform();
        const QPointF center = view.mapToScene(view.viewport()->rect().center());
        auto restored = [&](const Json &expected, QStringList nodes, const QString &selectedLink) {
            CHECK(exported(editor) == expected);
            if (nodes.isEmpty() && selectedLink.isEmpty()) nodes.append(qs(expected.at("rootId")));
            CHECK(editor.selectedNodeIds() == nodes && editor.selectedLinkId() == selectedLink);
            CHECK(view.transform() == zoom && editor.layoutDirection() == Editor::LayoutDirection::Right);
            CHECK(QLineF(center, view.mapToScene(view.viewport()->rect().center())).length() <= 2.0 / zoom.m11());
        };
        CHECK(editor.undo());
        pump();
        restored(before, beforeNodes, beforeLink);
        CHECK(changed.size() == count + 2 && editor.canRedo());
        CHECK(editor.redo());
        pump();
        restored(after, afterNodes, afterLink);
        CHECK(changed.size() == count + 3 && !editor.canRedo());
        ++commands;
    };
    QString added;
    CHECK(editor.selectNode(a));
    roundTrip([&] {
        added = editor.addNode(a, QStringLiteral("Created child"));
        CHECK(!added.isEmpty() && !hasRecord(baseline, "nodes", added));
        CHECK(record(exported(editor), "nodes", a).at("children") == Json({"d", utf8(added)}));
    });
    roundTrip([&] {
        const auto ids = editor.addNodes(a, encoded(Json::array({
            Json{{"topic", "Batch image"}, {"image", {{"url", utf8(embedded)}, {"width", 40}, {"height", 20}}}},
            Json{{"topic", "Batch link"}, {"hyperLink", "https://example.test/batch"}}
        })));
        CHECK(ids.size() == 2 && editor.selectedNodeId() == added);
        CHECK(record(exported(editor), "nodes", a).at("children") == Json({"d", utf8(added), utf8(ids[0]), utf8(ids[1])}));
    });
    roundTrip([&] { CHECK(editor.renameNode(added, QStringLiteral("Renamed child 世界"))); });
    roundTrip([&] {
        CHECK(editor.moveNode(added, b));
        CHECK(record(exported(editor), "nodes", b).at("children") == Json({utf8(added)}));
    });
    roundTrip([&] {
        CHECK(editor.moveNode(c, root, 0));
        CHECK(record(exported(editor), "nodes", root).at("children") == Json({"c", "a", "b"}));
    });
    CHECK(editor.selectNode(a));
    roundTrip([&] {
        CHECK(editor.updateNode(a, encoded(Json{{"style", {{"color", "#123456"}, {"fontWeight", "bold"}}},
                                               {"hyperLink", "docs/history"}})));
        const Json node = record(exported(editor), "nodes", a);
        CHECK(node.at("style").at("custom") == record(baseline, "nodes", a).at("style").at("custom"));
        CHECK(node.at("image").at("url") == utf8(embedded));
    });
    CHECK(editor.selectNode(d));
    {
        const QByteArray before = editor.toJson();
        const auto count = changed.size();
        QSignalSpy undo(&editor, &Editor::undoAvailable), redo(&editor, &Editor::redoAvailable);
        CHECK(editor.setExpanded(a, false));
        CHECK(editor.selectedNodeId() == a && texts(editor, QStringLiteral("Delta")).isEmpty());
        shortcut(editor, Qt::Key_Space);
        CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
        CHECK(editor.toJson() == before && changed.size() == count);
        CHECK(editor.canUndo() && !editor.canRedo() && undo.isEmpty() && redo.isEmpty());
    }
    QString createdLink;
    roundTrip([&] {
        createdLink = editor.addLink(a, b, false, QStringLiteral("Created link"));
        CHECK(!createdLink.isEmpty() && !hasRecord(baseline, "crossLinks", createdLink));
    });
    CHECK(editor.selectLink(link));
    roundTrip([&] {
        CHECK(editor.updateLink(link, b, a, false, QStringLiteral("Reversed link")));
        Json expected = record(baseline, "crossLinks", link);
        expected["source"] = "b"; expected["target"] = "a";
        expected["directed"] = false; expected["topic"] = "Reversed link";
        CHECK(record(exported(editor), "crossLinks", link) == expected);
    });
    roundTrip([&] {
        CHECK(editor.removeLink(link));
        CHECK(!hasRecord(exported(editor), "crossLinks", link));
    });
    CHECK(editor.selectLink(createdLink));
    roundTrip([&] {
        trigger(editor, "deleteSelection");
        CHECK(!hasRecord(exported(editor), "crossLinks", createdLink));
    });
    CHECK(editor.selectNode(d));
    roundTrip([&] {
        const Json before = exported(editor);
        auto &view = graphics(editor);
        const QPoint from = labelPoint(editor, QStringLiteral("Delta"));
        const QPoint to = labelPoint(editor, QStringLiteral("Beta"));
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
        movePointer(view, to, Qt::LeftButton);
        CHECK(exported(editor) == before);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, to);
        CHECK(record(exported(editor), "nodes", b).at("children") == Json({utf8(added), "d"}));
    });
    CHECK(editor.selectNode(a));
    roundTrip([&] {
        editor.resetZoom();
        const Json before = exported(editor);
        const auto count = changed.size();
        const QPoint end = startImageResize(editor, QPointF(60, 30), QStringLiteral("Alpha"));
        movePointer(graphics(editor), end + QPoint(20, 10), Qt::LeftButton);
        movePointer(graphics(editor), end, Qt::LeftButton);
        CHECK(exported(editor) == before && changed.size() == count);
        releaseImageResize(editor, end);
        const Json image = record(exported(editor), "nodes", a).at("image");
        CHECK(image.at("url") == utf8(embedded));
        CHECK(image.at("width").get<double>() > 120 && image.at("height").get<double>() > 60);
    });
    roundTrip([&] {
        QTemporaryDir directory;
        CHECK(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("linked file.txt"));
        QFile file(path);
        CHECK(file.open(QIODevice::WriteOnly));
        file.close();
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(path)});
        auto &view = graphics(editor);
        const QPoint point = labelPoint(editor, QStringLiteral("Alpha"));
        const Json before = exported(editor);
        QDragEnterEvent enter(point, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &enter);
        CHECK(enter.isAccepted() && exported(editor) == before);
        QDropEvent drop(QPointF(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &drop);
        CHECK(drop.isAccepted());
        CHECK(record(exported(editor), "nodes", a).at("hyperLink") ==
              utf8(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)));
    });
    CHECK(editor.selectNode(d));
    roundTrip([&] {
        CHECK(editor.removeNode(b));
        const Json after = exported(editor);
        CHECK(!hasRecord(after, "nodes", b) && !hasRecord(after, "nodes", d) && !hasRecord(after, "nodes", added));
        CHECK(!hasRecord(after, "crossLinks", QStringLiteral("l2")));
    });
    // Overlapping selected subtrees and their links disappear in one command.
    CHECK(editor.undo());
    --commands;
    CHECK(editor.selectNode(d));
    QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::ControlModifier,
                      labelPoint(editor, QStringLiteral("Beta")));
    QTest::mouseClick(graphics(editor).viewport(), Qt::LeftButton, Qt::ShiftModifier,
                      labelPoint(editor, QStringLiteral("Alpha")));
    pump();
    CHECK(editor.selectedNodeIds() == QStringList({d, b, a}));
    roundTrip([&] {
        shortcut(editor, Qt::Key_Delete);
        const Json after = exported(editor);
        CHECK(preorderIds(after) == QStringList({root, c}));
        CHECK(after.at("crossLinks").empty());
    });
    // No preview, multi-delete sub-operation, or history restoration added an entry.
    for (int i = 0; i < commands; ++i) CHECK(editor.undo());
    CHECK(!editor.canUndo() && editor.canRedo() && exported(editor) == baseline);
    const auto count = changed.size();
    CHECK(!editor.undo() && changed.size() == count && exported(editor) == baseline);
    CHECK(record(exported(editor), "nodes", a).at("image").at("url") == utf8(embedded));
}

static void history_link_gestures_case() {
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Right));
    showEditor(editor);
    CHECK(editor.selectLink(QStringLiteral("l1")));
    editor.resetZoom();
    auto &view = graphics(editor);
    const Json baseline = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    // Select the source endpoint by its proximity to the source node, not a private type.
    QList<QGraphicsItem *> handles;
    for (auto *item : view.scene()->items())
        if (item->isVisible() && item->cursor().shape() == Qt::OpenHandCursor) handles.append(item);
    CHECK(handles.size() == 2);
    const QPointF source = topicRect(editor, QStringLiteral("Alpha")).center();
    auto *handle = QLineF(source, handles[0]->scenePos()).length() < QLineF(source, handles[1]->scenePos()).length()
                       ? handles[0] : handles[1];
    view.ensureVisible(handle, 30, 30);
    pump();
    const QPoint target = labelPoint(editor, QStringLiteral("Delta"));
    const QPoint from = handle->deviceTransform(view.viewportTransform()).map(handle->boundingRect().center()).toPoint();
    CHECK(view.itemAt(from) == handle);
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, from);
    movePointer(view, target, Qt::LeftButton);
    CHECK(exported(editor) == baseline && changed.isEmpty() && !editor.canUndo());
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);
    pump();
    Json reconnected = baseline;
    for (auto &entry : reconnected.at("crossLinks")) if (entry.at("id") == "l1") entry["source"] = "d";
    CHECK(exported(editor) == reconnected && changed.size() == 1);
    CHECK(editor.undo() && exported(editor) == baseline && !editor.canUndo());
    CHECK(editor.selectedLinkId() == QStringLiteral("l1"));
    CHECK(editor.redo() && exported(editor) == reconnected);

    std::exception_ptr failure;
    bool visited = false;
    const QPoint point = labelPoint(editor, QStringLiteral("Related"));
    QTimer dismiss;
    dismiss.setSingleShot(true);
    QObject::connect(&dismiss, &QTimer::timeout, &editor, [] {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) menu->close();
    });
    QTimer::singleShot(0, &editor, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        try {
            CHECK(menu);
            auto &action = textAction(*menu, {QStringLiteral("<--->")});
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(&action).center());
            visited = true;
        } catch (...) { failure = std::current_exception(); }
        if (menu) menu->close();
    });
    dismiss.start(5000);
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, view.viewport()->mapToGlobal(point));
    QCoreApplication::sendEvent(view.viewport(), &event);
    dismiss.stop();
    pump();
    if (failure) std::rethrow_exception(failure);
    CHECK(visited);
    Json directed = reconnected;
    for (auto &entry : directed.at("crossLinks")) if (entry.at("id") == "l1") {
        entry["directed"] = true;
        entry["style"]["arrowDirection"] = "both";
    }
    CHECK(exported(editor) == directed && changed.size() == 4);
    CHECK(editor.undo() && exported(editor) == reconnected && changed.size() == 5);
    CHECK(editor.redo() && exported(editor) == directed && changed.size() == 6);
}

static void history_availability_case() {
    RawLabelEditor editor;
    CHECK(editor.loadJson(encoded(editorFixture())));
    showEditor(editor);
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), root = QStringLiteral("r");
    const Json baseline = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged), undo(&editor, &Editor::undoAvailable);
    QSignalSpy redo(&editor, &Editor::redoAvailable);
    auto availability = [&](bool canUndo, bool canRedo, std::initializer_list<bool> undoEvents,
                            std::initializer_list<bool> redoEvents) {
        CHECK(editor.canUndo() == canUndo && editor.canRedo() == canRedo);
        CHECK(editAction(editor, "undo").isEnabled() == canUndo);
        CHECK(editAction(editor, "redo").isEnabled() == canRedo);
        CHECK(undo.size() == static_cast<int>(undoEvents.size()) && redo.size() == static_cast<int>(redoEvents.size()));
        int index = 0;
        for (bool available : undoEvents) CHECK(undo.at(index++).front().toBool() == available);
        index = 0;
        for (bool available : redoEvents) CHECK(redo.at(index++).front().toBool() == available);
    };
    availability(false, false, {}, {});
    CHECK(!editor.undo() && !editor.redo() && exported(editor) == baseline && changed.isEmpty());
    CHECK(editor.renameNode(a, QStringLiteral("First revision")));
    const Json first = exported(editor);
    availability(true, false, {true}, {});
    CHECK(editor.renameNode(b, QStringLiteral("Second revision")));
    const Json second = exported(editor);
    availability(true, false, {true}, {});
    trigger(editor, "undo");
    CHECK(exported(editor) == first && changed.size() == 3);
    availability(true, true, {true}, {true});

    // Every semantic no-op family and invalid transaction leaves the redo branch intact.
    CHECK(editor.renameNode(a, QStringLiteral("First revision")));
    CHECK(editor.updateNode(a, encoded(Json{{"topic", "First revision"}, {"style", Json::object()}})));
    CHECK(editor.moveNode(QStringLiteral("c"), root, 2));
    CHECK(editor.setExpanded(a, false));
    CHECK(texts(editor, QStringLiteral("Delta")).isEmpty());
    availability(true, true, {true}, {true});
    CHECK(editor.setExpanded(a, true));
    CHECK(!texts(editor, QStringLiteral("Delta")).isEmpty());
    CHECK(editor.updateLink(QStringLiteral("l1"), a, b, true, QStringLiteral("Related")));
    CHECK(!editor.moveNode(a, QStringLiteral("d")));
    CHECK(!editor.updateNode(a, QByteArray("{bad")));
    CHECK(!editor.updateLink(QStringLiteral("l1"), a, QStringLiteral("absent"), true, QString()));
    CHECK(editor.addNode(QStringLiteral("absent"), QStringLiteral("Invalid parent")).isEmpty());
    CHECK(editor.addLink(a, QStringLiteral("absent"), false).isEmpty());
    CHECK(!editor.removeNode(root) && !editor.removeLink(QStringLiteral("absent")));
    CHECK(!editor.loadJson(QByteArray("{invalid")));
    const QString nul = QStringLiteral("Rejected") + QChar(QChar::Null) + QStringLiteral("tail");
    CHECK(!editor.newDocument(nul));
    CHECK(exported(editor) == first && changed.size() == 3);
    availability(true, true, {true}, {true});
    const QByteArray saved = editor.toJson();
    const QString markdown = editor.toMarkdown();
    CHECK(!saved.isEmpty() && !markdown.isEmpty() && Json::parse(saved.constData()) == first);
    CHECK(editor.selectLink(QStringLiteral("l1")));
    editor.clearSelection();
    CHECK(editor.setLayoutDirection(Editor::LayoutDirection::Left));
    editor.zoom(1.2);
    editor.scrollSteps(1, -1);
    editor.reloadImages();
    editor.setResourceBasePath(QDir::tempPath());
    pump();
    CHECK(exported(editor) == first && changed.size() == 3);
    availability(true, true, {true}, {true});
    trigger(editor, "redo");
    CHECK(exported(editor) == second && changed.size() == 4);
    availability(true, false, {true}, {true, false});
    CHECK(editor.undo() && exported(editor) == first);
    availability(true, true, {true}, {true, false, true});
    CHECK(editor.renameNode(b, QStringLiteral("Different branch")));
    const Json branch = exported(editor);
    CHECK(branch != second && changed.size() == 6);
    availability(true, false, {true}, {true, false, true, false});
    CHECK(!editor.redo() && exported(editor) == branch && changed.size() == 6);

    // A failed replacement did not secretly reset either side of the cursor.
    CHECK(editor.undo() && exported(editor) == first);
    CHECK(!editor.loadJson(QByteArray("not JSON")) && exported(editor) == first);
    CHECK(editor.undo() && exported(editor) == baseline);
    CHECK(editor.redo() && exported(editor) == first);
    CHECK(editor.redo() && exported(editor) == branch);
    CHECK(Json::parse(saved.constData()) == first); // Previously returned save bytes still belong to the caller.
    undo.clear(); redo.clear(); changed.clear();
    CHECK(editor.undo() && exported(editor) == first);
    availability(true, true, {}, {true});
    CHECK(editor.newDocument(QStringLiteral("New baseline")));
    CHECK(!editor.canUndo() && !editor.canRedo());
    availability(false, false, {false}, {true, false});
    const Json fresh = exported(editor);
    CHECK(!editor.undo() && !editor.redo() && exported(editor) == fresh && changed.size() == 2);
    CHECK(editor.renameNode(QStringLiteral("root"), QStringLiteral("Modified new document")));
    CHECK(editor.undo() && exported(editor) == fresh);
    undo.clear(); redo.clear(); changed.clear();
    CHECK(editor.loadJson(saved));
    CHECK(exported(editor) == first && changed.size() == 1);
    availability(false, false, {}, {false});
    CHECK(!editor.undo() && !editor.redo());
    CHECK(editor.renameNode(a, QStringLiteral("After load")));
    CHECK(editor.loadJson(editor.toJson())); // Even identical successful Open establishes a new baseline.
    CHECK(!editor.canUndo() && !editor.canRedo());
}

static void history_limits_case() {
    const QString root = QStringLiteral("root");
    for (int limit : {1, 3, 0}) {
        RawLabelConfig config;
        config.undoLimit = limit;
        RawLabelEditor editor(config);
        CHECK(editor.newDocument(QStringLiteral("Revision 0")));
        const int commands = limit == 0 ? 105 : limit + 3;
        const int retained = limit == 0 ? commands : limit;
        std::vector<Json> states{exported(editor)};
        for (int i = 1; i <= commands; ++i) {
            CHECK(editor.renameNode(root, QStringLiteral("Revision %1").arg(i)));
            states.push_back(exported(editor));
        }
        QSignalSpy changed(&editor, &Editor::documentChanged);
        for (int i = commands - 1; i >= commands - retained; --i) {
            CHECK(editor.undo() && exported(editor) == states[static_cast<size_t>(i)]);
            CHECK(editor.canUndo() == (i > commands - retained));
        }
        CHECK(changed.size() == retained && !editor.undo() && changed.size() == retained);
        for (int i = commands - retained + 1; i <= commands; ++i)
            CHECK(editor.redo() && exported(editor) == states[static_cast<size_t>(i)]);
        CHECK(!editor.canRedo() && !editor.redo() && changed.size() == retained * 2);
        CHECK(editor.undo());
        CHECK(editor.renameNode(root, QStringLiteral("Branched revision")));
        CHECK(!editor.canRedo());
        CHECK(editor.undo() && exported(editor) == states[static_cast<size_t>(commands - 1)]);
        for (int i = commands - 2; i >= commands - retained; --i)
            CHECK(editor.undo() && exported(editor) == states[static_cast<size_t>(i)]);
        CHECK(!editor.canUndo());
    }
    // A live limit bounds the entire history, including redo states beyond the current snapshot.
    for (int current : {5, 3, 1, 0}) {
        RawLabelEditor editor;
        CHECK(editor.newDocument(QStringLiteral("Revision 0")));
        std::vector<Json> states{exported(editor)};
        for (int i = 1; i <= 5; ++i) {
            CHECK(editor.renameNode(root, QStringLiteral("Revision %1").arg(i)));
            states.push_back(exported(editor));
        }
        for (int i = 5; i > current; --i) CHECK(editor.undo());
        CHECK(exported(editor) == states[static_cast<size_t>(current)]);
        showEditor(editor);
        shortcut(editor, Qt::Key_F2);
        QPointer<QPlainTextEdit> input = &topicInput(editor);
        const QString original = input->toPlainText();
        input->moveCursor(QTextCursor::End);
        input->insertPlainText(QStringLiteral(" draft"));
        auto cursor = input->textCursor();
        cursor.setPosition(1); cursor.setPosition(4, QTextCursor::KeepAnchor);
        input->setTextCursor(cursor);
        const QStringList selection = editor.selectedNodeIds();
        auto *scene = graphics(editor).scene();
        QSignalSpy changed(&editor, &Editor::documentChanged), pending(&editor, &Editor::pendingEditChanged);
        QSignalSpy selected(&editor, &Editor::selectionChanged);
        QSignalSpy undoAvailable(&editor, &Editor::undoAvailable), redoAvailable(&editor, &Editor::redoAvailable);
        editor.setUndoLimit(2);
        pump();
        const int first = std::min(current, 3), last = first + 2;
        CHECK(exported(editor) == states[static_cast<size_t>(current)]);
        CHECK(input && activeTopicInput(editor) == input.data() && input->hasFocus());
        CHECK(input->toPlainText() == original + QStringLiteral(" draft") && editor.hasPendingEdit());
        CHECK(input->textCursor().anchor() == 1 && input->textCursor().position() == 4);
        CHECK(changed.isEmpty() && pending.isEmpty() && selected.isEmpty());
        CHECK(graphics(editor).scene() == scene && editor.selectedNodeIds() == selection);
        CHECK(editor.canUndo() == (current > first) && editor.canRedo() == (current < last));
        CHECK(undoAvailable.size() == (current > 0 && current == first ? 1 : 0));
        if (!undoAvailable.isEmpty()) CHECK(!undoAvailable.front().front().toBool());
        CHECK(redoAvailable.isEmpty());
        topicKey(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(input->toPlainText() == original && !editor.hasPendingEdit() && changed.isEmpty());
        topicKey(editor, Qt::Key_Escape);
        // Increasing cannot restore the discarded oldest past or farthest future snapshots.
        editor.setUndoLimit(20);
        for (int i = current - 1; i >= first; --i)
            CHECK(editor.undo() && exported(editor) == states[static_cast<size_t>(i)]);
        CHECK(!editor.canUndo() && !editor.undo());
        for (int i = first + 1; i <= last; ++i)
            CHECK(editor.redo() && exported(editor) == states[static_cast<size_t>(i)]);
        CHECK(!editor.canRedo() && !editor.redo());
        // Zero (and a negative normalized to zero) allows later commands beyond the old bound.
        editor.setUndoLimit(2);
        editor.setUndoLimit(current == 0 ? -1 : 0);
        for (int i = 1; i <= 5; ++i)
            CHECK(editor.renameNode(root, QStringLiteral("Unlimited %1").arg(i)));
        for (int i = 4; i >= 1; --i) {
            CHECK(editor.undo());
            CHECK(record(exported(editor), "nodes", root).at("topic") == utf8(QStringLiteral("Unlimited %1").arg(i)));
        }
        CHECK(editor.undo() && exported(editor) == states[static_cast<size_t>(last)]);
        for (int i = last - 1; i >= first; --i)
            CHECK(editor.undo() && exported(editor) == states[static_cast<size_t>(i)]);
        CHECK(!editor.canUndo());
    }
    // Availability observers may destroy the facade during a live trim.
    QPointer<Editor> closing = new RawLabelEditor;
    for (int i = 1; i <= 5; ++i) CHECK(closing->renameNode(root, QString::number(i)));
    for (int i = 5; i > 1; --i) CHECK(closing->undo());
    QObject::connect(closing.data(), &Editor::undoAvailable, qApp, [&](bool available) {
        if (!available) delete closing.data();
    });
    closing->setUndoLimit(2);
    CHECK(closing.isNull());
}

static void history_drafts_case() {
    const QString a = QStringLiteral("a"), b = QStringLiteral("b"), linkId = QStringLiteral("l1");
    for (bool link : {false, true}) {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(link ? editor.selectLink(linkId) : editor.selectNode(a));
        const Json baseline = exported(editor);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        shortcut(editor, Qt::Key_F2);
        QPointer<QPlainTextEdit> input = &topicInput(editor);
        const QString draft = link ? QStringLiteral("Saved #literal link") : QStringLiteral("Saved node #one #two");
        input->setPlainText(draft);
        CHECK(editor.commitActiveEdit(true));
        Json saved = baseline;
        if (link) {
            for (auto &entry : saved.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = utf8(draft);
        } else {
            setTopic(saved, "a", QStringLiteral("Saved node"));
            for (auto &entry : saved.at("nodes")) if (entry.at("id") == "a") entry["tags"] = {"one", "two"};
        }
        CHECK(activeTopicInput(editor) == input.data() && exported(editor) == saved && changed.size() == 1);
        input->moveCursor(QTextCursor::End);
        input->insertPlainText(QStringLiteral(" discard #unsaved"));
        CHECK(editor.hasPendingEdit());
        topicKey(editor, Qt::Key_Escape);
        CHECK(!activeTopicInput(editor) && !editor.hasPendingEdit() && exported(editor) == saved && changed.size() == 1);
        CHECK(editor.undo() && exported(editor) == baseline && !editor.canUndo() && editor.canRedo());
        shortcut(editor, Qt::Key_F2);
        input = &topicInput(editor);
        const auto changes = changed.size();
        // An unchanged retained capture keeps the existing Redo branch and the editing session.
        CHECK(editor.commitActiveEdit(true) && editor.commitActiveEdit(true));
        CHECK(activeTopicInput(editor) == input.data() && input->hasFocus() && !editor.hasPendingEdit());
        CHECK(exported(editor) == baseline && changed.size() == changes && !editor.canUndo() && editor.canRedo());
        topicKey(editor, Qt::Key_Escape);
        CHECK(editor.redo() && exported(editor) == saved && !editor.canRedo());
        CHECK(editor.undo() && exported(editor) == baseline && !editor.canUndo());
    }
    for (bool link : {false, true}) {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.renameNode(b, QStringLiteral("Independent command")));
        const Json committed = exported(editor);
        CHECK(link ? editor.selectLink(linkId) : editor.selectNode(a));
        QSignalSpy changed(&editor, &Editor::documentChanged), pending(&editor, &Editor::pendingEditChanged);
        shortcut(editor, Qt::Key_F2);
        const QString original = topicInput(editor).toPlainText();
        const QString accepted = link ? QStringLiteral("Link #literal") : QStringLiteral("Node #one #two");
        topicInput(editor).selectAll();
        QApplication::clipboard()->setText(accepted);
        QTest::keyClick(&topicInput(editor), Qt::Key_V, Qt::ControlModifier);
        pump();
        CHECK(editor.hasPendingEdit() && topicInput(editor).toPlainText() == accepted);
        topicKey(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(topicInput(editor).toPlainText() == original && !editor.hasPendingEdit());
        topicKey(editor, Qt::Key_Y, Qt::ControlModifier);
        CHECK(topicInput(editor).toPlainText() == accepted && editor.hasPendingEdit());
        CHECK(exported(editor) == committed && changed.isEmpty() && editor.canUndo() && !editor.canRedo());
        CHECK(pending.size() == 3);
        const QTransform zoom = graphics(editor).transform();
        // Undo accepts a pending draft as one command, then undoes that command, not its predecessor.
        CHECK(editor.undo());
        pump();
        CHECK(exported(editor) == committed && changed.size() == 2);
        CHECK(!editor.hasPendingEdit() && !activeTopicInput(editor));
        CHECK(editor.canUndo() && editor.canRedo() && graphics(editor).transform() == zoom);
        CHECK(link ? editor.selectedLinkId() == linkId : editor.selectedNodeId() == a);
        Json acceptedState = committed;
        if (link) {
            for (auto &entry : acceptedState.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = utf8(accepted);
        } else {
            setTopic(acceptedState, "a", QStringLiteral("Node"));
            for (auto &entry : acceptedState.at("nodes")) if (entry.at("id") == "a") entry["tags"] = {"one", "two"};
        }
        CHECK(editor.redo() && exported(editor) == acceptedState && changed.size() == 3);
        CHECK(editor.undo() && exported(editor) == committed);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("Different draft"));
        // Accepting a changed draft is a real branch, so the old redo is no longer applicable.
        CHECK(!editor.redo());
        pump();
        Json replacement = committed;
        if (link) {
            for (auto &entry : replacement.at("crossLinks")) if (entry.at("id") == "l1") entry["topic"] = "Different draft";
        } else setTopic(replacement, "a", QStringLiteral("Different draft"));
        CHECK(exported(editor) == replacement && changed.size() == 5);
        CHECK(!editor.canRedo() && !editor.hasPendingEdit() && !activeTopicInput(editor));
        CHECK(editor.undo() && exported(editor) == committed);
        CHECK(editor.canUndo() && editor.canRedo());
        shortcut(editor, Qt::Key_F2);
        const QString rejected = QStringLiteral("Invalid") + QChar(QChar::Null) + QStringLiteral("draft");
        topicInput(editor).setPlainText(rejected);
        auto cursor = topicInput(editor).textCursor();
        cursor.setPosition(2); cursor.setPosition(5, QTextCursor::KeepAnchor);
        topicInput(editor).setTextCursor(cursor);
        changed.clear(); pending.clear();
        QSignalSpy undoAvailable(&editor, &Editor::undoAvailable), redoAvailable(&editor, &Editor::redoAvailable);
        QSignalSpy errors(&editor, &Editor::errorOccurred);
        const QStringList selectedNodes = editor.selectedNodeIds();
        const QString selectedLink = editor.selectedLinkId();
        CHECK(!editor.undo() && !editor.redo());
        CHECK(topicInput(editor).toPlainText() == rejected && editor.hasPendingEdit());
        CHECK(topicInput(editor).textCursor().anchor() == 2 && topicInput(editor).textCursor().position() == 5);
        CHECK(exported(editor) == committed && changed.isEmpty() && pending.isEmpty());
        CHECK(editor.selectedNodeIds() == selectedNodes && editor.selectedLinkId() == selectedLink);
        CHECK(editor.canUndo() && editor.canRedo() && undoAvailable.isEmpty() && redoAvailable.isEmpty());
        CHECK(!errors.isEmpty() && graphics(editor).transform() == zoom);
        topicKey(editor, Qt::Key_Escape);
        CHECK(!editor.hasPendingEdit() && changed.isEmpty());
        shortcut(editor, Qt::Key_Y, Qt::ControlModifier);
        CHECK(exported(editor) == replacement && changed.size() == 1);
        shortcut(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(exported(editor) == committed && changed.size() == 2);
    }
    {
        // A first inline edit is undoable even when there was no prior document command.
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor);
        CHECK(editor.selectNode(a));
        const Json baseline = exported(editor);
        shortcut(editor, Qt::Key_F2);
        topicInput(editor).setPlainText(QStringLiteral("First draft"));
        CHECK(editor.undo() && exported(editor) == baseline);
        CHECK(!editor.canUndo() && editor.canRedo() && !editor.hasPendingEdit());
        CHECK(editor.redo());
        Json expected = baseline;
        setTopic(expected, "a", QStringLiteral("First draft"));
        CHECK(exported(editor) == expected);
    }
}

static void history_text_scope_case() {
    const QString a = QStringLiteral("a"), b = QStringLiteral("b");
    for (bool multiline : {false, true}) {
        RawLabelEditor editor;
        CHECK(editor.loadJson(encoded(editorFixture())));
        showEditor(editor, QSize(1100, 900));
        const Json baseline = exported(editor);
        CHECK(editor.renameNode(b, QStringLiteral("Independent command")));
        CHECK(editor.selectNode(a));
        expandProperties(editor);
        const Json before = exported(editor);
        auto *panel = editor.findChild<QWidget *>(QStringLiteral("nodePropertiesPanel"));
        CHECK(panel);
        auto *scroll = panel->findChild<QScrollArea *>();
        QWidget *field = multiline ? static_cast<QWidget *>(panel->findChild<QPlainTextEdit *>(QStringLiteral("nodeNote")))
                                   : static_cast<QWidget *>(panel->findChild<QLineEdit *>(QStringLiteral("nodeUrl")));
        CHECK(scroll && field);
        scroll->ensureWidgetVisible(field);
        field->setFocus();
        pump();
        CHECK(field->hasFocus());
        const QString value = multiline ? QStringLiteral("Typed memo\nSecond line") : QStringLiteral("opaque:typed");
        QSignalSpy changed(&editor, &Editor::documentChanged);
        QTest::keyClick(field, Qt::Key_A, Qt::ControlModifier);
        QApplication::clipboard()->setText(value);
        QTest::keyClick(field, Qt::Key_V, Qt::ControlModifier);
        pump();
        Json edited = before;
        for (auto &entry : edited.at("nodes")) if (entry.at("id") == "a")
            entry[multiline ? "note" : "hyperLink"] = utf8(value);
        CHECK(exported(editor) == edited && changed.size() == 1 && !editor.hasPendingEdit());
        QTest::keyClick(field, Qt::Key_Z, Qt::ControlModifier);
        pump();
        CHECK(field->hasFocus() && exported(editor) == before && changed.size() == 2);
        CHECK(editor.canUndo() && !editor.canRedo()); // Local undo is another immediate property edit.
        QTest::keyClick(field, Qt::Key_Y, Qt::ControlModifier);
        pump();
        CHECK(field->hasFocus() && exported(editor) == edited && changed.size() == 3);
        CHECK(editor.canUndo() && !editor.canRedo());
        // Canvas document undo walks all three semantic property changes separately.
        shortcut(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(exported(editor) == before && changed.size() == 4);
        shortcut(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(exported(editor) == edited && changed.size() == 5);
        shortcut(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(exported(editor) == before && changed.size() == 6);
        shortcut(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(exported(editor) == baseline && changed.size() == 7 && !editor.canUndo());
        shortcut(editor, Qt::Key_Y, Qt::ControlModifier);
        CHECK(exported(editor) == before && changed.size() == 8);
    }
    {
        RawLabelConfig config;
        config.shortcuts.undo = {QKeySequence(Qt::CTRL | Qt::Key_9)};
        config.shortcuts.redo = {QKeySequence(Qt::CTRL | Qt::Key_8)};
        RawLabelEditor editor(config);
        showEditor(editor);
        const Json baseline = exported(editor);
        CHECK(editor.renameNode(QStringLiteral("root"), QStringLiteral("Rebound command")));
        const Json edited = exported(editor);
        shortcut(editor, Qt::Key_Z, Qt::ControlModifier);
        CHECK(exported(editor) == edited);
        shortcut(editor, Qt::Key_9, Qt::ControlModifier);
        CHECK(exported(editor) == baseline);
        shortcut(editor, Qt::Key_8, Qt::ControlModifier);
        CHECK(exported(editor) == edited);
        editor.setReadOnly(true);
        QSignalSpy changed(&editor, &Editor::documentChanged);
        CHECK(!editAction(editor, "undo").isEnabled() && !editAction(editor, "redo").isEnabled());
        CHECK(!editor.undo() && !editor.redo());
        shortcut(editor, Qt::Key_9, Qt::ControlModifier);
        editAction(editor, "undo").trigger();
        CHECK(exported(editor) == edited && changed.isEmpty());
        editor.setReadOnly(false);
        trigger(editor, "undo");
        CHECK(exported(editor) == baseline && changed.size() == 1);
        editor.setReadOnly(true);
        CHECK(!editor.undo() && !editor.redo());
        shortcut(editor, Qt::Key_8, Qt::ControlModifier);
        editAction(editor, "redo").trigger();
        CHECK(exported(editor) == baseline && changed.size() == 1);
        editor.setReadOnly(false);
        trigger(editor, "redo");
        CHECK(exported(editor) == edited && changed.size() == 2);
    }
}

static void orphans_case() {
    RawLabelConfig config;
    config.autoRandomBranchColor = false;
    config.confirmSubtreeDeletion = false;
    config.initialVisibleLevels = 2;
    RawLabelEditor editor(config);
    const QString main = QStringLiteral("main"), attached = QStringLiteral("attached");
    const Json input{{"schemaVersion", 1}, {"rootId", "main"}, {"nodes", Json::array({
        {{"id", "main"}, {"topic", "Main tree"}, {"children", {"attached"}}},
        {{"id", "attached"}, {"topic", "Attached"}}
    })}};
    CHECK(editor.loadJson(encoded(input)));
    showEditor(editor);
    const Json initial = exported(editor);
    QSignalSpy changed(&editor, &Editor::documentChanged);
    for (const char *name : {"moveNode", "detachNode", "deleteSelection"})
        CHECK(!editAction(editor, name).isEnabled());
    rejectUnchanged(editor, [&] { return editor.moveNode(main, {}); });
    rejectUnchanged(editor, [&] { return editor.removeNode(main); });

    editor.clearSelection();
    CHECK(editAction(editor, "addOrphan").isEnabled());
    trigger(editor, "addOrphan");
    const QString orphan = editor.selectedNodeId();
    CHECK(!orphan.isEmpty() && orphan != main && changed.size() == 1);
    CHECK(exported(editor).at("orphanIds") == Json::array({utf8(orphan)}));
    CHECK(record(exported(editor), "nodes", orphan).at("topic") == "");
    auto *draft = &topicInput(editor);
    draft->setPlainText(QStringLiteral("Orphan tree"));
    CHECK(editor.hasPendingEdit());
    CHECK(record(exported(editor), "nodes", orphan).at("topic") == "");
    CHECK(editor.commitActiveEdit(true));
    CHECK(activeTopicInput(editor) == draft && !editor.hasPendingEdit() && changed.size() == 2);
    const Json named = exported(editor);
    draft->setPlainText(QStringLiteral("Discard this draft"));
    topicKey(editor, Qt::Key_Escape);
    CHECK(exported(editor) == named && changed.size() == 2);
    CHECK(editAction(editor, "toggleBold").isChecked());
    CHECK(textItem(editor, QStringLiteral("Orphan tree"))->document()->defaultFont().bold());
    CHECK(editAction(editor, "moveNode").isEnabled() && editAction(editor, "deleteSelection").isEnabled());
    CHECK(!editAction(editor, "detachNode").isEnabled());
    CHECK(editor.undo());
    CHECK(record(exported(editor), "nodes", orphan).at("topic") == "");
    CHECK(editor.undo() && exported(editor) == initial && !editor.canUndo());
    CHECK(editor.redo() && editor.redo() && exported(editor) == named);
    CHECK(editor.selectedNodeId() == orphan);

    changed.clear();
    shortcut(editor, Qt::Key_Return); // Enter on any tree root creates a child.
    const QString child = editor.selectedNodeId();
    CHECK(!child.isEmpty() && child != orphan && changed.size() == 1);
    CHECK(record(exported(editor), "nodes", orphan).at("children") == Json::array({utf8(child)}));
    topicInput(editor).setPlainText(QStringLiteral("Orphan child"));
    topicKey(editor, Qt::Key_Return);
    CHECK(changed.size() == 2 && !activeTopicInput(editor));
    const QString leaf = editor.addNode(child, QStringLiteral("Deep orphan leaf"));
    CHECK(!leaf.isEmpty());
    const QString link = editor.addLink(attached, leaf, true, QStringLiteral("Across trees"));
    CHECK(!link.isEmpty());
    CHECK(editor.selectNode(orphan));
    const Json forest = exported(editor);
    changed.clear();
    CHECK(editor.setExpanded(orphan, false));
    CHECK(texts(editor, QStringLiteral("Orphan child")).isEmpty());
    CHECK(texts(editor, QStringLiteral("Deep orphan leaf")).isEmpty());
    CHECK(exported(editor) == forest && changed.isEmpty());
    const auto outline = editor.outline();
    CHECK(outline.size() == 5);
    const QStringList ids{main, attached, orphan, child, leaf};
    const std::vector<int> levels{1, 2, 1, 2, 3};
    for (int i = 0; i < outline.size(); ++i) {
        CHECK(outline[i].id == ids[i] && outline[i].level == levels[i]);
    }
    const QString markdown = editor.toMarkdown();
    const auto heading = [](const QString &id, int level, const QString &topic) {
        return QString(level, QLatin1Char('#')) + QStringLiteral(" <a id=\"m3-node-") +
            QString::fromLatin1(id.toUtf8().toHex()) + QStringLiteral("\"></a>") + topic;
    };
    CHECK(markdown.contains(heading(main, 1, QStringLiteral("Main tree"))));
    CHECK(markdown.contains(heading(orphan, 1, QStringLiteral("Orphan tree"))));
    CHECK(markdown.contains(heading(leaf, 3, QStringLiteral("Deep orphan leaf"))));
    const HtmlPage html(editor.toHtml());
    CHECK(html.values(QStringLiteral("title")) == QStringList{QStringLiteral("Main tree")});
    CHECK(html.tags(QStringLiteral("article")).size() == 5);
    CHECK(html.classes(QStringLiteral("m3-node")).size() == 3);
    CHECK(html.elements[html.node(orphan)].text.contains(QStringLiteral("Orphan tree")));
    CHECK(html.classes(QStringLiteral("m3-cross-link")).empty());
    checkHtmlDocument(html);
    checkHtmlReferences(html, attached, {{leaf, QStringLiteral("Across trees")}});
    checkHtmlReferences(html, leaf, {});
    const auto articles = html.tags(QStringLiteral("article"));
    const int articleSection = html.identified(QStringLiteral("id"), QStringLiteral("articles"));
    CHECK(html.tags(QStringLiteral("h1"), articleSection).size() == 2);
    QStringList articleIds;
    for (qsizetype i = 0; i < ids.size(); ++i) {
        const QString &id = ids[i];
        checkHtmlTitle(html, id, QStringLiteral("h") + QString::number(levels[i]), qs(record(forest, "nodes", id).at("topic")));
        articleIds.append(QStringLiteral("m3-node-") + QString::fromLatin1(id.toUtf8().toHex()));
        CHECK(html.elements[html.article(id)].parent == articleSection);
    }
    CHECK(html.articleIds() == articleIds);
    CHECK(html.tags(QStringLiteral("ul"), articleSection).empty() && html.tags(QStringLiteral("li"), articleSection).empty());
    CHECK(html.elements[articles[4]].text.contains(QStringLiteral("Deep orphan leaf")));
    CHECK(changed.isEmpty() && exported(editor) == forest);
    editor.findText(QStringLiteral("Deep orphan leaf"), Qt::CaseSensitive);
    CHECK(editor.selectedNodeId() == leaf && !texts(editor, QStringLiteral("Deep orphan leaf")).isEmpty());
    CHECK(exported(editor) == forest && changed.isEmpty());
    const HtmlPage revealed(editor.toHtml());
    CHECK(revealed.classes(QStringLiteral("m3-node")).size() == 5);
    CHECK(revealed.articleIds() == html.articleIds());
    CHECK(revealed.classes(QStringLiteral("m3-cross-link")).size() == 1);
    CHECK(revealed.elements[revealed.classes(QStringLiteral("m3-topic"), revealed.node(leaf)).front()].text == QStringLiteral("Deep orphan leaf"));
    CHECK(htmlRect(revealed.elements[revealed.node(orphan)]).top() > htmlRect(revealed.elements[revealed.node(main)]).bottom());

    CHECK(editor.selectNode(child));
    CHECK(editAction(editor, "detachNode").isEnabled());
    trigger(editor, "detachNode");
    const Json detached = exported(editor);
    CHECK(changed.size() == 1 && editor.selectedNodeId() == child);
    CHECK(detached.at("orphanIds") == Json::array({utf8(orphan), utf8(child)}));
    CHECK(record(detached, "nodes", orphan).at("children").empty());
    CHECK(record(detached, "nodes", child).at("children") == Json::array({utf8(leaf)}));
    CHECK(hasRecord(detached, "crossLinks", link));
    CHECK(!editAction(editor, "detachNode").isEnabled());
    CHECK(editor.undo() && exported(editor) == forest && editor.selectedNodeId() == child);
    CHECK(editor.redo() && exported(editor) == detached && editor.selectedNodeId() == child);
    changed.clear();
    dialogs({[&](QDialog *dialog) {
        auto *parent = dialog->findChild<QComboBox *>(QStringLiteral("newParent"));
        CHECK(parent && !pickerIds(*parent).contains(child) && !pickerIds(*parent).contains(leaf));
        CHECK(parent->currentIndex() >= 0);
        chooseId(*parent, orphan);
        dialog->accept();
    }}, [&] { trigger(editor, "moveNode"); });
    CHECK(changed.size() == 1 && exported(editor) == forest && editor.selectedNodeId() == child);
    CHECK(editor.undo() && exported(editor) == detached);
    CHECK(editor.redo() && exported(editor) == forest);

    // Root-to-node dragging reattaches once; blank, descendant and main-root drags do not mutate.
    CHECK(editor.setExpanded(attached, true)); // The initial depth frontier also folds leaves.
    CHECK(editor.selectNode(orphan));
    assertFit(editor);
    auto &view = graphics(editor);
    changed.clear();
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, labelPoint(editor, QStringLiteral("Orphan tree")));
    const QPoint target = labelPoint(editor, QStringLiteral("Attached"));
    movePointer(view, target, Qt::LeftButton);
    CHECK(changed.isEmpty() && exported(editor) == forest);
    CHECK(view.viewport()->cursor().shape() == Qt::DragMoveCursor);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, target);
    pump();
    const Json reattached = exported(editor);
    CHECK(changed.size() == 1 && editor.selectedNodeId() == orphan);
    CHECK(!reattached.contains("orphanIds"));
    CHECK(record(reattached, "nodes", attached).at("children") == Json::array({utf8(orphan)}));
    CHECK(editor.undo() && exported(editor) == forest && editor.selectedNodeId() == orphan);
    CHECK(editor.redo() && exported(editor) == reattached);
    CHECK(editor.undo() && exported(editor) == forest);
    for (int invalid = 0; invalid < 3; ++invalid) {
        CHECK(editor.selectNode(invalid == 2 ? main : orphan));
        assertFit(editor);
        changed.clear();
        const QString source = invalid == 2 ? QStringLiteral("Main tree") : QStringLiteral("Orphan tree");
        QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, labelPoint(editor, source));
        const QPoint destination = invalid == 0 ? blankPoint(view) : labelPoint(editor, QStringLiteral("Orphan child"));
        movePointer(view, destination, Qt::LeftButton);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, destination);
        pump();
        CHECK(exported(editor) == forest && changed.isEmpty());
    }

    CHECK(editor.selectNode(orphan));
    shortcut(editor, Qt::Key_Home);
    CHECK(editor.selectedNodeId() == main && exported(editor) == forest);
    CHECK(editor.selectNode(child));
    editor.setReadOnly(true);
    for (const char *name : {"addOrphan", "detachNode", "moveNode", "deleteSelection", "addChild"}) {
        CHECK(!editAction(editor, name).isEnabled());
        editAction(editor, name).trigger();
    }
    CHECK(editor.addNode({}, QStringLiteral("Blocked")).isEmpty());
    CHECK(!editor.moveNode(child, {}));
    CHECK(!editor.removeNode(orphan));
    CHECK(editor.setExpanded(orphan, false));
    CHECK(editor.selectedNodeId() == orphan);
    CHECK(editor.revealNode(leaf) && editor.selectedNodeId() == leaf);
    CHECK(exported(editor) == forest && changed.isEmpty());
    editor.setReadOnly(false);
    editor.setEnabled(false);
    CHECK(!editAction(editor, "addOrphan").isEnabled() && !editAction(editor, "detachNode").isEnabled());
    editor.setEnabled(true);

    // Save/reopen preserves the ordered forest and resets local folding per tree.
    const QByteArray snapshot = editor.toJson();
    RawLabelEditor reopened(config);
    CHECK(reopened.loadJson(snapshot) && exported(reopened) == forest);
    CHECK(!reopened.canUndo() && !reopened.canRedo());
    CHECK(!texts(reopened, QStringLiteral("Orphan child")).isEmpty());
    CHECK(texts(reopened, QStringLiteral("Deep orphan leaf")).isEmpty());
    CHECK(reopened.outline().size() == 5);
    CHECK(reopened.revealNode(leaf));
    CHECK(!texts(reopened, QStringLiteral("Deep orphan leaf")).isEmpty());
    CHECK(exported(reopened) == forest);

    CHECK(editor.selectNode(orphan));
    CHECK(editor.setExpanded(orphan, false));
    changed.clear();
    trigger(editor, "deleteSelection");
    CHECK(changed.size() == 1 && editor.selectedNodeIds().isEmpty()); // No surviving ancestor.
    CHECK(exported(editor) == initial); // Removing the orphan also removes its incident cross-link.
    CHECK(editor.undo() && exported(editor) == forest && editor.selectedNodeId() == orphan);
    CHECK(!texts(editor, QStringLiteral("Deep orphan leaf")).isEmpty()); // Reintroduced IDs start expanded.
    CHECK(editor.redo() && exported(editor) == initial);

    // A semantic observer may synchronously destroy either action's editor.
    for (bool detach : {false, true}) {
        QPointer<Editor> closing = new RawLabelEditor(config);
        QString id;
        if (detach) {
            id = closing->addNode(QStringLiteral("root"), QStringLiteral("Detach before close"));
            CHECK(!id.isEmpty() && closing->selectNode(id));
        }
        auto *action = closing->commandAction(detach ? QStringLiteral("detachNode") : QStringLiteral("addOrphan"));
        CHECK(action && action->isEnabled());
        Json committed;
        QObject::connect(closing.data(), &Editor::documentChanged, qApp, [&] {
            committed = exported(*closing);
            delete closing.data();
        });
        action->trigger();
        CHECK(!closing && committed.at("orphanIds").size() == 1);
        if (detach) CHECK(committed.at("orphanIds").front() == utf8(id));
    }
}

static void history_case() {
    history_commands_case();
    history_link_gestures_case();
    history_availability_case();
    history_limits_case();
    history_drafts_case();
    history_text_scope_case();
}

int main(int argc, char **argv) {
    try {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
#ifdef Q_OS_WIN
        if (qgetenv("QT_QPA_PLATFORM") == QByteArray("offscreen") && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
            qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + QByteArray("/Fonts"));
#endif
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QApplication application(argc, argv);
        QApplication::setQuitOnLastWindowClosed(false);
        CHECK(argc == 2);
        const std::string name = argv[1];
        if (name == "html") html_case();
        else if (name == "html_edges") html_edges_case();
        else if (name == "markdown") markdown_case();
        else if (name == "document") document_case();
        else if (name == "tree_edits") { tree_edits_case(); tree_edits_scene(); }
        else if (name == "orphans") orphans_case();
        else if (name == "collapse") { collapse_case(); collapse_scene(); }
        else if (name == "graph_edits") { graph_edits_case(); graph_edits_scene(); }
        else if (name == "images") { images_case(); image_activation_case(); }
        else if (name == "render") { render_case(); stroke_contrast_case(); routing_case(); }
        else if (name == "navigation") navigation_case();
        else if (name == "outline_navigation") outline_navigation_case();
        else if (name == "find") find_case();
        else if (name == "multi_selection") multi_selection_case();
        else if (name == "node_drag") node_drag_case();
        else if (name == "properties") properties_case();
        else if (name == "formatting_toolbar") formatting_toolbar_case();
        else if (name == "branch_color") { branch_color_case(); auto_branch_color_case(); }
        else if (name == "clipboard") clipboard_case();
        else if (name == "hyperlinks") hyperlinks_case();
        else if (name == "controls") controls_case();
        else if (name == "inline_edit") inline_edit_case();
        else if (name == "host_commit") host_commit_case();
        else if (name == "read_only") read_only_case();
        else if (name == "history") history_case();
        else if (name == "link_interaction") link_interaction_case();
        else if (name == "configuration") configuration_case();
        else if (name == "shortcut_help") shortcut_help_case();
        else if (name == "shortcuts") shortcuts_case();
        else if (name == "node_shortcuts") node_shortcuts_case();
        else if (name == "lifetime") lifetime_case();
#ifdef M3_QT_TEST_DEMO
        else if (name == "demo_controls") demo_controls_case();
        else if (name == "demo_images") demo_images_case();
        else if (name == "demo_html") demo_html_case();
        else if (name == "demo_markdown") demo_markdown_case();
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
