#include "qt_demo_window.h"
#include <QAction>
#include <QBuffer>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageReader>
#include <QMenuBar>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace {
constexpr qint64 imageByteLimit = 16 * 1024 * 1024;
constexpr int imagePixelLimit = 16777216;

QImage decodeImage(const QByteArray &bytes) {
    if (bytes.isEmpty() || bytes.size() > imageByteLimit) return {};
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly)) return {};
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.width() <= 0 || size.height() <= 0 || size.width() > imagePixelLimit / size.height()) return {};
    return reader.read();
}
}

DemoWindow::DemoWindow(const m3::qt::EditorConfig &config, QWidget *parent)
    : QMainWindow(parent), editor(new m3::qt::MindMapEditor(config, this)),
      imageNetwork(new QNetworkAccessManager(this)) {
    setCentralWidget(editor);
    connect(editor, &m3::qt::MindMapEditor::imageRequested, this, &DemoWindow::loadImage);
    resize(1100, 750);
    auto *file = menuBar()->addMenu(tr("&File"));
    auto *create = file->addAction(tr("&New")); create->setShortcut(QKeySequence::New);
    auto *open = file->addAction(tr("&Open...")); open->setShortcut(QKeySequence::Open);
    auto *saveAction = file->addAction(tr("&Save")); saveAction->setShortcut(QKeySequence::Save);
    auto *saveAsAction = file->addAction(tr("Save &As...")); saveAsAction->setShortcut(QKeySequence::SaveAs);
    file->addSeparator();
    auto *markdownAction = file->addAction(tr("Export &Markdown..."));
    connect(markdownAction, &QAction::triggered, this, [this] { exportMarkdown(); });
    auto *htmlAction = file->addAction(tr("Export &HTML..."));
    connect(htmlAction, &QAction::triggered, this, [this] { exportHtml(); });
    connect(create, &QAction::triggered, this, [this] { newFile(); });
    connect(open, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Open mind map"), filename, tr("JSON files (*.json);;All files (*)"));
        if (!path.isEmpty()) openFile(path);
    });
    connect(saveAction, &QAction::triggered, this, [this] { save(); });
    connect(saveAsAction, &QAction::triggered, this, [this] { saveAs(); });
    connect(editor, &m3::qt::MindMapEditor::documentChanged, this, [this] { setWindowModified(true); });
    connect(editor, &m3::qt::MindMapEditor::selectionChanged, this, [this](const QString &node, const QString &link) {
        statusBar()->showMessage(node.isEmpty() ? (link.isEmpty() ? tr("No selection") : tr("Link: %1").arg(link)) : tr("Node: %1").arg(node));
    });
    connect(editor, &m3::qt::MindMapEditor::errorOccurred, this, [this](const QString &message) { report(message); });
    editor->loadJson(QByteArray(R"({"schemaVersion":1,"rootId":"r","nodes":[
        {"id":"r","topic":"M3 Qt editor","children":["a","b","c"]},
        {"id":"a","topic":"Planning 世界","children":["d"],"note":"Preserve this note","style":{"custom":{"weight":2}}},
        {"id":"b","topic":"Implementation"},{"id":"c","topic":""},{"id":"d","topic":"Tests & verification"}],
        "crossLinks":[{"id":"l1","source":"a","target":"b","directed":true,"topic":"Depends on"},
        {"id":"l2","source":"d","target":"c","directed":false,"topic":"Related"},
        {"id":"l3","source":"a","target":"a","directed":true,"topic":"Review"},
        {"id":"l4","source":"a","target":"b","directed":true,"topic":"Feedback"}]})"));
    setWindowModified(false);
    updateTitle();
}
void DemoWindow::loadImage(const QString &url, quint64 requestId) {
    const QUrl source(url, QUrl::StrictMode);
    if (!source.isValid() || source.isRelative()) {
        editor->provideImage(url, requestId, {});
        return;
    }
    if (source.isLocalFile()) {
        QFile input(source.toLocalFile());
        if (!QFileInfo(input).isAbsolute() || !input.open(QIODevice::ReadOnly) || input.size() > imageByteLimit) {
            editor->provideImage(url, requestId, {});
            return;
        }
        const QByteArray bytes = input.read(qMin(input.size(), imageByteLimit));
        const QImage image = input.error() == QFileDevice::NoError && input.atEnd() ? decodeImage(bytes) : QImage();
        editor->provideImage(url, requestId, image);
        return;
    }
    if (source.scheme() != QStringLiteral("http") && source.scheme() != QStringLiteral("https")) {
        editor->provideImage(url, requestId, {});
        return;
    }
    QNetworkRequest request(source);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    request.setTransferTimeout(15000);
    auto *reply = imageNetwork->get(request);
    reply->setReadBufferSize(64 * 1024);
    struct Download {
        QByteArray bytes;
        bool rejected = false;
    };
    const auto download = std::make_shared<Download>();
    const auto receive = [reply, download] {
        if (download->rejected || reply->error() != QNetworkReply::NoError) return;
        const qint64 length = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const qint64 originalLength = reply->attribute(QNetworkRequest::OriginalContentLengthAttribute).toLongLong();
        const qint64 available = reply->bytesAvailable();
        if (length > imageByteLimit || originalLength > imageByteLimit || available > imageByteLimit - download->bytes.size()) {
            download->rejected = true;
            if (!reply->isFinished()) reply->abort();
            return;
        }
        if (available > 0) download->bytes.append(reply->read(available));
    };
    connect(reply, &QNetworkReply::metaDataChanged, reply, receive);
    connect(reply, &QNetworkReply::readyRead, reply, receive);
    connect(reply, &QNetworkReply::finished, editor, [this, reply, download, receive, url, requestId] {
        receive();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QImage image = !download->rejected && reply->error() == QNetworkReply::NoError && status >= 200 && status < 300
            ? decodeImage(download->bytes) : QImage();
        reply->deleteLater();
        editor->provideImage(url, requestId, image);
    });
    // Bound the whole request as well as periods with no transfer activity.
    QTimer::singleShot(15000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
}
bool DemoWindow::report(const QString &message) {
    statusBar()->showMessage(message);
    return false;
}
void DemoWindow::updateTitle() {
    setWindowTitle(tr("M3 Qt editor demo — %1[*]").arg(filename.isEmpty() ? tr("Untitled") : QFileInfo(filename).fileName()));
}
QString DemoWindow::currentFilePath() const { return filename; }
bool DemoWindow::mayReplace() {
    if (!isWindowModified()) return true;
    const auto response = QMessageBox::warning(this, tr("Unsaved changes"), tr("Save changes before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (response == QMessageBox::Save) return save();
    return response == QMessageBox::Discard;
}
bool DemoWindow::newFile() {
    if (!mayReplace() || !editor->newDocument()) return false;
    filename.clear(); setWindowModified(false); updateTitle();
    return true;
}
bool DemoWindow::openFile(const QString &path) {
    if (path.isEmpty()) return report(tr("An input path is required"));
    if (!mayReplace()) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) return report(input.errorString());
    const QByteArray bytes = input.readAll();
    if (input.error() != QFileDevice::NoError) return report(input.errorString());
    if (!editor->loadJson(bytes)) return false;
    filename = QFileInfo(path).absoluteFilePath();
    setWindowModified(false); updateTitle();
    return true;
}
bool DemoWindow::saveFile(const QString &path) {
    if (path.isEmpty()) return report(tr("An output path is required"));
    const QByteArray bytes = editor->toJson();
    if (bytes.isEmpty()) return report(editor->lastError());
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return report(output.errorString());
    if (output.write(bytes) != bytes.size()) { output.cancelWriting(); return report(output.errorString()); }
    if (!output.commit()) return report(output.errorString());
    filename = QFileInfo(path).absoluteFilePath();
    setWindowModified(false); updateTitle();
    statusBar()->showMessage(tr("Saved %1").arg(filename));
    return true;
}
bool DemoWindow::exportMarkdownFile(const QString &path) {
    if (path.isEmpty()) return report(tr("An output path is required"));
    const QString markdown = editor->toMarkdown();
    if (markdown.isEmpty()) return report(editor->lastError());
    const QByteArray bytes = markdown.toUtf8();
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return report(output.errorString());
    if (output.write(bytes) != bytes.size()) { output.cancelWriting(); return report(output.errorString()); }
    if (!output.commit()) return report(output.errorString());
    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).absoluteFilePath()));
    return true;
}
bool DemoWindow::exportMarkdown() {
    QFileDialog dialog(this, tr("Export mind map as Markdown"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(tr("Markdown files (*.md)"));
    dialog.setDefaultSuffix(QStringLiteral("md"));
    if (!filename.isEmpty()) {
        const QFileInfo current(filename);
        dialog.setDirectory(current.absolutePath());
        dialog.selectFile(current.completeBaseName() + QStringLiteral(".md"));
    } else {
        dialog.selectFile(QStringLiteral("Untitled.md"));
    }
    if (dialog.exec() != QDialog::Accepted) return false;
    const QStringList paths = dialog.selectedFiles();
    return !paths.isEmpty() && !paths.front().isEmpty() && exportMarkdownFile(paths.front());
}
bool DemoWindow::exportHtmlFile(const QString &path) {
    if (path.isEmpty()) return report(tr("An output path is required"));
    const QString html = editor->toHtml();
    if (html.isEmpty()) return report(editor->lastError());
    const QByteArray bytes = html.toUtf8();
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return report(output.errorString());
    if (output.write(bytes) != bytes.size()) { output.cancelWriting(); return report(output.errorString()); }
    if (!output.commit()) return report(output.errorString());
    statusBar()->showMessage(tr("Exported %1").arg(QFileInfo(path).absoluteFilePath()));
    return true;
}
bool DemoWindow::exportHtml() {
    QFileDialog dialog(this, tr("Export mind map as HTML"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilter(tr("HTML files (*.html *.htm)"));
    dialog.setDefaultSuffix(QStringLiteral("html"));
    if (!filename.isEmpty()) {
        const QFileInfo current(filename);
        dialog.setDirectory(current.absolutePath());
        dialog.selectFile(current.completeBaseName() + QStringLiteral(".html"));
    } else {
        dialog.selectFile(QStringLiteral("Untitled.html"));
    }
    if (dialog.exec() != QDialog::Accepted) return false;
    const QStringList paths = dialog.selectedFiles();
    return !paths.isEmpty() && !paths.front().isEmpty() && exportHtmlFile(paths.front());
}
bool DemoWindow::save() { return filename.isEmpty() ? saveAs() : saveFile(filename); }
bool DemoWindow::saveAs() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save mind map"), filename, tr("JSON files (*.json);;All files (*)"));
    return !path.isEmpty() && saveFile(path);
}
void DemoWindow::closeEvent(QCloseEvent *event) {
    if (mayReplace()) event->accept(); else event->ignore();
}
