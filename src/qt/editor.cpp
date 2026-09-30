#include "m3/qt/editor.h"
#include "mindmap_controller.h"
#include "mindmap_view.h"
#include "emoji_line_edit.h"
#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFrame>
#include <QFormLayout>
#include <QGridLayout>
#include <QIconEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QResource>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <functional>
#include <QTextBrowser>
#include <QToolButton>
#include <QToolBar>
#include <QTimer>
#include <QLabel>
#include <QVBoxLayout>
#include <QUrl>
#include <QWindow>
#include <QWidgetAction>

static void initializeM3Resources() {
    Q_INIT_RESOURCE(m3_resources);
}

namespace m3::qt {
namespace {
QString loadStyleSheet(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot load embedded stylesheet %s: %s", qPrintable(path), qPrintable(file.errorString()));
    return QString::fromUtf8(file.readAll());
}
// Lucide: https://lucide.dev/icons/rotate-ccw; notice: third_party/lucide/LICENSE.
class RotateCcwIconEngine final : public QIconEngine {
public:
    explicit RotateCcwIconEngine(const QPalette &palette) : palette(palette) {}
    QIconEngine *clone() const override { return new RotateCcwIconEngine(*this); }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        if (rect.isEmpty()) return;
        static const QPainterPath glyph = [] {
            QPainterPath path;
            path.moveTo(3, 12);
            path.arcTo(QRectF(3, 3, 18, 18), 180, 270);
            path.arcTo(QRectF(2.2866781853, 2.9999310106, 19.5, 19.5),
                       90.2155395051, 43.8151773805);
            path.lineTo(3, 8);
            path.moveTo(3, 3);
            path.lineTo(3, 8);
            path.lineTo(8, 8);
            return path;
        }();
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : palette.currentColorGroup();
        const auto role = mode == QIcon::Selected ? QPalette::HighlightedText : QPalette::ButtonText;
        const qreal side = qMin(rect.width(), rect.height());
        painter->save();
        painter->translate(rect.x() + (rect.width() - side) / 2, rect.y() + (rect.height() - side) / 2);
        painter->scale(side / 24, side / 24);
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(palette.color(group, role), 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(glyph);
        painter->restore();
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        if (size.isEmpty()) return QPixmap();
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        painter.end();
        return result;
    }
private:
    QPalette palette;
};
class ShortcutHelpPopup final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(m3::qt::ShortcutHelpPopup)
public:
    ShortcutHelpPopup(MindMapEditor *host, MindMapView *canvas, const QString &help)
        : QDialog(host, Qt::Popup), view(canvas) {
        setObjectName(QStringLiteral("shortcutHelpPopup"));
        setWindowTitle(tr("Keyboard shortcuts"));
        auto *layout = new QVBoxLayout(this);
        auto *browser = new QTextBrowser(this);
        browser->setObjectName(QStringLiteral("shortcutHelpBrowser"));
        browser->setAccessibleName(tr("Keyboard shortcuts"));
        browser->setLineWrapMode(QTextEdit::NoWrap);
        browser->setHtml(help);
        layout->addWidget(browser, 1);
        layout->addWidget(new QLabel(tr("%1 to close").arg(QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText)), this));
        setFocusProxy(browser);
    }
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->matches(QKeySequence::Cancel)) {
            const QPointer<MindMapView> canvas = view;
            event->accept();
            reject();
            if (canvas && canvas->window()->isActiveWindow()) canvas->setFocus(Qt::OtherFocusReason);
            return;
        }
        QDialog::keyPressEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (!rect().contains(event->pos())) setAttribute(Qt::WA_NoMouseReplay);
        QDialog::mousePressEvent(event);
    }
private:
    QPointer<MindMapView> view;
};
QString propertiesText(const char *text) {
    return QCoreApplication::translate("m3::qt::NodePropertiesPanel", text);
}
constexpr int fontSizePresetCount = 13;
void populateFontSizes(QComboBox *combo) {
    combo->setEditable(false);
    combo->setAccessibleName(propertiesText("Font size"));
    combo->addItem(propertiesText("Default"), 0.0);
    for (const int size : {10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64})
        combo->addItem(propertiesText("%1 px").arg(size), double(size));
}
void syncFontSize(QComboBox *combo, qreal size) {
    const QSignalBlocker blocker(combo);
    int index = combo->findData(double(size));
    if (index < 0) {
        if (combo->count() > fontSizePresetCount) combo->removeItem(fontSizePresetCount);
        combo->addItem(propertiesText("%1 px").arg(QString::number(size, 'g', 6)), double(size));
        index = fontSizePresetCount;
    } else if (index < fontSizePresetCount && combo->count() > fontSizePresetCount) {
        combo->removeItem(fontSizePresetCount);
    }
    combo->setCurrentIndex(index);
}
class FontSizeAction final : public QWidgetAction {
public:
    FontSizeAction(QObject *parent, std::function<void(qreal)> apply)
        : QWidgetAction(parent), apply(std::move(apply)) {
        setObjectName(QStringLiteral("fontSize"));
        setText(propertiesText("Font size"));
        setToolTip(text());
        connect(this, &QAction::changed, this, [this] {
            if (!isEnabled()) hidePopups();
        });
    }
    void synchronize(qreal size) {
        currentSize = size;
        for (auto *widget : createdWidgets()) syncFontSize(static_cast<QComboBox *>(widget), size);
    }
    void hidePopups() {
        for (auto *widget : createdWidgets()) static_cast<QComboBox *>(widget)->hidePopup();
    }
protected:
    QWidget *createWidget(QWidget *parent) override {
        auto *combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("toolbarFontSize"));
        populateFontSizes(combo);
        syncFontSize(combo, currentSize);
        combo->setToolTip(text());
        combo->setEnabled(isEnabled());
        combo->installEventFilter(this);
        connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo](int index) {
            if (isEnabled()) apply(combo->itemData(index).toDouble());
        });
        return combo;
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Hide || event->type() == QEvent::EnabledChange) {
            if (auto *combo = qobject_cast<QComboBox *>(watched)) combo->hidePopup();
        }
        return QWidgetAction::eventFilter(watched, event);
    }
private:
    std::function<void(qreal)> apply;
    qreal currentSize = 0;
};
class NodeColorPalette final : public QWidget {
public:
    NodeColorPalette(QWidget *parent, const QString &namePrefix, std::function<void(const QJsonValue &)> apply)
        : QWidget(parent), apply(std::move(apply)) {
        auto *layout = new QGridLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        static const QString sheet = loadStyleSheet(QStringLiteral(":/m3/qt/color_swatch.qss"));
        auto makeSwatch = [&](const QString &background, const QString &contrast) {
            auto *button = new QToolButton(this);
            button->setCheckable(true);
            button->setFocusPolicy(Qt::StrongFocus);
            button->setMinimumWidth(24);
            button->setFixedHeight(26);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setToolButtonStyle(Qt::ToolButtonTextOnly);
            button->setStyleSheet(sheet.arg(background, contrast));
            const int index = swatches.size();
            layout->addWidget(button, index / 6, index % 6);
            swatches.append(button);
            connect(button, &QToolButton::clicked, this, [this, index] { activateIndex(index); });
            return button;
        };
        auto *automatic = makeSwatch(QStringLiteral("palette(button)"), QStringLiteral("palette(button-text)"));
        automatic->setObjectName(namePrefix + QStringLiteral("nodeDefaultColor"));
        automatic->setText(propertiesText("Auto"));
        automatic->setAccessibleName(propertiesText("Default color"));
        for (const auto &entry : nodeColors) {
            const QString hex = QString::fromLatin1(entry.hex);
            const QString label = propertiesText("%1 (%2)").arg(QCoreApplication::translate("m3::qt::NodeColors", entry.name), hex);
            const QString contrast = QColor(hex).lightnessF() > 0.55 ? QStringLiteral("#202020") : QStringLiteral("#ffffff");
            const int index = swatches.size();
            auto *button = makeSwatch(hex, contrast);
            button->setObjectName(namePrefix + QStringLiteral("nodeColor_") + hex.mid(1));
            // Preserve the card's editor-wide palette enumeration contract.
            if (namePrefix.isEmpty()) button->setProperty("color", hex);
            button->setAccessibleName(label);
            button->setToolTip(label);
            button->setText(QString::number((index / 6 + 1) * 10 + index % 6 + 1));
        }
    }
    void setColor(const QColor &color, const QString &autoToolTip) {
        currentColor = color;
        swatches.front()->setToolTip(autoToolTip);
        syncChecks();
    }
    void activateIndex(int index) {
        if (!isEnabled() || index < 0 || index >= swatches.size()) return;
        const QJsonValue value = index == 0 ? QJsonValue(QJsonValue::Null)
            : QJsonValue(QString::fromLatin1(nodeColors[index - 1].hex));
        apply(value);
    }
private:
    QList<QToolButton *> swatches;
    QColor currentColor;
    std::function<void(const QJsonValue &)> apply;
    void syncChecks() {
        for (int index = 0; index < swatches.size(); ++index) {
            const QSignalBlocker blocker(swatches[index]);
            swatches[index]->setChecked(index == 0 ? !currentColor.isValid()
                : currentColor == QColor(QString::fromLatin1(nodeColors[index - 1].hex)));
        }
    }
};
class ColorMenu final : public QMenu {
public:
    explicit ColorMenu(QWidget *parent) : QMenu(parent) {
        connect(this, &QMenu::aboutToHide, this, [this] { pendingRow = 0; });
        connect(qApp, &QApplication::focusChanged, this, [this] { pendingRow = 0; });
    }
    NodeColorPalette *palette = nullptr;
protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::ShortcutOverride) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if ((key->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier &&
                key->key() >= Qt::Key_0 && key->key() <= Qt::Key_9) {
                event->accept();
                return true;
            }
        }
        return QMenu::event(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        const int digit = event->key() - Qt::Key_0;
        if ((event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && digit >= 0 && digit <= 9) {
            event->accept();
            if (!palette || !palette->isEnabled() || event->isAutoRepeat()) return;
            if (!pendingRow) {
                if (digit >= 1 && digit <= 4) pendingRow = digit;
            } else {
                const int row = pendingRow;
                pendingRow = 0;
                if (digit >= 1 && digit <= 6) palette->activateIndex((row - 1) * 6 + digit - 1);
            }
            return;
        }
        pendingRow = 0;
        QMenu::keyPressEvent(event);
    }
private:
    int pendingRow = 0;
};
class IconsMenu final : public QMenu {
public:
    explicit IconsMenu(QWidget *parent) : QMenu(parent) {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS) || defined(Q_OS_MAC)
        // Keep an independent input focus window: the toolbar extension can
        // reclaim Windows popup keyboard routing while this submenu remains visible.
        setWindowFlags(Qt::Tool | Qt::NoDropShadowWindowHint);
        setAttribute(Qt::WA_ShowWithoutActivating, false);
        connect(this, &QMenu::aboutToShow, this, [this] {
            previousModality = windowModality();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
            setWindowModality(Qt::ApplicationModal);
#else
            // Windows suppresses native presses on disabled modal owners before Qt
            // can see them. Keep the owner enabled and consume outside presses below.
            setWindowModality(Qt::NonModal);
#endif
            qApp->installEventFilter(this);
        });
        connect(this, &QMenu::aboutToHide, this, [this] {
            qApp->removeEventFilter(this);
            setWindowModality(previousModality);
        });
#endif
    }
    QPointer<EmojiLineEdit> input;
    void boundPickerSize() {
        if (!input) return;
        const QSize available = screen()->availableGeometry().size();
        input->pickerWidget()->setFixedSize(qMax(1, qMin(420, available.width() - 24)),
            qMax(1, qMin(360, available.height() - input->sizeHint().height() - 32)));
    }
