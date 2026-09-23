#ifndef M3_QT_DEMO_WINDOW_H
#define M3_QT_DEMO_WINDOW_H
#include "m3/qt/editor.h"
#include <QMainWindow>
class QNetworkAccessManager;
class DemoWindow : public QMainWindow {
public:
    explicit DemoWindow(const m3::qt::EditorConfig &config = {}, QWidget *parent = nullptr);
    bool newFile();
    bool openFile(const QString &path);
    bool saveFile(const QString &path);
    bool exportMarkdownFile(const QString &path);
    bool exportHtmlFile(const QString &path);
    QString currentFilePath() const;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    m3::qt::MindMapEditor *editor;
    QNetworkAccessManager *imageNetwork;
    QString filename;
    void loadImage(const QString &url, quint64 requestId);
    bool mayReplace();
    bool save();
    bool saveAs();
    bool exportMarkdown();
    bool exportHtml();
    void updateTitle();
    bool report(const QString &message);
};
#endif
