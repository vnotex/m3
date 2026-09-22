#ifndef M3_QT_DEMO_WINDOW_H
#define M3_QT_DEMO_WINDOW_H
#include <QMainWindow>
namespace m3::qt { class MindMapEditor; }
class DemoWindow : public QMainWindow {
public:
    explicit DemoWindow(QWidget *parent = nullptr);
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
    QString filename;
    bool mayReplace();
    bool save();
    bool saveAs();
    bool exportMarkdown();
    bool exportHtml();
    void updateTitle();
    bool report(const QString &message);
};
#endif