protected:
    void showEvent(QShowEvent *event) override {
        QMenu::showEvent(event);
        if ((windowFlags() & Qt::WindowType_Mask) == Qt::Tool) {
            QTimer::singleShot(0, this, [this] {
                if (!isVisible() || !input || !input->isEnabled()) return;
                const QPointer<IconsMenu> guard(this);
                const QRect available = screen()->availableGeometry();
                move(qBound(available.left(), x(), qMax(available.left(), available.right() - width() + 1)),
                     qBound(available.top(), y(), qMax(available.top(), available.bottom() - height() + 1)));
                if (!guard) return;
                raise();
                if (!guard) return;
                activateWindow();
                if (guard && input) input->setFocus(Qt::PopupFocusReason);
            });
        } else if (input && input->isEnabled()) input->setFocus(Qt::PopupFocusReason);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            // The input gets first refusal; otherwise Enter must not close a menu.
            event->accept();
            return;
        }
        QMenu::keyPressEvent(event);
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (isVisible() && (windowFlags() & Qt::WindowType_Mask) == Qt::Tool) {
            if (event->type() == QEvent::ApplicationDeactivate) hide();
            else if (event->type() == QEvent::MouseButtonPress) {
                // Inspect native windows before modal filtering suppresses outside
                // QWidget delivery. The category dropdown remains an owned child.
                auto *widget = qobject_cast<QWidget *>(watched);
                if (auto *window = qobject_cast<QWindow *>(watched)) widget = QWidget::find(window->winId());
                // QWidget::isAncestorOf stops at window boundaries; a combo popup
                // is a window, but its parentWidget chain still belongs to this menu.
                auto *owner = widget;
                while (owner && owner != this) owner = owner->parentWidget();
                if (widget && !owner) {
                    hide();
                    return true;
                }
            }
        }
        return false;
    }
