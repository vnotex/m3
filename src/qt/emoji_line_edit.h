#ifndef M3_QT_EMOJI_LINE_EDIT_H
#define M3_QT_EMOJI_LINE_EDIT_H
#include <QCoreApplication>
#include <QLineEdit>

class QAbstractItemModel;
class QFrame;
class QLabel;
class QListView;
class QComboBox;

namespace m3::qt {
// Completes the comma-separated entry at the caret without changing line-edit semantics.
class EmojiLineEdit final : public QLineEdit {
    Q_DECLARE_TR_FUNCTIONS(m3::qt::EmojiLineEdit)
public:
    enum class PickerMode { Floating, Embedded };
    explicit EmojiLineEdit(QWidget *parent = nullptr, PickerMode mode = PickerMode::Floating);
    QWidget *pickerWidget() const;
    ~EmojiLineEdit() override;
    void dismissPopup();
protected:
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
private:
    const PickerMode mode;
    QFrame *popup = nullptr;
    QComboBox *categories = nullptr;
    QListView *choices = nullptr;
    QLabel *description = nullptr;
    QAbstractItemModel *emojiModel = nullptr;
    QString query, navigationHintText;
    bool inserting = false, focusCheckPending = false;
    void createPopup();
    void syncAppearance();
    void showPopup(bool all = false);
    void scheduleNavigationHint();
    bool positionPopup();
    void filterMatches();
    bool ownsPopupWidget(QWidget *widget) const;
    void queuePopupFocusCheck();
    void chooseCurrent();
    QPair<int, int> tokenRange() const;
};
}
#endif
