#include "m3/qt/editor.h"
#include "mindmap_controller.h"
#include "mindmap_view.h"
#include "emoji_line_edit.h"
#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
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
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <functional>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QUrl>
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
class NodePropertiesPanel final : public QFrame {
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
        fontSize->setAccessibleName(tr("Font size"));
        fontSize->addItem(tr("Default"), 0.0);
        for (const int size : {10, 12, 14, 16, 18, 20, 24, 28, 32, 40, 48, 64})
            fontSize->addItem(tr("%1 px").arg(size), double(size));
        presetCount = fontSize->count();
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
        auto *palette = new QGridLayout;
        palette->setSpacing(6);
        static const QString swatchStyleSheet = loadStyleSheet(QStringLiteral(":/m3/qt/color_swatch.qss"));
        auto makeSwatch = [this](const QString &background, const QString &contrast) {
            auto *button = new QToolButton(body);
            button->setCheckable(true);
            button->setFocusPolicy(Qt::StrongFocus);
            button->setMinimumWidth(24);
            button->setFixedHeight(26);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            button->setStyleSheet(swatchStyleSheet.arg(background, contrast));
            return button;
        };
        defaultColor = makeSwatch(QStringLiteral("palette(button)"), QStringLiteral("palette(button-text)"));
        defaultColor->setObjectName(QStringLiteral("nodeDefaultColor"));
        defaultColor->setText(tr("Auto"));
        defaultColor->setToolButtonStyle(Qt::ToolButtonTextOnly);
        defaultColor->setAccessibleName(tr("Default color"));
        palette->addWidget(defaultColor, 0, 0);
        struct Swatch { const char *hex; const char *name; };
        const Swatch colors[] = {
            {"#ffffff", QT_TR_NOOP("White")}, {"#ecf0f1", QT_TR_NOOP("Cloud")},
            {"#95a5a6", QT_TR_NOOP("Gray")}, {"#34495e", QT_TR_NOOP("Slate")},
            {"#2c3e50", QT_TR_NOOP("Midnight")}, {"#000000", QT_TR_NOOP("Black")},
            {"#e74c3c", QT_TR_NOOP("Red")}, {"#e67e22", QT_TR_NOOP("Orange")},
            {"#f39c12", QT_TR_NOOP("Amber")}, {"#f1c40f", QT_TR_NOOP("Yellow")},
            {"#2ecc71", QT_TR_NOOP("Green")}, {"#27ae60", QT_TR_NOOP("Forest")},
            {"#1abc9c", QT_TR_NOOP("Teal")}, {"#16a085", QT_TR_NOOP("Jade")},
            {"#3498db", QT_TR_NOOP("Blue")}, {"#2980b9", QT_TR_NOOP("Ocean")},
            {"#9b59b6", QT_TR_NOOP("Purple")}, {"#8e44ad", QT_TR_NOOP("Violet")},
            {"#ffb6c1", QT_TR_NOOP("Pink")}, {"#f4a6a6", QT_TR_NOOP("Rose")},
            {"#ffd3a5", QT_TR_NOOP("Peach")}, {"#a8e6cf", QT_TR_NOOP("Mint")},
            {"#a9d6f5", QT_TR_NOOP("Sky")}
        };
        for (const auto &entry : colors) {
            const QString hex = QString::fromLatin1(entry.hex);
            const QString description = tr("%1 (%2)").arg(tr(entry.name), hex);
            const QString contrast = QColor(hex).lightnessF() > 0.55 ? QStringLiteral("#202020") : QStringLiteral("#ffffff");
            auto *button = makeSwatch(hex, contrast);
            button->setObjectName(QStringLiteral("nodeColor_") + hex.mid(1));
            button->setProperty("color", hex);
            button->setAccessibleName(description);
            button->setToolTip(description);
            const int index = int(swatches.size()) + 1;
            palette->addWidget(button, index / 6, index % 6);
            swatches.append(button);
            connect(button, &QToolButton::toggled, this, [this, hex](bool checked) {
                if (checked) applyStyle({{colorKey(), hex}});
                else refreshColors();
            });
        }
        content->addLayout(palette);
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
            reposition();
            toggle->setFocus(Qt::OtherFocusReason);
        });
        auto *collapse = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        collapse->setContext(Qt::WidgetWithChildrenShortcut);
        connect(collapse, &QShortcut::activated, this, [this] {
            toggle->setChecked(false);
            if (view) view->setFocus(Qt::OtherFocusReason);
        });
        connect(fontSize, &QComboBox::currentIndexChanged, this, [this] {
            const double size = fontSize->currentData().toDouble();
            applyStyle({{QStringLiteral("fontSize"), size > 0 ? QJsonValue(size) : QJsonValue(QJsonValue::Null)}});
        });
        connect(bold, &QToolButton::toggled, this, [this](bool checked) {
            applyStyle({{QStringLiteral("fontWeight"), checked ? QStringLiteral("bold") : QStringLiteral("normal")}});
        });
        connect(italic, &QToolButton::toggled, this, [this](bool checked) {
            applyStyle({{QStringLiteral("fontStyle"), checked ? QStringLiteral("italic") : QStringLiteral("normal")}});
        });
        connect(defaultColor, &QToolButton::toggled, this, [this](bool checked) {
            if (checked) applyStyle({{colorKey(), QJsonValue(QJsonValue::Null)}});
            else refreshColors();
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
        connect(note, &QPlainTextEdit::textChanged, this, [this] {
            apply({{QStringLiteral("note"), note->toPlainText()}});
        });
        connect(controller, &MindMapController::selectionChanged, this, [this] { refresh(); });
        connect(controller, &MindMapController::documentChanged, this, [this] { refresh(); });
        host->installEventFilter(this);
        view->installEventFilter(this);
        view->viewport()->installEventFilter(this);
        hide();
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
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
    QToolButton *toggle, *bold, *italic, *reset, *textColor, *fillColor, *defaultColor;
    QLineEdit *tags, *url;
    EmojiLineEdit *icons;
    QPlainTextEdit *note;
    QList<QToolButton *> swatches;
    QString boundId;
    NodeStyle currentStyle;
    int presetCount = 0;
    bool refreshing = false;
    static QStringList commaValues(const QString &text) {
        QStringList values;
        for (const auto &part : text.split(QLatin1Char(','))) {
            const QString value = part.trimmed();
            if (!value.isEmpty()) values.append(value);
        }
        return values;
    }
    QString colorKey() const {
        return fillColor->isChecked() ? QStringLiteral("background") : QStringLiteral("color");
    }
    void applyStyle(const QJsonObject &style) {
        apply({{QStringLiteral("style"), style}});
    }
    void apply(const QJsonObject &patch) {
        if (refreshing || boundId.isEmpty()) return;
        const QString id = boundId;
        const QByteArray json = QJsonDocument(patch).toJson(QJsonDocument::Compact);
        controller->updateNodeProperties(id, json);
        // Re-read even after a no-op or a nested load/new from a host signal handler.
        refresh();
    }
    void refreshColors() {
        const QColor color = fillColor->isChecked() ? currentStyle.backgroundColor : currentStyle.textColor;
        for (auto *button : swatches) {
            const QSignalBlocker blocker(button);
            button->setChecked(color.isValid() && color == QColor(button->property("color").toString()));
        }
        const QSignalBlocker blocker(defaultColor);
        defaultColor->setChecked(!color.isValid());
        defaultColor->setToolTip(fillColor->isChecked() ? tr("Use the default fill") : tr("Use the default text color"));
    }
    void refresh() {
        const auto properties = controller->nodeProperties(controller->selectedNodeId());
        const bool sameNode = properties.id.isEmpty() == false && properties.id == boundId;
        const QScopedValueRollback<bool> guard(refreshing, true);
        boundId = properties.id;
        currentStyle = properties.style;
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
        if (!sameNode || note->toPlainText() != properties.note) {
            const QSignalBlocker blocker(note);
            note->setPlainText(properties.note);
        }
        {
            const QSignalBlocker blocker(fontSize);
            int index = fontSize->findData(double(properties.style.fontSize));
            if (index < 0) {
                if (fontSize->count() > presetCount) fontSize->removeItem(presetCount);
                fontSize->addItem(tr("%1 px").arg(QString::number(properties.style.fontSize, 'g', 6)), double(properties.style.fontSize));
                index = presetCount;
            } else if (index < presetCount && fontSize->count() > presetCount) {
                fontSize->removeItem(presetCount);
            }
            fontSize->setCurrentIndex(index);
        }
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
        if (!view || boundId.isEmpty()) { hide(); return; }
        const QRect viewport(view->viewport()->mapTo(parentWidget(), QPoint()), view->viewport()->size());
        const QRect available = viewport.intersected(parentWidget()->rect()).adjusted(12, 12, -12, -12);
        if (available.isEmpty()) { hide(); return; }
        const bool expanded = toggle->isChecked();
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
    }
};
}
class MindMapEditor::Private {
public:
    MindMapEditor *host;
    const EditorConfig config;
    MindMapView *view;
    MindMapController *controller;
    QLabel *error;
    QToolBar *toolbar;
    QComboBox *direction;
    QAction *addChild, *addSibling, *addSiblingBefore, *editSelection, *deleteSelection, *toggleExpanded, *move, *up, *down, *addLink;
    QAction *rootSelection, *clearSelectionAction;
    QList<QAction *> nodeNavigation;
    enum class TopicOperation { Child, SiblingAfter, SiblingBefore };
    enum class Navigation { Parent, Child, PreviousSibling, NextSibling };
    QList<QAction *> menuActions;
    QAction *action(const char *name, const QString &text, const QList<QKeySequence> &shortcuts, std::function<void()> command, bool showInToolbar = true) {
        auto *result = new QAction(text, host);
        result->setObjectName(QString::fromLatin1(name));
        result->setShortcuts(shortcuts);
        result->setShortcutVisibleInContextMenu(true);
        QStringList keys;
        for (const auto &shortcut : shortcuts) keys.append(shortcut.toString(QKeySequence::NativeText));
        result->setToolTip(keys.isEmpty() ? text : text + QStringLiteral(" (%1)").arg(keys.join(QStringLiteral(", "))));
        result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        view->addAction(result);
        if (showInToolbar) {
            toolbar->addAction(result);
            menuActions.append(result);
        }
        QObject::connect(view, &MindMapView::topicEditingChanged, result, [result, shortcuts](bool editing) {
            result->setShortcuts(editing ? QList<QKeySequence>{} : shortcuts);
        });
        QObject::connect(result, &QAction::triggered, host, [this, command = std::move(command)] {
            view->finishTopicEdit(true);
            command();
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
    void updateActions() {
        const auto nodes = controller->choices();
        const auto *node = choice(nodes, controller->selectedNodeId());
        const bool hasLink = controller->selectedLinkId().isEmpty() == false;
        const bool movable = node && !node->parent.isEmpty();
        addChild->setEnabled(node); addLink->setEnabled(node);
        addSibling->setEnabled(node); addSiblingBefore->setEnabled(node);
        for (auto *action : nodeNavigation) action->setEnabled(node);
        rootSelection->setEnabled(!nodes.empty());
        clearSelectionAction->setEnabled(node || hasLink);
        editSelection->setEnabled(node || hasLink);
        deleteSelection->setEnabled(movable || hasLink);
        move->setEnabled(movable);
        toggleExpanded->setEnabled(node && !node->children.isEmpty());
        toggleExpanded->setText(node && !node->expanded ? tr("Expand") : tr("Collapse"));
        const auto *parent = node ? choice(nodes, node->parent) : nullptr;
        const auto index = parent ? parent->children.indexOf(node->id) : -1;
        up->setEnabled(parent && index > 0);
        down->setEnabled(parent && index >= 0 && index + 1 < parent->children.size());
        QSignalBlocker blocker(direction);
        direction->setCurrentIndex(direction->findData(int(controller->layoutDirection())));
    }
    void createNode(TopicOperation operation) {
        const QString id = controller->selectedNodeId();
        const auto nodes = controller->choices();
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
        if (!parent || (!parent->expanded && !controller->setExpanded(parentId, true))) return;
        const QString created = controller->addNode(parentId, QString(), index);
        if (!created.isEmpty()) view->beginTopicEdit(created, config.shortcuts.acceptTopic);
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
        const auto nodes = controller->choices();
        if (nodes.empty()) return;
        const QString id = controller->selectedLinkId();
        const auto link = insert ? LinkPresentation{} : controller->linkChoice(id);
        if (!insert && link.id.isEmpty()) return;
        QDialog dialog(host);
        dialog.setWindowTitle(insert ? tr("Add link") : tr("Edit link"));
        auto *form = new QFormLayout(&dialog);
        auto *source = new QComboBox(&dialog), *target = new QComboBox(&dialog);
        source->setObjectName(QStringLiteral("sourceNode")); target->setObjectName(QStringLiteral("targetNode"));
        populate(source, nodes); populate(target, nodes);
        source->setCurrentIndex(source->findData(insert ? controller->selectedNodeId() : link.source));
        target->setCurrentIndex(target->findData(insert ? controller->selectedNodeId() : link.target));
        auto *topic = new QLineEdit(link.topic, &dialog);
        topic->setObjectName(QStringLiteral("linkTopic"));
        auto *directed = new QCheckBox(tr("Directed"), &dialog);
        directed->setObjectName(QStringLiteral("directed")); directed->setChecked(insert || link.directed);
        form->addRow(tr("Source"), source); form->addRow(tr("Target"), target);
        form->addRow(tr("Topic"), topic); form->addRow(directed);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) {
            if (insert) controller->addLink(source->currentData().toString(), target->currentData().toString(), directed->isChecked(), topic->text());
            else controller->updateLink(id, source->currentData().toString(), target->currentData().toString(), directed->isChecked(), topic->text());
        }
    }
    void moveDialog() {
        const auto nodes = controller->choices();
        const QString id = controller->selectedNodeId();
        const auto *node = choice(nodes, id);
        if (!node || node->parent.isEmpty()) return;
        QSet<QString> excluded{id};
        // Choices arrive in child preorder: each excluded parent precedes its descendants.
        for (const auto &entry : nodes) if (excluded.contains(entry.parent)) excluded.insert(entry.id);
        QDialog dialog(host);
        dialog.setWindowTitle(tr("Move node"));
        auto *form = new QFormLayout(&dialog);
        auto *parent = new QComboBox(&dialog);
        parent->setObjectName(QStringLiteral("newParent"));
        populate(parent, nodes, excluded);
        auto *index = new QSpinBox(&dialog);
        index->setObjectName(QStringLiteral("insertIndex"));
        auto range = [&] {
            const auto *destination = choice(nodes, parent->currentData().toString());
            const int maximum = destination ? int(destination->children.size()) - (destination->id == node->parent ? 1 : 0) : 0;
            index->setRange(0, maximum);
            index->setValue(maximum);
        };
        QObject::connect(parent, &QComboBox::currentIndexChanged, &dialog, range);
        parent->setCurrentIndex(parent->findData(node->parent));
        range();
        const auto *oldParent = choice(nodes, node->parent);
        if (oldParent) index->setValue(int(oldParent->children.indexOf(id)));
        form->addRow(tr("New parent"), parent); form->addRow(tr("Insertion index (zero-based)"), index);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        form->addRow(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted) controller->moveNode(id, parent->currentData().toString(), index->value());
    }
    void reorder(int delta) {
        const auto nodes = controller->choices();
        const auto *node = choice(nodes, controller->selectedNodeId());
        const auto *parent = node ? choice(nodes, node->parent) : nullptr;
        if (parent) controller->moveNode(node->id, parent->id, int(parent->children.indexOf(node->id)) + delta);
    }
    Private(MindMapEditor *editor, const EditorConfig &settings) : host(editor), config(settings) {
        auto *layout = new QVBoxLayout(editor);
        view = new MindMapView(editor);
        error = new QLabel(editor);
        error->setTextFormat(Qt::PlainText); error->setWordWrap(true); error->hide();
        controller = new MindMapController(*view, editor);
        toolbar = new QToolBar(editor);
        layout->addWidget(toolbar);
        addChild = action("addChild", tr("Add child"), config.shortcuts.addChild, [this] { createNode(TopicOperation::Child); });
        addSibling = action("addSibling", tr("Add sibling"), config.shortcuts.addSibling, [this] { createNode(TopicOperation::SiblingAfter); });
        addSiblingBefore = action("addSiblingBefore", tr("Add sibling before"), config.shortcuts.addSiblingBefore, [this] { createNode(TopicOperation::SiblingBefore); });
        editSelection = action("editSelection", tr("Rename/Edit"), config.shortcuts.editSelection, [this] {
            if (controller->selectedLinkId().isEmpty())
                view->beginTopicEdit(controller->selectedNodeId(), config.shortcuts.acceptTopic);
            else linkDialog(false);
        });
        deleteSelection = action("deleteSelection", tr("Delete"), config.shortcuts.deleteSelection, [this] {
            const auto link = controller->selectedLinkId(), node = controller->selectedNodeId();
            if (!link.isEmpty()) controller->removeLink(link);
            else if (!node.isEmpty() && (!config.confirmSubtreeDeletion || QMessageBox::question(host, tr("Delete subtree"),
                tr("Delete this node and all its descendants?"), QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Yes))
                controller->removeNode(node);
        });
        toggleExpanded = action("toggleExpanded", tr("Expand/Collapse"), config.shortcuts.toggleExpanded, [this] {
            const auto nodes = controller->choices();
            const auto *node = choice(nodes, controller->selectedNodeId());
            if (node) controller->setExpanded(node->id, !node->expanded);
        });
        move = action("moveNode", tr("Move..."), config.shortcuts.moveNode, [this] { moveDialog(); });
        up = action("moveUp", tr("Move up"), config.shortcuts.moveUp, [this] { reorder(-1); });
        down = action("moveDown", tr("Move down"), config.shortcuts.moveDown, [this] { reorder(1); });
        addLink = action("addLink", tr("Add link"), config.shortcuts.addLink, [this] { linkDialog(true); });
        toolbar = new QToolBar(editor);
        layout->addWidget(toolbar);
        action("zoomIn", tr("Zoom +"), config.shortcuts.zoomIn, [this] { view->zoom(1.2); });
        action("zoomOut", tr("Zoom -"), config.shortcuts.zoomOut, [this] { view->zoom(1 / 1.2); });
        action("resetZoom", tr("100%"), config.shortcuts.resetZoom, [this] { view->resetZoom(); });
        action("fit", tr("Fit"), config.shortcuts.fit, [this] { view->fitContents(); });
        rootSelection = action("selectRoot", tr("Focus main node"), config.shortcuts.selectRoot, [this] { host->focusRoot(); });
        direction = new QComboBox(toolbar);
        direction->setObjectName(QStringLiteral("layoutDirection"));
        direction->addItem(tr("Balanced"), int(LayoutDirection::Balanced));
        direction->addItem(tr("Right"), int(LayoutDirection::Right));
        direction->addItem(tr("Left"), int(LayoutDirection::Left));
        direction->addItem(tr("Outline"), int(LayoutDirection::Outline));
        direction->setAccessibleName(tr("Layout direction"));
        toolbar->addWidget(direction);
        QObject::connect(direction, &QComboBox::currentIndexChanged, editor, [this] {
            const auto requested = static_cast<LayoutDirection>(direction->currentData().toInt());
            view->finishTopicEdit(true);
            controller->setLayoutDirection(requested);
            updateActions();
        });
        nodeNavigation = {
            action("selectParent", tr("Select parent"), config.shortcuts.selectParent, [this] { navigate(Navigation::Parent); }, false),
            action("selectChild", tr("Select first child"), config.shortcuts.selectChild, [this] { navigate(Navigation::Child); }, false),
            action("previousSibling", tr("Select previous sibling"), config.shortcuts.previousSibling, [this] { navigate(Navigation::PreviousSibling); }, false),
            action("nextSibling", tr("Select next sibling"), config.shortcuts.nextSibling, [this] { navigate(Navigation::NextSibling); }, false)
        };
        clearSelectionAction = action("clearSelection", tr("Clear selection"), config.shortcuts.clearSelection, [this] { controller->clearSelection(); }, false);
        layout->addWidget(view, 1); layout->addWidget(error);
        new NodePropertiesPanel(editor, view, controller);
        view->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(view, &QWidget::customContextMenuRequested, editor, [this](const QPoint &point) {
            view->finishTopicEdit(true);
            QMenu menu(host); menu.addActions(menuActions); menu.exec(view->mapToGlobal(point));
        });
        QObject::connect(controller, &MindMapController::documentChanged, editor, [this, editor] { updateActions(); emit editor->documentChanged(); });
        QObject::connect(controller, &MindMapController::selectionChanged, editor, [this, editor](const QString &node, const QString &link) {
            updateActions(); emit editor->selectionChanged(node, link);
        });
        QObject::connect(controller, &MindMapController::errorOccurred, editor, [this, editor](const QString &message) {
            error->setText(message); error->show(); emit editor->errorOccurred(message);
        });
        QObject::connect(controller, &MindMapController::commandSucceeded, editor, [this] { error->clear(); error->hide(); updateActions(); });
        QObject::connect(view, &MindMapView::nodePicked, controller, &MindMapController::selectNode);
        QObject::connect(view, &MindMapView::nodeLinkActivated, editor, &MindMapEditor::nodeLinkActivated);
        QObject::connect(view, &MindMapView::fileDropped, editor, [this, editor](const QString &nodeId, const QString &filePath) {
            const QString resolvedUrl = editor->resolveDroppedFileUrl(filePath);
            if (resolvedUrl.isEmpty()) return;
            const QJsonObject patch{{QStringLiteral("hyperLink"), resolvedUrl}};
            const QByteArray json = QJsonDocument(patch).toJson(QJsonDocument::Compact);
            controller->updateNodeProperties(nodeId, json);
        });
        QObject::connect(view, &MindMapView::nodeMoveRequested, controller, &MindMapController::moveNode);
        QObject::connect(view, &MindMapView::linkPicked, controller, &MindMapController::selectLink);
        QObject::connect(view, &MindMapView::emptyPicked, controller, &MindMapController::clearSelection);
        QObject::connect(view, &MindMapView::expansionRequested, controller, &MindMapController::setExpanded);
        QObject::connect(view, &MindMapView::appearanceChanged, controller, &MindMapController::refreshAppearance);
        QObject::connect(view, &MindMapView::editRequested, editSelection, &QAction::trigger);
        QObject::connect(view, &MindMapView::topicEditRequested, controller, &MindMapController::commitTopicEdit);
        controller->newDocument(QStringLiteral("Central topic"));
        updateActions();
    }
};
MindMapEditor::MindMapEditor(QWidget *parent) : MindMapEditor(EditorConfig{}, parent) {}
MindMapEditor::MindMapEditor(const EditorConfig &config, QWidget *parent)
    : QWidget(parent), d(std::make_unique<Private>(this, config)) {}
MindMapEditor::~MindMapEditor() {
    // Disarm the input before QWidget teardown can send it a committing FocusOut.
    delete d->view;
}
QString MindMapEditor::resolveDroppedFileUrl(const QString &filePath) const {
    return QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded);
}
bool MindMapEditor::newDocument(const QString &topic) { return d->controller->newDocument(topic); }
bool MindMapEditor::loadJson(const QByteArray &json) { return d->controller->loadJson(json); }
QByteArray MindMapEditor::toJson() const { return d->controller->toJson(); }
QString MindMapEditor::toMarkdown() const { return d->controller->toMarkdown(); }
QString MindMapEditor::lastError() const { return d->controller->lastError(); }
QString MindMapEditor::addNode(const QString &parent, const QString &topic, int index) { return d->controller->addNode(parent, topic, index); }
bool MindMapEditor::renameNode(const QString &id, const QString &topic) { return d->controller->renameNode(id, topic); }
bool MindMapEditor::removeNode(const QString &id) { return d->controller->removeNode(id); }
bool MindMapEditor::moveNode(const QString &id, const QString &parent, int index) { return d->controller->moveNode(id, parent, index); }
bool MindMapEditor::setExpanded(const QString &id, bool expanded) { return d->controller->setExpanded(id, expanded); }
QString MindMapEditor::addLink(const QString &source, const QString &target, bool directed, const QString &topic) { return d->controller->addLink(source, target, directed, topic); }
bool MindMapEditor::updateLink(const QString &id, const QString &source, const QString &target, bool directed, const QString &topic) { return d->controller->updateLink(id, source, target, directed, topic); }
bool MindMapEditor::removeLink(const QString &id) { return d->controller->removeLink(id); }
bool MindMapEditor::selectNode(const QString &id) { return d->controller->selectNode(id); }
bool MindMapEditor::selectLink(const QString &id) { return d->controller->selectLink(id); }
void MindMapEditor::clearSelection() { d->controller->clearSelection(); }
QString MindMapEditor::selectedNodeId() const { return d->controller->selectedNodeId(); }
QString MindMapEditor::selectedLinkId() const { return d->controller->selectedLinkId(); }
bool MindMapEditor::setLayoutDirection(LayoutDirection direction) { return d->controller->setLayoutDirection(direction); }
MindMapEditor::LayoutDirection MindMapEditor::layoutDirection() const { return d->controller->layoutDirection(); }
void MindMapEditor::fitToContents() { d->view->fitContents(); }
bool MindMapEditor::focusRoot() {
    d->view->finishTopicEdit(true);
    const auto nodes = d->controller->choices();
    if (nodes.empty() || !d->controller->selectNode(nodes.front().id)) return false;
    d->view->centerNode(nodes.front().id);
    d->view->setFocus(Qt::OtherFocusReason);
    return true;
}
}