private:
    Qt::WindowModality previousModality = Qt::NonModal;
};
QStringList commaValues(const QString &text) {
    QStringList values;
    for (const auto &part : text.split(QLatin1Char(','))) {
        const QString value = part.trimmed();
        if (!value.isEmpty()) values.append(value);
    }
    return values;
}
class NodePropertiesPanel final : public QFrame {
    Q_DECLARE_TR_FUNCTIONS(m3::qt::NodePropertiesPanel)
public:
    NodePropertiesPanel(MindMapEditor *host, MindMapView *canvas, MindMapController *model)
        : QFrame(host), view(canvas), controller(model) {
        setObjectName(QStringLiteral("nodePropertiesPanel"));
        setAccessibleName(tr("Node properties"));
        static const QString panelStyleSheet = loadStyleSheet(QStringLiteral(":/m3/qt/node_properties.qss"));
        setStyleSheet(panelStyleSheet);
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->setSizeConstraint(QLayout::SetNoConstraint);
        heading = new QWidget(this);
        auto *header = new QVBoxLayout(heading);
        header->setContentsMargins(12, 8, 8, 9);
        header->setSpacing(2);
        auto *titleRow = new QHBoxLayout;
        title = new QLabel(tr("Node properties"), heading);
        title->setObjectName(QStringLiteral("nodePropertiesTitle"));
        title->setTextFormat(Qt::PlainText);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        toggle = new QToolButton(heading);
        toggle->setObjectName(QStringLiteral("nodePropertiesToggle"));
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setFixedSize(28, 28);
        toggle->setAutoRaise(true);
        toggle->setFocusPolicy(Qt::StrongFocus);
        titleRow->addWidget(title, 1);
        titleRow->addWidget(toggle);
        header->addLayout(titleRow);
        subtitle = new QLabel(heading);
        subtitle->setTextFormat(Qt::PlainText);
        subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        subtitle->setFixedHeight(subtitle->fontMetrics().height());
        header->addWidget(subtitle);
        layout->addWidget(heading);
        scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setMinimumSize(0, 0);
        body = new QWidget(scroll);
        auto *content = new QVBoxLayout(body);
        content->setContentsMargins(12, 8, 12, 12);
        content->setSpacing(8);
        auto section = [this, content](const QString &text) {
            auto *label = new QLabel(text, body);
            auto font = label->font();
            font.setBold(true);
            label->setFont(font);
            content->addWidget(label);
        };
        section(tr("Appearance"));
        auto *fontRow = new QHBoxLayout;
        auto *sizeLabel = new QLabel(tr("&Size"), body);
        fontSize = new QComboBox(body);
        fontSize->setObjectName(QStringLiteral("nodeFontSize"));
        populateFontSizes(fontSize);
        sizeLabel->setBuddy(fontSize);
        bold = new QToolButton(body);
        bold->setObjectName(QStringLiteral("nodeBold"));
        bold->setText(QStringLiteral("B"));
        bold->setToolButtonStyle(Qt::ToolButtonTextOnly);
        bold->setAccessibleName(tr("Bold"));
        bold->setCheckable(true);
        bold->setFocusPolicy(Qt::StrongFocus);
        auto boldFont = bold->font();
        boldFont.setBold(true);
        bold->setFont(boldFont);
        italic = new QToolButton(body);
        italic->setObjectName(QStringLiteral("nodeItalic"));
        italic->setText(QStringLiteral("I"));
        italic->setToolButtonStyle(Qt::ToolButtonTextOnly);
        italic->setAccessibleName(tr("Italic"));
        italic->setCheckable(true);
        italic->setFocusPolicy(Qt::StrongFocus);
        auto italicFont = italic->font();
        italicFont.setItalic(true);
        italic->setFont(italicFont);
        reset = new QToolButton(body);
        reset->setObjectName(QStringLiteral("nodeResetAppearance"));
        reset->setText(tr("Reset appearance"));
        reset->setAccessibleName(tr("Reset appearance"));
        reset->setToolTip(tr("Restore default font, text color and fill"));
        reset->setToolButtonStyle(Qt::ToolButtonIconOnly);
        reset->setFocusPolicy(Qt::StrongFocus);
        reset->setIconSize(QSize(16, 16));
        reset->setIcon(QIcon(new RotateCcwIconEngine(reset->palette())));
        reset->installEventFilter(this);
        fontRow->addWidget(sizeLabel);
        fontRow->addWidget(fontSize, 1);
        fontRow->addWidget(bold);
        fontRow->addWidget(italic);
        fontRow->addWidget(reset);
        content->addLayout(fontRow);
        auto *modeRow = new QHBoxLayout;
        auto *modes = new QButtonGroup(this);
        textColor = new QToolButton(body);
        fillColor = new QToolButton(body);
        textColor->setObjectName(QStringLiteral("nodeTextColor"));
        fillColor->setObjectName(QStringLiteral("nodeFillColor"));
        textColor->setText(tr("Text"));
        fillColor->setText(tr("Fill"));
        textColor->setAccessibleName(tr("Text color"));
        fillColor->setAccessibleName(tr("Fill color"));
        for (auto *button : {textColor, fillColor}) {
            button->setCheckable(true);
            button->setFocusPolicy(Qt::StrongFocus);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            modes->addButton(button);
            modeRow->addWidget(button);
            connect(button, &QToolButton::toggled, this, [this](bool checked) { if (checked) refreshColors(); });
        }
        content->addLayout(modeRow);
        palette = new NodeColorPalette(body, QString(), [this](const QJsonValue &color) {
            applyStyle({{colorKey(), color}});
        });
        content->addWidget(palette);
        auto *separator = new QFrame(body);
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        content->addWidget(separator);
        section(tr("Details"));
        auto *fields = new QFormLayout;
        fields->setContentsMargins(0, 0, 0, 0);
        fields->setSpacing(8);
        fields->setRowWrapPolicy(QFormLayout::WrapLongRows);
        fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        auto line = [this, fields](const char *name, const QString &label, const QString &accessible, QLineEdit *input = nullptr) {
            if (!input) input = new QLineEdit(body);
            input->setObjectName(QString::fromLatin1(name));
            input->setAccessibleName(accessible);
            input->setMinimumWidth(80);
            auto *caption = new QLabel(label, body);
            caption->setBuddy(input);
            fields->addRow(caption, input);
            return input;
        };
        tags = line("nodeTags", tr("&Tags"), tr("Tags"));
        icons = new EmojiLineEdit(body);
        line("nodeIcons", tr("&Icons"), tr("Icons"), icons);
        url = line("nodeUrl", tr("&URL"), tr("URL"));
        imageUrl = line("nodeImageUrl", tr("Image"), tr("Image"));
        imageUrl->setPlaceholderText(tr("Image URL or reference"));
        tags->setPlaceholderText(tr("Separate with commas"));
        icons->setPlaceholderText(tr("Search emoji names or paste emoji"));
        icons->setToolTip(tr(
            "<qt><p align=\"left\" style=\"margin-top: 0; margin-bottom: 6px;\">"
            "Search by emoji name, or paste emoji.<br>"
            "Separate multiple icons with commas.</p>"
            "<table cellspacing=\"0\" cellpadding=\"2\">"
            "<tr><td align=\"left\" valign=\"top\"><nobr><b>Ctrl+H / Ctrl+L</b></nobr></td>"
            "<td align=\"left\" valign=\"top\">Move left / right</td></tr>"
            "<tr><td align=\"left\" valign=\"top\"><nobr><b>Ctrl+J / Ctrl+K</b></nobr></td>"
            "<td align=\"left\" valign=\"top\">Move down / up</td></tr>"
            "<tr><td align=\"left\" valign=\"top\"><nobr><b>Up / Down</b></nobr></td>"
            "<td align=\"left\" valign=\"top\">Previous / next emoji</td></tr>"
            "<tr><td align=\"left\" valign=\"top\"><nobr><b>Ctrl+PgUp / Ctrl+PgDn</b></nobr></td>"
            "<td align=\"left\" valign=\"top\">Previous / next category</td></tr>"
            "<tr><td align=\"left\" valign=\"top\"><nobr><b>Enter</b></nobr></td>"
            "<td align=\"left\" valign=\"top\">Use selected emoji</td></tr>"
            "<tr><td align=\"left\" valign=\"top\"><nobr><b>Esc</b></nobr></td>"
            "<td align=\"left\" valign=\"top\">Close picker</td></tr>"
            "</table></qt>"));
        url->setPlaceholderText(tr("URL or reference"));
        note = new QPlainTextEdit(body);
        note->setObjectName(QStringLiteral("nodeNote"));
        note->setAccessibleName(tr("Note"));
        note->setPlaceholderText(tr("Add a note"));
        note->setTabChangesFocus(true);
        note->setMinimumHeight(78);
        note->setMaximumHeight(100);
        auto *noteLabel = new QLabel(tr("&Note"), body);
        noteLabel->setBuddy(note);
        fields->addRow(noteLabel, note);
        content->addLayout(fields);
        content->addStretch();
        scroll->setWidget(body);
        layout->addWidget(scroll, 1);
        textColor->setChecked(true);
        connect(toggle, &QToolButton::toggled, this, [this] {
            pendingColorRow = 0;
            reposition();
            toggle->setFocus(Qt::OtherFocusReason);
        });
        auto *collapse = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        collapse->setContext(Qt::WidgetWithChildrenShortcut);
        connect(collapse, &QShortcut::activated, this, [this] {
            toggle->setChecked(false);
            if (view) view->setFocus(Qt::OtherFocusReason);
        });
        connect(fontSize, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            const double size = fontSize->currentData().toDouble();
            applyStyle({{QStringLiteral("fontSize"), size > 0 ? QJsonValue(size) : QJsonValue(QJsonValue::Null)}});
        });
        connect(bold, &QToolButton::toggled, this, [this](bool checked) {
            applyStyle({{QStringLiteral("fontWeight"), checked ? QStringLiteral("bold") : QStringLiteral("normal")}});
        });
        connect(italic, &QToolButton::toggled, this, [this](bool checked) {
            applyStyle({{QStringLiteral("fontStyle"), checked ? QStringLiteral("italic") : QStringLiteral("normal")}});
        });
        connect(reset, &QToolButton::clicked, this, [this] {
            applyStyle({{QStringLiteral("fontSize"), QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("fontWeight"), QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("fontStyle"), QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("color"), QJsonValue(QJsonValue::Null)},
                        {QStringLiteral("background"), QJsonValue(QJsonValue::Null)}});
        });
        connect(tags, &QLineEdit::textChanged, this, [this](const QString &text) {
            apply({{QStringLiteral("tags"), QJsonArray::fromStringList(commaValues(text))}});
        });
        connect(icons, &QLineEdit::textChanged, this, [this](const QString &text) {
            apply({{QStringLiteral("icons"), QJsonArray::fromStringList(commaValues(text))}});
        });
        connect(url, &QLineEdit::textChanged, this, [this](const QString &text) {
            apply({{QStringLiteral("hyperLink"), text}});
        });
        connect(imageUrl, &QLineEdit::textChanged, this, [this](const QString &text) {
            if (refreshing || boundId.isEmpty() || (!currentImage && text.isEmpty())) return;
            apply({{QStringLiteral("image"), QJsonObject{{QStringLiteral("url"), text},
                {QStringLiteral("width"), currentImage ? currentImage->width : 0},
                {QStringLiteral("height"), currentImage ? currentImage->height : 0}}}});
        });
        connect(note, &QPlainTextEdit::textChanged, this, [this] {
            apply({{QStringLiteral("note"), note->toPlainText()}});
        });
        connect(controller, &MindMapController::selectionChanged, this, [this] { refresh(); });
        connect(controller, &MindMapController::documentChanged, this, [this] { refresh(); });
        connect(qApp, &QApplication::focusChanged, this, [this] { pendingColorRow = 0; });
        for (auto *button : {textColor, fillColor}) button->installEventFilter(this);
        for (auto *button : palette->findChildren<QToolButton *>()) button->installEventFilter(this);
        fontSize->view()->installEventFilter(this);
        host->installEventFilter(this);
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
        hide();
    }
    void setReadOnly(bool value) {
        if (value) {
            pendingColorRow = 0;
            fontSize->hidePopup();
            icons->dismissPopup();
        }
        fontSize->setEnabled(!value);
        for (auto *button : {bold, italic, reset, textColor, fillColor}) button->setEnabled(!value);
        palette->setEnabled(!value);
        for (auto *input : {tags, static_cast<QLineEdit *>(icons), url, imageUrl}) input->setReadOnly(value);
        note->setReadOnly(value);
    }
    enum class Action { ToggleBold, ToggleItalic, ResetStyle, TextColor, FillColor, Tags, Icons, Note, ToggleProperties };
    void activate(Action action) {
        if (controller->isReadOnly() && action != Action::ToggleProperties) return;
        const QPointer<NodePropertiesPanel> guard(this);
        refresh();
        if (!guard || boundId.isEmpty()) return;
        QWidget *target = nullptr;
        switch (action) {
        case Action::ToggleBold:
            applyStyle({{QStringLiteral("fontWeight"), bold->isChecked() ? QStringLiteral("normal") : QStringLiteral("bold")}});
            return;
        case Action::ToggleItalic:
            applyStyle({{QStringLiteral("fontStyle"), italic->isChecked() ? QStringLiteral("normal") : QStringLiteral("italic")}});
            return;
        case Action::ResetStyle: reset->click(); return;
        case Action::ToggleProperties:
            toggle->click();
            if (view) view->setFocus(Qt::OtherFocusReason);
            return;
        case Action::TextColor: target = textColor; break;
        case Action::FillColor: target = fillColor; break;
        case Action::Tags: target = tags; break;
        case Action::Icons: target = icons; break;
        case Action::Note: target = note; break;
        }
        toggle->setChecked(true);
        reposition();
        layout()->activate();
        body->layout()->activate();
        if (scroll->layout()) scroll->layout()->activate();
        scroll->ensureWidgetVisible(target);
        if (action == Action::TextColor) textColor->setChecked(true);
        else if (action == Action::FillColor) fillColor->setChecked(true);
        if (!isVisible() || !isEnabled() || !target->isVisible() || !target->isEnabled()) return;
        target->setFocus(Qt::OtherFocusReason);
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::FocusOut || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Hide)
            pendingColorRow = 0;
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (watched == fontSize->view() && fontSize->view()->isVisible() &&
                key->key() == Qt::Key_Escape) {
                if (event->type() == QEvent::KeyPress) fontSize->hidePopup();
                event->accept();
                return true;
            }
            const auto modifiers = key->modifiers() & ~Qt::KeypadModifier;
            const int digit = key->key() - Qt::Key_0;
            if (modifiers != Qt::NoModifier || digit < 0 || digit > 9) {
                pendingColorRow = 0;
            } else if (watched == parentWidget() && event->type() == QEvent::KeyPress) {
                // Only unhandled child keys reach the editor; shortcuts and inputs win.
                if (controller->isReadOnly() || !isVisible() || !isEnabled() || !toggle->isChecked() || boundId.isEmpty()) {
                    pendingColorRow = 0;
                    return false;
                }
                key->accept();
                if (key->isAutoRepeat()) return true;
                if (pendingColorRow == 0) {
                    if (digit >= 1 && digit <= 4) pendingColorRow = digit;
                    return true;
                }
                const int row = pendingColorRow;
                pendingColorRow = 0;
                if (digit >= 1 && digit <= 6) {
                    const int index = (row - 1) * 6 + digit - 1;
                    palette->activateIndex(index);
                    // Applying a color may synchronously destroy the editor.
                }
                return true;
            }
        }
        // Color controls do not drive the card's geometry or font refresh.
        if (qobject_cast<QToolButton *>(watched) && watched != reset) return false;
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Move:
        case QEvent::Show:
        case QEvent::LayoutRequest:
        case QEvent::StyleChange:
            reposition();
            break;
        case QEvent::PaletteChange:
            if (watched == reset) reset->setIcon(QIcon(new RotateCcwIconEngine(reset->palette())));
            break;
        case QEvent::FontChange:
            refresh();
            break;
        default:
            break;
        }
        return QFrame::eventFilter(watched, event);
    }
private:
    QPointer<MindMapView> view;
    MindMapController *controller;
    QWidget *heading, *body;
    QLabel *title, *subtitle;
    QScrollArea *scroll;
    QComboBox *fontSize;
    QToolButton *toggle, *bold, *italic, *reset, *textColor, *fillColor;
    QLineEdit *tags, *url, *imageUrl;
    EmojiLineEdit *icons;
    QPlainTextEdit *note;
    NodeColorPalette *palette;
    QString boundId;
    QString presentedNodeId;
    bool presentedExpanded = false;
    bool repositioning = false;
    NodeStyle currentStyle;
    std::optional<NodeImage> currentImage;
    int pendingColorRow = 0;
    bool refreshing = false;
    QString colorKey() const {
        return fillColor->isChecked() ? QStringLiteral("background") : QStringLiteral("color");
    }
    void applyStyle(const QJsonObject &style) {
        apply({{QStringLiteral("style"), style}});
    }
    void apply(const QJsonObject &patch) {
        if (refreshing || boundId.isEmpty()) return;
        if (controller->isReadOnly()) { refresh(); return; }
        const QString id = boundId;
        const QByteArray json = QJsonDocument(patch).toJson(QJsonDocument::Compact);
        const QPointer<NodePropertiesPanel> guard(this);
        controller->updateNodeProperties(id, json);
        // Re-read even after a no-op or a nested load/new from a host signal handler.
        if (guard) refresh();
    }
    void refreshColors() {
        pendingColorRow = 0;
        const QColor color = fillColor->isChecked() ? currentStyle.backgroundColor : currentStyle.textColor;
        palette->setColor(color, fillColor->isChecked() ? tr("Use the default fill") : tr("Use the default text color"));
    }
    void refresh() {
        const QPointer<NodePropertiesPanel> lifetime(this);
        const auto properties = controller->nodeProperties(controller->selectedNodeId());
        if (!lifetime) return;
        const bool sameNode = properties.id.isEmpty() == false && properties.id == boundId;
        const QScopedValueRollback<bool> guard(refreshing, true);
        boundId = properties.id;
        currentStyle = properties.style;
        currentImage = properties.image;
        subtitle->setText(properties.topic);
        subtitle->setToolTip(QStringLiteral("<qt>%1</qt>").arg(properties.topic.toHtmlEscaped()));
        auto refreshList = [this, sameNode](QLineEdit *input, const QStringList &values) {
            const QString text = values.join(QStringLiteral(", "));
            if (!sameNode || (commaValues(input->text()) != values && input->text() != text)) {
                if (input == icons) icons->dismissPopup();
                const QSignalBlocker blocker(input);
                input->setText(text);
            }
        };
        refreshList(tags, properties.tags);
        refreshList(icons, properties.icons);
        if (!sameNode || url->text() != properties.hyperlink) {
            const QSignalBlocker blocker(url);
            url->setText(properties.hyperlink);
        }
        const QString imageText = currentImage ? currentImage->url : QString();
        if (!sameNode || imageUrl->text() != imageText) {
            const QSignalBlocker blocker(imageUrl);
            imageUrl->setText(imageText);
        }
        if (!sameNode || note->toPlainText() != properties.note) {
            const QSignalBlocker blocker(note);
            note->setPlainText(properties.note);
        }
        syncFontSize(fontSize, properties.style.fontSize);
        {
            const QSignalBlocker blocker(bold);
            bold->setChecked(properties.style.bold.value_or(properties.root));
            bold->setToolTip(properties.style.bold.has_value()
                ? tr("Explicit font weight; Reset appearance restores the default")
                : tr("Default font weight for this node"));
        }
        {
            const QSignalBlocker blocker(italic);
            italic->setChecked(properties.style.italic.value_or(view && view->font().italic()));
            italic->setToolTip(properties.style.italic.has_value()
                ? tr("Explicit font style; Reset appearance restores the default")
                : tr("Default font style for this node"));
        }
        refreshColors();
        reposition();
    }
    void reposition() {
        if (repositioning) return;
        const QScopedValueRollback<bool> guard(repositioning, true);
        const bool wasVisible = isVisible();
        if (!view || boundId.isEmpty()) {
            presentedNodeId.clear();
            presentedExpanded = false;
            hide();
            return;
        }
        const QRect viewport(view->viewport()->mapTo(parentWidget(), QPoint()), view->viewport()->size());
        const QRect available = viewport.intersected(parentWidget()->rect()).adjusted(12, 12, -12, -12);
        if (available.isEmpty()) {
            presentedNodeId.clear();
            presentedExpanded = false;
            hide();
            return;
        }
        const bool expanded = toggle->isChecked();
        const bool reveal = expanded && (!wasVisible || boundId != presentedNodeId || !presentedExpanded);
        title->setVisible(expanded);
        subtitle->setVisible(expanded);
        scroll->setVisible(expanded);
        heading->layout()->setContentsMargins(expanded ? QMargins(12, 8, 8, 9) : QMargins());
        toggle->setArrowType(expanded ? Qt::UpArrow : Qt::DownArrow);
        const QString action = expanded ? tr("Collapse node properties") : tr("Expand node properties");
        toggle->setToolTip(action);
        toggle->setAccessibleName(action);
        heading->layout()->activate();
        layout()->activate();
        const QSize header = heading->sizeHint();
        const int width = qMin(expanded ? 300 : header.width() + 2, available.width());
        const int height = qMin(header.height() + (expanded ? body->sizeHint().height() : 0) + 2, available.height());
        setGeometry(available.right() - width + 1, available.top(), width, height);
        show();
        raise();
        if (!isVisible() || !view->isVisible()) return;
        presentedNodeId = boundId;
        presentedExpanded = expanded;
        if (reveal && !controller->isRestoringHistory()) {
            const QString id = boundId;
            if (!id.isEmpty() && id == controller->selectedNodeId()) {
                view->ensureNodeVisible(id, QRect(view->viewport()->mapFromGlobal(mapToGlobal(QPoint())), size()));
            }
        }
    }
};
}
class MindMapEditor::Private : public QObject {
    Q_DECLARE_TR_FUNCTIONS(m3::qt::MindMapEditor)
public:
    MindMapEditor *host;
    const EditorConfig config;
    MindMapView *view;
    MindMapController *controller;
    QLabel *error;
    QAction *addChild, *addSibling, *addSiblingBefore, *editSelection, *deleteSelection, *toggleExpanded, *move, *up, *down, *addLink;
    QAction *rootSelection, *clearSelectionAction, *editLink, *toggleProperties, *copySelection;
    QAction *undoAction, *redoAction;
    QAction *boldAction = nullptr, *italicAction = nullptr;
    QList<QAction *> nodeNavigation;
    NodePropertiesPanel *properties;
    QList<QAction *> nodeEditingActions;
    QList<QAction *> toolbarFormattingActions;
    FontSizeAction *fontSizeAction = nullptr;
    quint64 formattingEpoch = 0;
    struct FormattingBinding { quint64 epoch = 0; QString node; };
    struct FormattingPopup {
        QAction *action = nullptr;
        QMenu *menu = nullptr;
        QWidget *content = nullptr;
        FormattingBinding binding;
        QList<QPointer<QWidget>> presenters;
    };
    FormattingPopup textPopup, fillPopup, iconsPopup;
    EmojiLineEdit *toolbarIcons = nullptr;
    QList<FormattingPopup *> formattingPopups;
    NodeColorPalette *textPalette = nullptr, *fillPalette = nullptr;
    bool canFormat() const {
        return host->isEnabled() && !controller->isReadOnly() &&
            controller->selectedNodeIds().size() == 1 && !controller->selectedNodeId().isEmpty();
    }
    FormattingBinding formattingBinding() const {
        return {formattingEpoch, controller->selectedNodeId()};
    }
    bool validBinding(const FormattingBinding &binding) const {
        return binding.epoch == formattingEpoch && !binding.node.isEmpty() &&
            binding.node == controller->selectedNodeId() && canFormat();
    }
    void invalidateFormatting() {
        ++formattingEpoch;
        const QPointer<MindMapEditor> guard(host);
        if (fontSizeAction) fontSizeAction->hidePopups();
        if (!guard) return;
        for (auto *popup : formattingPopups) {
            popup->binding = {};
            popup->content->setEnabled(false);
            popup->menu->hide();
            if (!guard) return;
        }
    }
    void rememberPresenters(FormattingPopup &popup) {
        popup.presenters.clear();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const auto associated = popup.action->associatedObjects();
#else
        const auto associated = popup.action->associatedWidgets();
#endif
        for (auto *object : associated) {
            auto *widget = qobject_cast<QWidget *>(object);
            if (!widget || !widget->isVisible()) continue;
            for (; widget; widget = widget->parentWidget()) {
                if ((qobject_cast<QToolBar *>(widget) || (widget->isWindow() && !qobject_cast<QMenu *>(widget))) &&
                    !popup.presenters.contains(widget)) popup.presenters.append(widget);
            }
        }
    }
    void preparePopup(FormattingPopup &popup) {
        popup.content->setEnabled(false);
        popup.binding = {};
        const FormattingBinding binding = formattingBinding();
        const QPointer<MindMapEditor> guard(host);
        const QPointer<QMenu> menu(popup.menu);
        const bool committed = validBinding(binding) && host->commitActiveEdit();
        if (!guard || !menu) return;
        if (!committed || !validBinding(binding)) {
            const QPointer<QWidget> rejectedInput(committed ? nullptr : view->focusWidget());
            QTimer::singleShot(0, menu, [guard, menu, rejectedInput] {
                if (menu) menu->hide();
                if (guard && rejectedInput && rejectedInput->isVisible() && guard->window()->isActiveWindow())
                    rejectedInput->setFocus(Qt::OtherFocusReason);
            });
            syncFormatting();
            return;
        }
        popup.binding = binding;
        syncFormatting();
        if (!guard || !menu) return;
        if (!validBinding(binding)) { menu->hide(); return; }
        rememberPresenters(popup);
        if (&popup == &iconsPopup) {
            const QSignalBlocker blocker(toolbarIcons);
            QString text = toolbarIcons->text();
            if (!text.isEmpty() && !text.trimmed().endsWith(QLatin1Char(','))) text += QStringLiteral(", ");
            toolbarIcons->setText(text);
            toolbarIcons->setCursorPosition(text.size());
            static_cast<IconsMenu *>(popup.menu)->boundPickerSize();
            const QPointer<QWidget> ancestor = QApplication::activePopupWidget();
            if ((menu->windowFlags() & Qt::WindowType_Mask) == Qt::Tool && ancestor && ancestor != menu) {
                // QMenu's submenu-opening stack still uses its parent after aboutToShow.
                // Transfer only after it unwinds, then release the ancestor's keyboard
                // grab and prepare the same bound node as an independent input menu.
                QTimer::singleShot(0, menu, [this, guard, menu, ancestor, binding] {
                    if (!guard || !menu || !menu->isVisible() || !validBinding(binding)) return;
                    const QPoint anchor = menu->pos();
                    if (ancestor) ancestor->hide();
                    if (guard && menu && validBinding(binding)) menu->popup(anchor);
                });
                return; // Reject input during the popup-stack transfer.
            }
        }
        popup.content->setEnabled(true);
    }
    void connectPopup(FormattingPopup &popup, const char *name, const char *label) {
        popup.action = new QAction(propertiesText(label), host);
        popup.action->setObjectName(QString::fromLatin1(name));
        popup.action->setToolTip(popup.action->text());
        popup.action->setMenu(popup.menu);
        popup.menu->setObjectName(QString::fromLatin1(name) + QStringLiteral("Menu"));
        popup.content->setEnabled(false);
        auto *contentAction = new QWidgetAction(popup.menu);
        contentAction->setDefaultWidget(popup.content);
        popup.menu->addAction(contentAction);
        toolbarFormattingActions.append(popup.action);
        formattingPopups.append(&popup);
        QObject::connect(popup.menu, &QMenu::aboutToShow, host, [this, &popup] { preparePopup(popup); });
        QObject::connect(popup.menu, &QMenu::aboutToHide, host, [&popup] {
            popup.binding = {};
            popup.content->setEnabled(false);
            popup.presenters.clear();
        });
    }
    void applyPopup(FormattingPopup &popup, const QJsonObject &patch, bool closeAfterApply) {
        if (!popup.menu->isVisible() || !popup.content->isEnabled() || !validBinding(popup.binding)) return;
        const FormattingBinding binding = popup.binding;
        const QPointer<MindMapEditor> guard(host);
        const QPointer<QMenu> menu(popup.menu);
        const QByteArray json = QJsonDocument(patch).toJson(QJsonDocument::Compact);
        const bool applied = controller->updateNodeProperties(binding.node, json);
        if (!guard || !menu) return;
        syncFormatting();
        if (!guard || !menu || !applied || !validBinding(binding) || !closeAfterApply) return;
        menu->hide();
        if (guard && host->window()->isActiveWindow()) view->setFocus(Qt::OtherFocusReason);
    }
    void createColorPopup(FormattingPopup &popup, const char *name, const char *label,
                          const QString &prefix, const QString &key, NodeColorPalette *&palette) {
        auto *menu = new ColorMenu(host);
        popup.menu = menu;
        palette = new NodeColorPalette(menu, prefix, [this, &popup, key](const QJsonValue &color) {
            applyPopup(popup, {{QStringLiteral("style"), QJsonObject{{key, color}}}}, true);
        });
        menu->palette = palette;
        popup.content = palette;
        connectPopup(popup, name, label);
    }
    void createIconsPopup() {
        auto *menu = new IconsMenu(host);
        iconsPopup.menu = menu;
        auto *content = new QWidget(menu);
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(6);
        toolbarIcons = new EmojiLineEdit(content, EmojiLineEdit::PickerMode::Embedded);
        toolbarIcons->setObjectName(QStringLiteral("toolbarNodeIcons"));
        toolbarIcons->setAccessibleName(propertiesText("Icons"));
        toolbarIcons->setPlaceholderText(propertiesText("Search emoji names or paste emoji"));
        layout->addWidget(toolbarIcons);
        layout->addWidget(toolbarIcons->pickerWidget(), 1);
        menu->input = toolbarIcons;
        iconsPopup.content = content;
        connectPopup(iconsPopup, "iconsPopup", "Icons");
        QObject::connect(toolbarIcons, &QLineEdit::textChanged, host, [this](const QString &text) {
            applyPopup(iconsPopup, {{QStringLiteral("icons"), QJsonArray::fromStringList(commaValues(text))}}, false);
        });
    }
    void applyFontSize(qreal size) {
        const FormattingBinding binding = formattingBinding();
        const QPointer<MindMapEditor> guard(host);
        if (!validBinding(binding)) { syncFormatting(); return; }
        const bool committed = host->commitActiveEdit();
        if (!guard) return;
        if (!committed || !validBinding(binding)) { syncFormatting(); return; }
        const QJsonObject style{{QStringLiteral("fontSize"), size > 0 ? QJsonValue(size) : QJsonValue(QJsonValue::Null)}};
        const QByteArray patch = QJsonDocument(QJsonObject{{QStringLiteral("style"), style}}).toJson(QJsonDocument::Compact);
        controller->updateNodeProperties(binding.node, patch);
        if (guard) syncFormatting();
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        const QPointer<MindMapEditor> guard(host);
        if (watched == host) {
            if (event->type() == QEvent::Hide) invalidateFormatting();
            else if (event->type() == QEvent::EnabledChange) {
                invalidateFormatting();
                if (guard) updateActions();
            }
        } else if (event->type() == QEvent::Hide || event->type() == QEvent::Close || event->type() == QEvent::Destroy) {
            for (const auto *popup : formattingPopups) {
                bool presenting = false;
                for (const auto &presenter : popup->presenters) {
                    if (presenter.data() == watched) { presenting = true; break; }
                }
                if (presenting) { invalidateFormatting(); break; }
            }
        }
        return false;
    }
    enum class TopicOperation { Child, SiblingAfter, SiblingBefore };
    enum class Navigation { Parent, Child, PreviousSibling, NextSibling };
    QString shortcutHelpText;
    QPointer<ShortcutHelpPopup> helpPopup;
    QPointer<QDialog> mutationDialog;
    void showHelp() {
        if (!helpPopup) helpPopup = new ShortcutHelpPopup(host, view, shortcutHelpText);
        const QRect available = view->screen()->availableGeometry().adjusted(12, 12, -12, -12);
        const QSize size(qMin(640, available.width()), qMin(640, available.height()));
        const QPoint center = view->viewport()->mapToGlobal(view->viewport()->rect().center());
        helpPopup->resize(size);
        helpPopup->move(qBound(available.left(), center.x() - size.width() / 2, available.right() - size.width() + 1),
                        qBound(available.top(), center.y() - size.height() / 2, available.bottom() - size.height() + 1));
        helpPopup->show();
        helpPopup->raise();
        helpPopup->setFocus(Qt::PopupFocusReason);
    }
    QString shortcutHelp() const {
        auto row = [](const QString &label, const QList<QKeySequence> &bindings) {
            QStringList keys;
            for (const auto &binding : bindings)
                if (!binding.isEmpty()) keys.append(binding.toString(QKeySequence::NativeText).toHtmlEscaped());
            const QString value = keys.isEmpty() ? tr("Unassigned").toHtmlEscaped() : keys.join(QStringLiteral("<br>"));
            return QStringLiteral("<tr><td valign=top><nobr>%1</nobr></td><td valign=top><nobr>%2</nobr></td></tr>")
                .arg(value, label.toHtmlEscaped());
        };
        auto section = [](const QString &title, const QString &rows) {
            return QStringLiteral("<p><b>%1</b></p><table cellspacing=4>%2</table>")
                .arg(title.toHtmlEscaped(), rows);
        };
        QString canvas, nodes;
        for (auto *action : view->actions()) {
            auto &rows = nodeEditingActions.contains(action) ? nodes : canvas;
            rows += row(action->text(), action->shortcuts());
        }
        QString inlineEdit = row(tr("Accept topic"), config.shortcuts.acceptTopic);
        inlineEdit += row(tr("Cancel draft"), {QKeySequence(Qt::Key_Escape)});
        QList<QKeySequence> newlines;
        for (const auto &newline : {QKeySequence(Qt::SHIFT | Qt::Key_Return), QKeySequence(Qt::SHIFT | Qt::Key_Enter)}) {
            bool accepts = false;
            for (const auto &binding : config.shortcuts.acceptTopic)
                if (!binding.isEmpty() && binding[0] == newline[0]) { accepts = true; break; }
            if (!accepts) newlines.append(newline);
        }
        if (!newlines.isEmpty()) inlineEdit += row(tr("New line"), newlines);
        QString emoji = row(tr("Move left / down / up / right"),
            {QKeySequence(Qt::CTRL | Qt::Key_H), QKeySequence(Qt::CTRL | Qt::Key_J),
             QKeySequence(Qt::CTRL | Qt::Key_K), QKeySequence(Qt::CTRL | Qt::Key_L)});
        emoji += row(tr("Previous / next emoji"), {QKeySequence(Qt::Key_Up), QKeySequence(Qt::Key_Down)});
        emoji += row(tr("Previous / next category"), {QKeySequence(Qt::CTRL | Qt::Key_PageUp), QKeySequence(Qt::CTRL | Qt::Key_PageDown)});
        emoji += row(tr("Use selected emoji"), {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)});
        emoji += row(tr("Close picker"), {QKeySequence(Qt::Key_Escape)});
        emoji += row(tr("Leave picker field"), {QKeySequence(Qt::Key_Tab), QKeySequence(Qt::SHIFT | Qt::Key_Tab)});
        const QString left = section(tr("Canvas"), canvas) + section(tr("During drag or image resize"),
            row(tr("Cancel gesture"), {QKeySequence(Qt::Key_Escape)}));
        const QString right = section(tr("Selected node on canvas"), nodes) + section(tr("Inline topic"), inlineEdit)
            + section(tr("Node properties"), row(tr("Collapse card"), {QKeySequence(Qt::Key_Escape)})
                + QStringLiteral("<tr><td colspan=2>%1<br>%2<br>%3</td></tr>").arg(
                    tr("With properties expanded, outside inputs:").toHtmlEscaped(),
                    tr("Type row (1-4), then column (1-6).").toHtmlEscaped(),
                    tr("11 selects Auto.").toHtmlEscaped()))
            + section(tr("Emoji picker"), emoji);
        return QStringLiteral("<qt><b>%1</b><p>%2</p><table cellspacing=12><tr>"
            "<td valign=top>%3</td><td valign=top>%4</td></tr></table></qt>")
            .arg(tr("Keyboard shortcuts").toHtmlEscaped(),
                 tr("Canvas shortcuts require map focus. Letters type normally in text fields.").toHtmlEscaped(), left, right);
    }
    QAction *action(const char *name, const QString &text, const QList<QKeySequence> &shortcuts, std::function<void()> command) {
        auto *result = new QAction(text, host);
        result->setObjectName(QString::fromLatin1(name));
        result->setShortcuts(shortcuts);
        result->setShortcutVisibleInContextMenu(true);
        QStringList keys;
        for (const auto &shortcut : shortcuts) keys.append(shortcut.toString(QKeySequence::NativeText));
        result->setToolTip(keys.isEmpty() ? text : text + QStringLiteral(" (%1)").arg(keys.join(QStringLiteral(", "))));
        result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(result);
        QObject::connect(view, &MindMapView::topicEditingChanged, result, [result, shortcuts](bool editing) {
            result->setShortcuts(editing ? QList<QKeySequence>{} : shortcuts);
        });
        QObject::connect(result, &QAction::triggered, host, [this, result, command = std::move(command)] {
            const QPointer<MindMapEditor> guard(host);
            if (!result->isEnabled()) { syncFormatting(); return; }
            const bool appearance = result == boldAction || result == italicAction ||
                result->objectName() == QStringLiteral("resetStyle");
            const FormattingBinding binding = formattingBinding();
            const bool committed = view->finishTopicEdit(true);
            if (!guard) return;
            if (!committed || !result->isEnabled() || (appearance && !validBinding(binding))) {
                syncFormatting();
                return;
            }
            command();
            if (guard) syncFormatting();
        });
        return result;
    }
    static const NodeChoice *choice(const std::vector<NodeChoice> &nodes, const QString &id) {
        for (const auto &node : nodes) if (node.id == id) return &node;
        return nullptr;
    }
    static void populate(QComboBox *picker, const std::vector<NodeChoice> &nodes, const QSet<QString> &excluded = {}) {
        for (const auto &node : nodes)
            if (!excluded.contains(node.id)) picker->addItem(node.topic + QStringLiteral(" [") + node.id + QLatin1Char(']'), node.id);
    }
    void syncFormatting() {
        const QPointer<MindMapEditor> guard(host);
        const auto node = controller->nodeProperties(controller->selectedNodeId());
        if (!guard) return;
        if (boldAction) {
            const QSignalBlocker blocker(boldAction);
            boldAction->setChecked(node.style.bold.value_or(node.root));
        }
        if (italicAction) {
            const QSignalBlocker blocker(italicAction);
            italicAction->setChecked(node.style.italic.value_or(view->font().italic()));
        }
        if (fontSizeAction) fontSizeAction->synchronize(node.style.fontSize);
        if (textPalette) textPalette->setColor(node.style.textColor, propertiesText("Use the default text color"));
        if (fillPalette) fillPalette->setColor(node.style.backgroundColor, propertiesText("Use the default fill"));
        if (toolbarIcons && commaValues(toolbarIcons->text()) != node.icons) {
            const QSignalBlocker blocker(toolbarIcons);
            toolbarIcons->setText(node.icons.join(QStringLiteral(", ")));
        }
    }
    void updateActions() {
        const QPointer<MindMapEditor> guard(host);
        const auto nodes = controller->choices();
        if (!guard) return;
        const auto *node = choice(nodes, controller->selectedNodeId());
        const bool hasLink = controller->selectedLinkId().isEmpty() == false;
        const auto selectedNodes = controller->selectedNodeIds();
        const bool hasNodes = !selectedNodes.isEmpty();
        const bool deletableNodes = hasNodes && !nodes.empty() && !selectedNodes.contains(nodes.front().id);
        const bool writable = host->isEnabled() && !controller->isReadOnly();
        const bool movable = writable && node && !node->parent.isEmpty();
        undoAction->setEnabled(controller->canUndo());
        redoAction->setEnabled(controller->canRedo());
        addChild->setEnabled(writable && node); addLink->setEnabled(writable && node);
        addSibling->setEnabled(writable && node); addSiblingBefore->setEnabled(writable && node);
        for (auto *action : nodeNavigation) action->setEnabled(node);
        for (auto *action : nodeEditingActions) action->setEnabled(node && (writable || action == toggleProperties));
        for (auto *action : toolbarFormattingActions) action->setEnabled(writable && node);
        if (!writable || !node) fontSizeAction->hidePopups();
        properties->setReadOnly(!writable);
        copySelection->setEnabled(hasNodes || hasLink);
        rootSelection->setEnabled(!nodes.empty());
        clearSelectionAction->setEnabled(hasNodes || hasLink);
        editSelection->setEnabled(writable && (node || hasLink));
        editLink->setEnabled(writable && hasLink);
        deleteSelection->setEnabled(writable && (deletableNodes || hasLink));
        move->setEnabled(movable);
        toggleExpanded->setEnabled(writable && node && !node->children.isEmpty());
        toggleExpanded->setText(node && !node->expanded ? tr("Expand") : tr("Collapse"));
        const auto *parent = node ? choice(nodes, node->parent) : nullptr;
        const auto index = parent ? parent->children.indexOf(node->id) : -1;
        up->setEnabled(writable && parent && index > 0);
        down->setEnabled(writable && parent && index >= 0 && index + 1 < parent->children.size());
        syncFormatting();
    }
    void createNode(TopicOperation operation) {
        if (controller->isReadOnly()) return;
        const QPointer<MindMapEditor> guard(host);
        const QString id = controller->selectedNodeId();
        const auto nodes = controller->choices();
        if (!guard) return;
        const auto *node = choice(nodes, id);
        if (!node) return;
        QString parentId = id;
        int index = -1;
        if ((operation == TopicOperation::SiblingAfter || operation == TopicOperation::SiblingBefore) && !node->parent.isEmpty()) {
            const auto *parent = choice(nodes, node->parent);
            if (!parent) return;
            parentId = parent->id;
            index = int(parent->children.indexOf(id)) + (operation == TopicOperation::SiblingAfter ? 1 : 0);
        }
        const auto *parent = choice(nodes, parentId);
        if (!parent || (!parent->expanded && !controller->setExpanded(parentId, true)) || !guard) return;
        const QString created = controller->addNode(parentId, QString(), index);
        if (guard && !created.isEmpty() && controller->selectedNodeId() == created)
            view->beginTopicEdit(created, config.shortcuts.acceptTopic);
    }
    void navigate(Navigation command) {
        const auto nodes = controller->choices();
        if (nodes.empty()) return;
        const auto *node = choice(nodes, controller->selectedNodeId());
        if (!node) return;
        QString target;
        if (command == Navigation::Parent) target = node->parent;
        else if (command == Navigation::Child) {
            if (node->expanded && !node->children.isEmpty()) target = node->children.front();
        } else {
            const auto *parent = choice(nodes, node->parent);
            if (!parent) return;
            const auto index = parent->children.indexOf(node->id) + (command == Navigation::PreviousSibling ? -1 : 1);
            if (index >= 0 && index < parent->children.size()) target = parent->children[index];
        }
        if (!target.isEmpty()) controller->selectNode(target);
    }
    void linkDialog(bool insert) {
        if (controller->isReadOnly()) return;
        const auto nodes = controller->choices();
        if (nodes.empty()) return;
        const QString id = controller->selectedLinkId();
        const auto link = insert ? LinkPresentation{} : controller->linkChoice(id);
        if (!insert && link.id.isEmpty()) return;
        const QPointer<MindMapEditor> guard(host);
        QPointer<QDialog> dialog(new QDialog(host));
        mutationDialog = dialog;
        dialog->setWindowTitle(insert ? tr("Add link") : tr("Edit link"));
        auto *form = new QFormLayout(dialog);
        auto *source = new QComboBox(dialog), *target = new QComboBox(dialog);
        source->setObjectName(QStringLiteral("sourceNode")); target->setObjectName(QStringLiteral("targetNode"));
        populate(source, nodes); populate(target, nodes);
        source->setCurrentIndex(source->findData(insert ? controller->selectedNodeId() : link.source));
        target->setCurrentIndex(target->findData(insert ? controller->selectedNodeId() : link.target));
        auto *topic = new QLineEdit(link.topic, dialog);
        topic->setObjectName(QStringLiteral("linkTopic"));
        auto *directed = new QCheckBox(tr("Directed"), dialog);
        directed->setObjectName(QStringLiteral("directed")); directed->setChecked(!insert && link.direction != LinkDirection::None);
        form->addRow(tr("Source"), source); form->addRow(tr("Target"), target);
        form->addRow(tr("Topic"), topic); form->addRow(directed);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, dialog.data(), &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog.data(), &QDialog::reject);
        const bool accepted = dialog->exec() == QDialog::Accepted;
        if (!guard || !dialog) return;
        if (!accepted) { delete dialog.data(); return; }
        const QString sourceId = source->currentData().toString(), targetId = target->currentData().toString();
        const QString linkTopic = topic->text();
        const bool isDirected = directed->isChecked();
        delete dialog.data();
        if (!guard) return;
        if (insert) {
            const QString created = controller->addLink(sourceId, targetId, isDirected, linkTopic);
            if (guard && !created.isEmpty() && controller->selectedLinkId() == created) {
                host->activateWindow();
                if (guard) view->beginLinkTopicEdit(created, config.shortcuts.acceptTopic);
            }
        } else controller->updateLink(id, sourceId, targetId, isDirected, linkTopic);
    }
    void moveDialog() {
        if (controller->isReadOnly()) return;
        const auto nodes = controller->choices();
        const QString id = controller->selectedNodeId();
        const auto *node = choice(nodes, id);
        if (!node || node->parent.isEmpty()) return;
        QSet<QString> excluded{id};
        // Choices arrive in child preorder: each excluded parent precedes its descendants.
        for (const auto &entry : nodes) if (excluded.contains(entry.parent)) excluded.insert(entry.id);
        const QPointer<MindMapEditor> guard(host);
        QPointer<QDialog> dialog(new QDialog(host));
        mutationDialog = dialog;
        dialog->setWindowTitle(tr("Move node"));
        auto *form = new QFormLayout(dialog);
        auto *parent = new QComboBox(dialog);
        parent->setObjectName(QStringLiteral("newParent"));
        populate(parent, nodes, excluded);
        auto *index = new QSpinBox(dialog);
        index->setObjectName(QStringLiteral("insertIndex"));
        auto range = [&] {
            const auto *destination = choice(nodes, parent->currentData().toString());
            const int maximum = destination ? int(destination->children.size()) - (destination->id == node->parent ? 1 : 0) : 0;
            index->setRange(0, maximum);
            index->setValue(maximum);
        };
        QObject::connect(parent, QOverload<int>::of(&QComboBox::currentIndexChanged), dialog.data(), range);
        parent->setCurrentIndex(parent->findData(node->parent));
        range();
        const auto *oldParent = choice(nodes, node->parent);
        if (oldParent) index->setValue(int(oldParent->children.indexOf(id)));
        form->addRow(tr("New parent"), parent); form->addRow(tr("Insertion index (zero-based)"), index);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, dialog.data(), &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog.data(), &QDialog::reject);
        const bool accepted = dialog->exec() == QDialog::Accepted;
        if (!guard || !dialog) return;
        const QString parentId = parent->currentData().toString();
        const int insertionIndex = index->value();
        delete dialog.data();
        if (guard && accepted) controller->moveNode(id, parentId, insertionIndex);
    }
    void reorder(int delta) {
        const auto nodes = controller->choices();
        const auto *node = choice(nodes, controller->selectedNodeId());
        const auto *parent = node ? choice(nodes, node->parent) : nullptr;
        if (parent) controller->moveNode(node->id, parent->id, int(parent->children.indexOf(node->id)) + delta);
    }
    Private(MindMapEditor *editor, const EditorConfig &settings) : QObject(editor), host(editor), config(settings) {
        auto *layout = new QVBoxLayout(editor);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        view = new MindMapView(editor);
        error = new QLabel(editor);
        error->setTextFormat(Qt::PlainText); error->setWordWrap(true); error->hide();
        controller = new MindMapController(*view, config, editor);
        view->setTopicCommitHandler([model = QPointer<MindMapController>(controller)](
            const QString &id, const QString &draft, const QString &previousDraft, bool link) {
            return model && (link ? model->commitLinkTopicEdit(id, draft) : model->commitTopicEdit(id, draft, previousDraft));
        });
        undoAction = action("undo", tr("Undo"), config.shortcuts.undo, [this] { host->undo(); });
        redoAction = action("redo", tr("Redo"), config.shortcuts.redo, [this] { host->redo(); });
        addChild = action("addChild", tr("Add Child"), config.shortcuts.addChild, [this] { createNode(TopicOperation::Child); });
        addSibling = action("addSibling", tr("Add Sibling"), config.shortcuts.addSibling, [this] { createNode(TopicOperation::SiblingAfter); });
        addSiblingBefore = action("addSiblingBefore", tr("Add Sibling Before"), config.shortcuts.addSiblingBefore, [this] { createNode(TopicOperation::SiblingBefore); });
        editSelection = action("editSelection", tr("Rename/Edit"), config.shortcuts.editSelection, [this] {
            if (controller->selectedLinkId().isEmpty())
                view->beginTopicEdit(controller->selectedNodeId(), config.shortcuts.acceptTopic);
            else view->beginLinkTopicEdit(controller->selectedLinkId(), config.shortcuts.acceptTopic);
        });
        deleteSelection = action("deleteSelection", tr("Delete"), config.shortcuts.deleteSelection, [this] {
            const auto link = controller->selectedLinkId();
            if (!link.isEmpty()) { controller->removeLink(link); return; }
            const auto nodes = controller->selectedNodeIds();
            if (nodes.isEmpty()) return;
            const QPointer<MindMapEditor> guard(host);
            if (config.confirmSubtreeDeletion && QMessageBox::question(host,
                nodes.size() == 1 ? tr("Delete subtree") : tr("Delete subtrees"),
                nodes.size() == 1 ? tr("Delete this node and all its descendants?")
                    : tr("Delete the %1 selected nodes and all their descendants?").arg(nodes.size()),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
            if (guard && controller->selectedNodeIds() == nodes) controller->removeSelectedNodes();
        });
        toggleExpanded = action("toggleExpanded", tr("Expand/Collapse"), config.shortcuts.toggleExpanded, [this] {
            const auto nodes = controller->choices();
            const auto *node = choice(nodes, controller->selectedNodeId());
            if (node) controller->setExpanded(node->id, !node->expanded);
        });
        move = action("moveNode", tr("Move..."), config.shortcuts.moveNode, [this] { moveDialog(); });
        up = action("moveUp", tr("Move Up"), config.shortcuts.moveUp, [this] { reorder(-1); });
        down = action("moveDown", tr("Move Down"), config.shortcuts.moveDown, [this] { reorder(1); });
        addLink = action("addLink", tr("Add link"), config.shortcuts.addLink, [this] { linkDialog(true); });
        editLink = action("editLink", tr("Link properties..."), {}, [this] { linkDialog(false); });
        auto *zoomIn = action("zoomIn", tr("Zoom In"), config.shortcuts.zoomIn, [this] { view->zoom(1.2); });
        auto *zoomOut = action("zoomOut", tr("Zoom Out"), config.shortcuts.zoomOut, [this] { view->zoom(1 / 1.2); });
        auto *resetZoom = action("resetZoom", tr("100%"), config.shortcuts.resetZoom, [this] { view->resetZoom(); });
        auto *fit = action("fit", tr("Fit"), config.shortcuts.fit, [this] { view->fitContents(); });
        rootSelection = action("selectRoot", tr("Focus Main Node"), config.shortcuts.selectRoot, [this] { host->focusRoot(); });
        nodeNavigation = {
            action("selectParent", tr("Select parent"), config.shortcuts.selectParent, [this] { navigate(Navigation::Parent); }),
            action("selectChild", tr("Select first child"), config.shortcuts.selectChild, [this] { navigate(Navigation::Child); }),
            action("previousSibling", tr("Select previous sibling"), config.shortcuts.previousSibling, [this] { navigate(Navigation::PreviousSibling); }),
            action("nextSibling", tr("Select next sibling"), config.shortcuts.nextSibling, [this] { navigate(Navigation::NextSibling); })
        };
        copySelection = action("copy", tr("Copy"), {QKeySequence(QKeySequence::Copy)}, [this] {
            QApplication::clipboard()->setText(host->selectedText());
        });
        clearSelectionAction = action("clearSelection", tr("Clear selection"), config.shortcuts.clearSelection, [this] { controller->clearSelection(); });
        layout->addWidget(view, 1); layout->addWidget(error);
        properties = new NodePropertiesPanel(editor, view, controller);
        nodeEditingActions = {
            boldAction = action("toggleBold", tr("Toggle bold"), config.shortcuts.toggleBold, [this] { properties->activate(NodePropertiesPanel::Action::ToggleBold); }),
            italicAction = action("toggleItalic", tr("Toggle italic"), config.shortcuts.toggleItalic, [this] { properties->activate(NodePropertiesPanel::Action::ToggleItalic); }),
            action("resetStyle", tr("Reset style"), config.shortcuts.resetStyle, [this] { properties->activate(NodePropertiesPanel::Action::ResetStyle); }),
            action("textColor", tr("Text color"), config.shortcuts.textColor, [this] { properties->activate(NodePropertiesPanel::Action::TextColor); }),
            action("fillColor", tr("Fill color"), config.shortcuts.fillColor, [this] { properties->activate(NodePropertiesPanel::Action::FillColor); }),
            action("editTags", tr("Edit tags"), config.shortcuts.editTags, [this] { properties->activate(NodePropertiesPanel::Action::Tags); }),
            action("editIcons", tr("Edit icons"), config.shortcuts.editIcons, [this] { properties->activate(NodePropertiesPanel::Action::Icons); }),
            action("editNote", tr("Edit note"), config.shortcuts.editNote, [this] { properties->activate(NodePropertiesPanel::Action::Note); }),
            toggleProperties = action("toggleProperties", tr("Toggle properties panel"), config.shortcuts.toggleProperties, [this] { properties->activate(NodePropertiesPanel::Action::ToggleProperties); }),
            action("editTopic", tr("Edit topic"), config.shortcuts.editTopic, [this] {
                const QString id = controller->selectedNodeId();
                if (!id.isEmpty()) view->beginTopicEdit(id, config.shortcuts.acceptTopic);
            })
        };
        boldAction->setCheckable(true);
        italicAction->setCheckable(true);
        fontSizeAction = new FontSizeAction(editor, [this](qreal size) { applyFontSize(size); });
        toolbarFormattingActions.append(fontSizeAction);
        createColorPopup(textPopup, "textColorPopup", "Text color", QStringLiteral("toolbarText_"),
                         QStringLiteral("color"), textPalette);
        createColorPopup(fillPopup, "fillColorPopup", "Fill color", QStringLiteral("toolbarFill_"),
                         QStringLiteral("background"), fillPalette);
        createIconsPopup();
        for (auto *action : nodeEditingActions) action->setAutoRepeat(false);
        action("showHelp", tr("Keyboard shortcuts"), config.shortcuts.showHelp, [this] { showHelp(); })->setAutoRepeat(false);
        // Snapshot configured bindings before inline editing temporarily clears them.
        shortcutHelpText = shortcutHelp();
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(view, &QWidget::customContextMenuRequested, editor, [this, zoomIn, zoomOut, resetZoom, fit](const QPoint &point) {
            const QPointer<MindMapEditor> guard(host);
            if (!view->finishTopicEdit(true) || !guard) return;
            const QString nodeId = controller->selectedNodeId();
            const bool hasNode = nodeId.isEmpty() == false;
            const QString linkId = controller->selectedLinkId();
            QMenu menu(host);
            if (controller->selectedNodeIds().size() > 1) {
                menu.addAction(deleteSelection);
            } else if (hasNode) {
                const auto node = controller->nodeProperties(nodeId);
                if (!guard || node.id.isEmpty()) return;
                menu.addActions({addChild, addSibling, addSiblingBefore});
                menu.addSeparator();
                menu.addAction(editSelection);
                menu.addAction(tr("Add URL"), host, [this, nodeId] {
                    if (!controller->isReadOnly()) host->onAddUrl(nodeId);
                })->setEnabled(!controller->isReadOnly());
                menu.addAction(tr("Add Image"), host, [this, nodeId] {
                    if (!controller->isReadOnly()) host->onAddImage(nodeId);
                })->setEnabled(!controller->isReadOnly());
                auto *branchMenu = menu.addMenu(tr("Branch Color"));
                branchMenu->setEnabled(!controller->isReadOnly());
                auto *branchGroup = new QActionGroup(branchMenu);
                branchGroup->setExclusive(true);
                auto addBranchColor = [&](const QString &label, const QColor &color, const QJsonValue &value) {
                    auto *action = branchMenu->addAction(label);
                    action->setCheckable(true);
                    branchGroup->addAction(action);
                    action->setChecked(color == node.style.branchColor);
                    if (color.isValid()) {
                        QPixmap swatch(16, 16);
                        swatch.fill(color);
                        QPainter painter(&swatch);
                        painter.setPen(branchMenu->palette().color(QPalette::Mid));
                        painter.drawRect(0, 0, 15, 15);
                        painter.end();
                        action->setIcon(QIcon(swatch));
                    }
                    QObject::connect(action, &QAction::triggered, host, [this, nodeId, value] {
                        const QJsonObject patch{{QStringLiteral("style"),
                            QJsonObject{{QStringLiteral("branchColor"), value}}}};
                        controller->updateNodeProperties(nodeId, QJsonDocument(patch).toJson(QJsonDocument::Compact));
                    });
                };
                addBranchColor(tr("Auto"), QColor(), QJsonValue(QJsonValue::Null));
                for (const auto &entry : nodeColors) {
                    const QString hex = QString::fromLatin1(entry.hex);
                    addBranchColor(tr("%1 (%2)").arg(QCoreApplication::translate("m3::qt::NodeColors", entry.name), hex), QColor(hex), hex);
                }
                menu.addSeparator();
                menu.addActions({deleteSelection, up, down});
            } else if (!linkId.isEmpty()) {
                const auto link = controller->linkChoice(linkId);
                if (!guard || link.id.isEmpty()) return;
                menu.addAction(editSelection);
                menu.addSeparator();
                auto *group = new QActionGroup(&menu);
                group->setExclusive(true);
                const struct { const char *text; LinkDirection direction; } choices[] = {
                    {"---", LinkDirection::None}, {"<---", LinkDirection::Backward},
                    {"--->", LinkDirection::Forward}, {"<--->", LinkDirection::Both}
                };
                for (const auto &choice : choices) {
                    auto *action = menu.addAction(QString::fromLatin1(choice.text));
                    action->setEnabled(!controller->isReadOnly());
                    action->setCheckable(true);
                    group->addAction(action);
                    action->setChecked(link.direction == choice.direction);
                    QObject::connect(action, &QAction::triggered, host, [this, linkId, direction = choice.direction] {
                        controller->setLinkDirection(linkId, direction);
                    });
                }
                menu.addSeparator();
                menu.addAction(deleteSelection);
            } else {
                menu.addActions({rootSelection, fit, zoomIn, zoomOut, resetZoom});
            }
            if (hasNode || !linkId.isEmpty() || controller->selectedNodeIds().size() > 1) {
                menu.addSeparator();
                menu.addAction(copySelection);
            }
            menu.exec(view->mapToGlobal(point));
        });
        QObject::connect(controller, &MindMapController::documentChanged, editor, [this, editor] {
            const QPointer<MindMapEditor> guard(editor);
            updateActions();
            if (guard) emit editor->documentChanged();
        });
        QObject::connect(controller, &MindMapController::undoAvailable, undoAction, &QAction::setEnabled);
        QObject::connect(controller, &MindMapController::redoAvailable, redoAction, &QAction::setEnabled);
        QObject::connect(controller, &MindMapController::undoAvailable, editor, &MindMapEditor::undoAvailable);
        QObject::connect(controller, &MindMapController::redoAvailable, editor, &MindMapEditor::redoAvailable);
        QObject::connect(controller, &MindMapController::selectionChanged, editor, [this, editor](const QString &node, const QString &link) {
            const QPointer<MindMapEditor> guard(editor);
            invalidateFormatting();
            if (!guard) return;
            updateActions();
            if (guard) emit editor->selectionChanged(node, link);
        });
        QObject::connect(controller, &MindMapController::errorOccurred, editor, [this, editor](const QString &message) {
            error->setText(message); error->show(); emit editor->errorOccurred(message);
        });
        QObject::connect(controller, &MindMapController::commandSucceeded, editor, [this] { error->clear(); error->hide(); updateActions(); });
        QObject::connect(controller, &MindMapController::imageRequested, editor, &MindMapEditor::imageRequested);
        QObject::connect(controller, &MindMapController::layoutDirectionChanged, editor, &MindMapEditor::layoutDirectionChanged);
        QObject::connect(view, &MindMapView::zoomFactorChanged, editor, &MindMapEditor::zoomFactorChanged);
        QObject::connect(view, &MindMapView::pendingEditChanged, editor, &MindMapEditor::pendingEditChanged);
        QObject::connect(view, &MindMapView::nodePicked, controller, &MindMapController::selectNode);
        QObject::connect(view, &MindMapView::nodeSelectionToggled, controller, &MindMapController::toggleNodeSelection);
        QObject::connect(view, &MindMapView::nodeLinkActivated, editor, [this, editor](const QString &id, const QString &url) {
            const QString resolved = controller->resolveResourceUrl(url);
            emit editor->nodeLinkActivated(id, resolved);
        });
        QObject::connect(view, &MindMapView::fileDropped, editor, [this, editor](const QString &nodeId, const QString &filePath) {
            if (controller->isReadOnly()) return;
            const QPointer<MindMapEditor> guard(editor);
            const QString resolvedUrl = editor->resolveDroppedFileUrl(filePath);
            if (!guard || controller->isReadOnly() || resolvedUrl.isEmpty()) return;
            const QJsonObject patch{{QStringLiteral("hyperLink"), resolvedUrl}};
            const QByteArray json = QJsonDocument(patch).toJson(QJsonDocument::Compact);
            controller->updateNodeProperties(nodeId, json);
        });
        QObject::connect(view, &MindMapView::imageResizeRequested, controller,
            [this](const QString &id, const QString &url, const QSizeF &original, const QSizeF &size) {
                const auto node = controller->nodeProperties(id);
                if (controller->selectedNodeId() != id || !node.image || node.image->url != url ||
                    QSizeF(node.image->width, node.image->height) != original) return;
                const QJsonObject image{{QStringLiteral("url"), url},
                    {QStringLiteral("width"), size.width()}, {QStringLiteral("height"), size.height()}};
                controller->updateNodeProperties(id,
                    QJsonDocument(QJsonObject{{QStringLiteral("image"), image}}).toJson(QJsonDocument::Compact));
            });
        QObject::connect(view, &MindMapView::nodeMoveRequested, controller, &MindMapController::moveNode);
        QObject::connect(view, &MindMapView::linkPicked, controller, &MindMapController::selectLink);
        QObject::connect(view, &MindMapView::emptyPicked, controller, &MindMapController::clearSelection);
        QObject::connect(view, &MindMapView::expansionRequested, controller, &MindMapController::setExpanded);
        QObject::connect(view, &MindMapView::appearanceChanged, controller, &MindMapController::refreshAppearance);
        QObject::connect(view, &MindMapView::appearanceChanged, editor, [this] { syncFormatting(); });
        QObject::connect(view, &MindMapView::editRequested, editSelection, &QAction::trigger);
        QObject::connect(view, &MindMapView::linkEndpointChangeRequested, controller, &MindMapController::reconnectLink);
        QObject::connect(view, &MindMapView::linkCreationRequested, editor, [this](const QString &source, const QString &target) {
            const QPointer<MindMapEditor> guard(host);
            const QString created = controller->addLink(source, target, false, {});
            if (guard && !created.isEmpty() && controller->selectedLinkId() == created)
                view->beginLinkTopicEdit(created, config.shortcuts.acceptTopic);
        });
        qApp->installEventFilter(this);
        controller->newDocument(QStringLiteral("Central topic"));
        updateActions();
    }
};
MindMapEditor::MindMapEditor(QWidget *parent) : MindMapEditor(EditorConfig{}, parent) {}
MindMapEditor::MindMapEditor(const EditorConfig &config, QWidget *parent)
    : QWidget(parent) {
    initializeM3Resources();
    d = std::make_unique<Private>(this, config);
}
MindMapEditor::~MindMapEditor() {
    d->invalidateFormatting();
    // Disarm the input before QWidget teardown can send it a committing FocusOut.
    delete d->view;
}
QAction *MindMapEditor::commandAction(const QString &name) const {
    return name.isEmpty() ? nullptr : findChild<QAction *>(name, Qt::FindDirectChildrenOnly);
}
QString MindMapEditor::resolveDroppedFileUrl(const QString &filePath) const {
    return QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded);
}
void MindMapEditor::onAddUrl(const QString &) {}
void MindMapEditor::onAddImage(const QString &) {}
bool MindMapEditor::newDocument(const QString &topic) {
    const QPointer<MindMapEditor> guard(this);
    d->invalidateFormatting();
    return guard && d->controller->newDocument(topic);
}
bool MindMapEditor::loadJson(const QByteArray &json) {
    const QPointer<MindMapEditor> guard(this);
    d->invalidateFormatting();
    return guard && d->controller->loadJson(json);
}
bool MindMapEditor::commitActiveEdit(bool keepEditing) { return d->view->finishTopicEdit(true, false, keepEditing); }
bool MindMapEditor::hasPendingEdit() const { return d->view->hasPendingEdit(); }
void MindMapEditor::setReadOnly(bool value) {
    const QPointer<MindMapEditor> guard(this);
    if (value != d->controller->isReadOnly()) d->invalidateFormatting();
    if (!guard) return;
    d->controller->setReadOnly(value);
    if (!guard) return;
    if (d->controller->isReadOnly() && d->mutationDialog) d->mutationDialog->reject();
    if (guard) d->updateActions();
}
bool MindMapEditor::isReadOnly() const { return d->controller->isReadOnly(); }
bool MindMapEditor::canUndo() const { return d->controller->canUndo(); }
bool MindMapEditor::canRedo() const { return d->controller->canRedo(); }
bool MindMapEditor::undo() { return d->controller->undo(); }
bool MindMapEditor::redo() { return d->controller->redo(); }
QByteArray MindMapEditor::nodeJson(const QString &id) const { return d->controller->nodeJson(id); }
bool MindMapEditor::updateNode(const QString &id, const QByteArray &patch) { return d->controller->updateNodeProperties(id, patch); }
QByteArray MindMapEditor::toJson() const { return d->controller->toJson(); }
QString MindMapEditor::toMarkdown() const { return d->controller->toMarkdown(); }
QString MindMapEditor::toHtml() const { return d->controller->toHtml(); }
QString MindMapEditor::lastError() const { return d->controller->lastError(); }
QString MindMapEditor::resourceBasePath() const { return d->controller->resourceBasePath(); }
void MindMapEditor::provideImage(const QString &url, quint64 requestId, const QImage &image) { d->controller->provideImage(url, requestId, image); }
void MindMapEditor::reloadImages() { d->controller->reloadImages(); }
void MindMapEditor::setResourceBasePath(const QString &path) { d->controller->setResourceBasePath(path); }
QString MindMapEditor::addNode(const QString &parent, const QString &topic, int index) { return d->controller->addNode(parent, topic, index); }
bool MindMapEditor::renameNode(const QString &id, const QString &topic) { return d->controller->renameNode(id, topic); }
bool MindMapEditor::removeNode(const QString &id) { return d->controller->removeNode(id); }
bool MindMapEditor::moveNode(const QString &id, const QString &parent, int index) { return d->controller->moveNode(id, parent, index); }
bool MindMapEditor::setExpanded(const QString &id, bool expanded) { return d->controller->setExpanded(id, expanded); }
QString MindMapEditor::addLink(const QString &source, const QString &target, bool directed, const QString &topic) { return d->controller->addLink(source, target, directed, topic); }
bool MindMapEditor::updateLink(const QString &id, const QString &source, const QString &target, bool directed, const QString &topic) { return d->controller->updateLink(id, source, target, directed, topic); }
bool MindMapEditor::removeLink(const QString &id) { return d->controller->removeLink(id); }
QVector<OutlineEntry> MindMapEditor::outline() const { return d->controller->outline(); }
bool MindMapEditor::revealNode(const QString &id) { return d->controller->revealNode(id); }
FindResult MindMapEditor::findText(const QString &text, Qt::CaseSensitivity sensitivity, bool backward, bool incremental) {
    return d->controller->findText(text, sensitivity, backward, incremental);
}
void MindMapEditor::clearFind() { d->controller->clearFind(); }
bool MindMapEditor::selectNode(const QString &id) { return d->controller->selectNode(id); }
bool MindMapEditor::selectLink(const QString &id) { return d->controller->selectLink(id); }
void MindMapEditor::clearSelection() { d->controller->clearSelection(); }
QString MindMapEditor::selectedNodeId() const { return d->controller->selectedNodeId(); }
QStringList MindMapEditor::selectedNodeIds() const { return d->controller->selectedNodeIds(); }
QString MindMapEditor::selectedLinkId() const { return d->controller->selectedLinkId(); }
QString MindMapEditor::selectedText() const {
    QWidget *focus = QApplication::focusWidget();
    if (focus && isAncestorOf(focus)) {
        for (auto *input = focus; input && input != this; input = input->parentWidget()) {
            if (const auto *line = qobject_cast<QLineEdit *>(input)) return line->selectedText();
            if (const auto *plain = qobject_cast<QPlainTextEdit *>(input))
                return plain->textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            if (const auto *text = qobject_cast<QTextEdit *>(input))
                return text->textCursor().selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        }
    }
    return d->controller->selectedText();
}
bool MindMapEditor::setLayoutDirection(LayoutDirection direction) { return d->controller->setLayoutDirection(direction); }
MindMapEditor::LayoutDirection MindMapEditor::layoutDirection() const { return d->controller->layoutDirection(); }
void MindMapEditor::fitToContents() { d->view->fitContents(); }
qreal MindMapEditor::zoomFactor() const { return d->view->transform().m11(); }
void MindMapEditor::zoom(qreal factor) { d->view->zoom(factor); }
void MindMapEditor::resetZoom() { d->view->resetZoom(); }
void MindMapEditor::scrollSteps(int horizontal, int vertical) { d->view->scrollSteps(horizontal, vertical); }
bool MindMapEditor::focusRoot() {
    const QPointer<MindMapEditor> guard(this);
    if (!commitActiveEdit() || !guard) return false;
    const auto nodes = d->controller->choices();
    if (!guard || nodes.empty() || !d->controller->selectNode(nodes.front().id)) return false;
    if (!guard) return true;
    d->view->centerNode(nodes.front().id);
    d->view->setFocus(Qt::OtherFocusReason);
    return true;
}
}
