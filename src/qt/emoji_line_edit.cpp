#include "emoji_line_edit.h"
#include <QAbstractListModel>
#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFocusEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>

namespace m3::qt {
namespace {
constexpr int glyphRole = Qt::UserRole + 1;
constexpr char navigationHintShownProperty[] = "m3.emojiNavigationHintShown";
struct Emoji {
    QString glyph, name, searchable;
    int category;
};
struct Catalog {
    QStringList categories;
    QList<Emoji> entries;
};
const Catalog &catalog() {
    // Unicode 15.1, unmodified upstream data; notice embedded alongside the data.
    static const Catalog data = [] {
        QFile file(QStringLiteral(":/m3/emoji/emoji-test.txt"));
        if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot load embedded emoji catalog");
        Catalog result;
        QString group, line;
        QTextStream stream(&file);
        while (stream.readLineInto(&line)) {
            if (line.startsWith(QStringLiteral("# group: "))) {
                group = line.mid(9);
                continue;
            }
            const qsizetype semicolon = line.indexOf(QLatin1Char(';'));
            const qsizetype comment = line.indexOf(QLatin1Char('#'));
            if (semicolon < 0 || comment < semicolon ||
                line.mid(semicolon + 1, comment - semicolon - 1).trimmed() != QStringLiteral("fully-qualified")) continue;
            const QStringList codes = line.left(semicolon).simplified().split(QLatin1Char(' '));
            QList<char32_t> points;
            points.reserve(codes.size());
            for (const auto &code : codes) points.append(char32_t(code.toUInt(nullptr, 16)));
            const QString annotation = line.mid(comment + 1).trimmed();
            const qsizetype version = annotation.indexOf(QStringLiteral(" E"));
            const qsizetype nameStart = annotation.indexOf(QLatin1Char(' '), version + 2);
            if (group.isEmpty() || version < 0 || nameStart < 0) qFatal("Invalid embedded emoji catalog");
            const QString name = annotation.mid(nameStart + 1);
            if (result.categories.isEmpty() || result.categories.back() != group) result.categories.append(group);
            result.entries.append({QString::fromUcs4(points.constData(), points.size()), name,
                                   (name + QLatin1Char(' ') + group).toCaseFolded(), int(result.categories.size() - 1)});
        }
        if (result.entries.isEmpty()) qFatal("Empty embedded emoji catalog");
        return result;
    }();
    return data;
}
class EmojiModel final : public QAbstractListModel {
public:
    explicit EmojiModel(QObject *parent) : QAbstractListModel(parent) { rows.reserve(catalog().entries.size()); }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(rows.size()); }
    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows.size()) return {};
        const auto &entry = catalog().entries[rows[index.row()]];
        switch (role) {
        case Qt::DisplayRole:
        case Qt::ToolTipRole:
        case Qt::AccessibleTextRole: return entry.name;
        case glyphRole: return entry.glyph;
        default: return {};
        }
    }
    void filter(int category, const QString &query) {
        const QStringList terms = query.toCaseFolded().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        beginResetModel();
        rows.clear();
        const auto &entries = catalog().entries;
        for (qsizetype i = 0; i < entries.size(); ++i) {
            const auto &entry = entries[i];
            if (category >= 0 && entry.category != category) continue;
            if (std::all_of(terms.begin(), terms.end(), [&entry](const QString &term) {
                return entry.searchable.contains(term) || entry.glyph.contains(term);
            })) rows.append(int(i));
        }
        endResetModel();
    }
private:
    QList<int> rows;
};
class EmojiGridView final : public QListView {
public:
    explicit EmojiGridView(QWidget *parent) : QListView(parent) { updateGrid(); }
    void moveSelection(int key) {
        // IconMode's horizontal cursor actions follow model order, even in RTL.
        if (isRightToLeft() && (key == Qt::Key_Left || key == Qt::Key_Right))
            key = key == Qt::Key_Left ? Qt::Key_Right : Qt::Key_Left;
        QKeyEvent arrow(QEvent::KeyPress, key, Qt::NoModifier);
        QListView::keyPressEvent(&arrow);
    }
    void doItemsLayout() override {
        updateGrid();
        QListView::doItemsLayout();
    }
protected:
    void resizeEvent(QResizeEvent *event) override {
        QListView::resizeEvent(event);
        updateGrid();
    }
    void changeEvent(QEvent *event) override {
        QListView::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) updateGrid();
    }
private:
    void updateGrid() {
        int width = maximumViewportSize().width();
        // IconMode reserves the AsNeeded scrollbar gutter even while it is hidden.
        auto *bar = verticalScrollBar();
        if (verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
            style()->pixelMetric(QStyle::PM_ScrollView_ScrollBarOverlap, nullptr, bar) == 0) {
            width -= style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, bar);
            if (style()->styleHint(QStyle::SH_ScrollView_FrameOnlyAroundContents, nullptr, this)) {
                QStyleOption option;
                option.initFrom(this);
                width -= 2 * style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, this);
            }
        }
        // QListView wraps against QRect::right(), the inclusive boundary.
        width = qMax(1, width - 1);
        const QFontMetrics metrics = fontMetrics();
        const int preferredWidth = qMax(88, metrics.horizontalAdvance(QStringLiteral("MMMMMMMM")) + 12);
        const int columns = qMax(1, width / preferredWidth);
        const QSize cell(width / columns, 46 + metrics.height());
        if (gridSize() != cell) setGridSize(cell);
    }
};
class EmojiDelegate final : public QStyledItemDelegate {
public:
    explicit EmojiDelegate(QListView *view) : QStyledItemDelegate(view), view(*view) {}
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return view.gridSize(); }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        painter->save();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        const QRect bounds = option.rect.adjusted(2, 2, -2, -2);
        if (selected || option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(option.palette.color(selected ? QPalette::Highlight : QPalette::AlternateBase));
            painter->drawRoundedRect(bounds, 4, 4);
        }
        painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        QFont glyphFont = option.font;
        glyphFont.setPixelSize(28);
        painter->setFont(glyphFont);
        painter->drawText(QRect(bounds.left(), bounds.top(), bounds.width(), 36), Qt::AlignCenter,
                          index.data(glyphRole).toString());
        painter->setFont(option.font);
        painter->drawText(bounds.adjusted(3, 36, -3, 0), Qt::AlignHCenter | Qt::AlignTop,
                          option.fontMetrics.elidedText(index.data().toString(), Qt::ElideRight, bounds.width() - 6));
        painter->restore();
    }
private:
    const QListView &view;
};
bool within(QWidget *child, QWidget *parent) {
    return child && (child == parent || parent->isAncestorOf(child));
}
}
EmojiLineEdit::EmojiLineEdit(QWidget *parent) : QLineEdit(parent) {
    connect(this, &QLineEdit::textChanged, this, [this] {
        if (!inserting && hasFocus()) showPopup();
    });
    connect(this, &QLineEdit::cursorPositionChanged, this, [this] {
        if (!inserting && popup && popup->isVisible()) showPopup();
    });
}
EmojiLineEdit::~EmojiLineEdit() { dismissPopup(); }
void EmojiLineEdit::createPopup() {
    popup = new QFrame(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    popup->setObjectName(QStringLiteral("emojiPopup"));
    popup->setAccessibleName(tr("Choose an emoji"));
    popup->setAttribute(Qt::WA_ShowWithoutActivating);
    popup->setFrameShape(QFrame::StyledPanel);
    popup->setAutoFillBackground(true);
    popup->setBackgroundRole(QPalette::Base);
    popup->setFocusPolicy(Qt::NoFocus);
    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    categories = new QComboBox(popup);
    categories->setObjectName(QStringLiteral("emojiCategories"));
    categories->setAccessibleName(tr("Emoji categories"));
    categories->setFocusPolicy(Qt::NoFocus);
    categories->addItem(tr("All categories"));
    categories->addItems(catalog().categories);
    layout->addWidget(categories);
    choices = new EmojiGridView(popup);
    choices->setObjectName(QStringLiteral("emojiChoices"));
    choices->setAccessibleName(tr("Emoji results"));
    choices->setFocusPolicy(Qt::NoFocus);
    choices->setViewMode(QListView::IconMode);
    choices->setResizeMode(QListView::Adjust);
    choices->setMovement(QListView::Static);
    choices->setUniformItemSizes(true);
    choices->setSelectionMode(QAbstractItemView::SingleSelection);
    choices->setEditTriggers(QAbstractItemView::NoEditTriggers);
    choices->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    choices->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    choices->setMouseTracking(true);
    choices->setItemDelegate(new EmojiDelegate(choices));
    emojiModel = new EmojiModel(choices);
    choices->setModel(emojiModel);
    layout->addWidget(choices, 1);
    description = new QLabel(popup);
    description->setObjectName(QStringLiteral("emojiDescription"));
    description->setTextFormat(Qt::PlainText);
    description->setWordWrap(true);
    layout->addWidget(description);
    syncAppearance();
    connect(categories, &QComboBox::currentIndexChanged, this, [this] { filterMatches(); });
    connect(categories, &QComboBox::activated, this, [this] { queuePopupFocusCheck(); });
    connect(choices, &QListView::clicked, this, [this] { chooseCurrent(); });
    connect(choices, &QListView::entered, this, [this](const QModelIndex &index) {
        description->setText(index.data().toString());
    });
    connect(choices->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (index.isValid()) description->setText(index.data().toString());
    });
}
void EmojiLineEdit::syncAppearance() {
    // Stylesheet-backed parents do not propagate palette/font changes to tool windows.
    QWidget *widgets[] = {popup, categories, choices, description};
    for (auto *widget : widgets) {
        widget->setPalette(palette());
        widget->setFont(font());
    }
    description->setFixedHeight(fontMetrics().height() * 2 + 4);
}
QPair<int, int> EmojiLineEdit::tokenRange() const {
    const QString value = text();
    const int caret = cursorPosition();
    int start = caret == 0 ? 0 : int(value.lastIndexOf(QLatin1Char(','), caret - 1)) + 1;
    int end = int(value.indexOf(QLatin1Char(','), caret));
    if (end < 0) end = int(value.size());
    while (start < end && value[start].isSpace()) ++start;
    while (end > start && value[end - 1].isSpace()) --end;
    return {start, end};
}
void EmojiLineEdit::showPopup(bool all) {
    if (inserting || !isVisible() || !isEnabled() || isReadOnly()) return;
    if (!popup) createPopup();
    if (all) {
        const QSignalBlocker blocker(categories);
        categories->setCurrentIndex(0);
        query.clear();
    } else {
        const auto range = tokenRange();
        query = text().mid(range.first, range.second - range.first);
    }
    filterMatches();
    if (!positionPopup()) return;
    if (!popup->isVisible()) {
        qApp->installEventFilter(this);
        popup->show();
        queuePopupFocusCheck();
    }
}
void EmojiLineEdit::scheduleNavigationHint() {
    if (qApp->property(navigationHintShownProperty).toBool()) return;
    // Wait past focus/click dispatch: Qt dismisses tooltips on the opening release.
    QTimer::singleShot(0, this, [this] {
        if (focusCheckPending || !popup || !popup->isVisible() || !hasFocus() ||
            QGuiApplication::applicationState() != Qt::ApplicationActive ||
            QApplication::mouseButtons() != Qt::NoButton ||
            qApp->property(navigationHintShownProperty).toBool()) return;
        const QString text = tr("Ctrl+H/J/K/L for navigation");
        const QPointer<QWidget> owner(popup);
        const QPoint anchor = popup->mapToGlobal(popup->rect().topLeft());
        navigationHintText = text;
        qApp->setProperty(navigationHintShownProperty, true);
        if (owner && owner->isVisible()) QToolTip::showText(anchor, text, owner, QRect(), 5000);
    });
}
bool EmojiLineEdit::positionPopup() {
    if (visibleRegion().isEmpty()) { dismissPopup(); return false; }
    const QPoint anchor = mapToGlobal(QPoint(0, height()));
    QScreen *target = QGuiApplication::screenAt(anchor);
    if (!target) target = screen();
    const QRect available = target->availableGeometry();
    const int width = qMin(qMax(420, this->width()), available.width());
    const int height = qMin(360, available.height());
    const int top = anchor.y() + height <= available.bottom() + 1 ? anchor.y() : anchor.y() - this->height() - height;
    popup->setGeometry(std::clamp(anchor.x(), available.left(), available.right() + 1 - width),
                       std::clamp(top, available.top(), available.bottom() + 1 - height), width, height);
    return true;
}
void EmojiLineEdit::filterMatches() {
    static_cast<EmojiModel *>(emojiModel)->filter(categories->currentIndex() - 1, query);
    if (emojiModel->rowCount() == 0) {
        description->setText(tr("No matching emoji. Try another name or category."));
    } else {
        choices->setCurrentIndex(emojiModel->index(0, 0));
        choices->scrollToTop();
    }
}
bool EmojiLineEdit::ownsPopupWidget(QWidget *widget) const {
    return within(widget, popup) || within(widget, categories->view()->window());
}
void EmojiLineEdit::queuePopupFocusCheck() {
    if (focusCheckPending) return;
    focusCheckPending = true;
    // Native activation may precede button/focus updates. Inspect settled ownership,
    // including the category combo's separate popup, rather than mouse-button state.
    QTimer::singleShot(0, this, [this] {
        focusCheckPending = false;
        if (!popup || !popup->isVisible()) return;
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) { dismissPopup(); return; }
        if (categories->view()->window()->isVisible()) return;
        auto *activePopup = QApplication::activePopupWidget();
        auto *focus = QApplication::focusWidget();
        auto *active = QApplication::activeWindow();
        if ((activePopup && !ownsPopupWidget(activePopup)) ||
            (focus && focus != this && !ownsPopupWidget(focus)) ||
            (active && active != window() && !ownsPopupWidget(active))) {
            dismissPopup();
            return;
        }
        // Null focus/activation can be transient for a non-activating tool. Keep it
        // open, but only restore keyboard focus once one of our windows is active.
        if (!hasFocus() && active) {
            window()->activateWindow();
            setFocus(Qt::OtherFocusReason);
            return; // The resulting FocusIn will recheck ownership before showing a tip.
        }
        scheduleNavigationHint();
    });
}
void EmojiLineEdit::dismissPopup() {
    qApp->removeEventFilter(this);
    if (!navigationHintText.isEmpty() && QToolTip::text() == navigationHintText) QToolTip::hideText();
    navigationHintText.clear();
    if (categories) categories->hidePopup();
    if (popup) popup->hide();
}
void EmojiLineEdit::chooseCurrent() {
    const QString glyph = choices->currentIndex().data(glyphRole).toString();
    if (glyph.isEmpty()) return;
    auto range = tokenRange();
    if (hasSelectedText()) {
        const int start = selectionStart(), end = start + int(selectedText().size());
        // Tab/shortcut focus can select the whole list; never replace neighboring entries.
        if (start >= range.first && end <= range.second) range = {start, end};
    }
    const QString replacement = glyph + (range.second == text().size() ? QStringLiteral(", ") : QString());
    dismissPopup();
    inserting = true;
    QPointer<EmojiLineEdit> guard(this);
    setSelection(range.first, range.second - range.first);
    if (!guard) return;
    // One native edit: preserves undo and the host's synchronous textChanged contract.
    insert(replacement);
    if (guard) inserting = false;
}
void EmojiLineEdit::focusInEvent(QFocusEvent *event) {
    QLineEdit::focusInEvent(event);
    if (!popup || !popup->isVisible()) showPopup(true);
}
void EmojiLineEdit::focusOutEvent(QFocusEvent *event) {
    QLineEdit::focusOutEvent(event);
    if (popup && popup->isVisible()) queuePopupFocusCheck();
}
void EmojiLineEdit::mousePressEvent(QMouseEvent *event) {
    QLineEdit::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && (!popup || !popup->isVisible())) showPopup(true);
}
bool EmojiLineEdit::event(QEvent *event) {
    if (description && (event->type() == QEvent::FontChange || event->type() == QEvent::PaletteChange))
        syncAppearance();
    if (event->type() == QEvent::Hide || event->type() == QEvent::EnabledChange || event->type() == QEvent::ReadOnlyChange)
        dismissPopup();
    if (popup && popup->isVisible() && (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::ControlModifier && !categories->view()->isVisible()) {
            int direction = 0;
            switch (key->key()) {
            case Qt::Key_H: direction = Qt::Key_Left; break;
            case Qt::Key_J: direction = Qt::Key_Down; break;
            case Qt::Key_K: direction = Qt::Key_Up; break;
            case Qt::Key_L: direction = Qt::Key_Right; break;
            default: break;
            }
            if (direction) {
                if (event->type() == QEvent::KeyPress)
                    static_cast<EmojiGridView *>(choices)->moveSelection(direction);
                event->accept();
                return true;
            }
        }
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) dismissPopup();
        else if (event->type() == QEvent::ShortcutOverride &&
                 (key->key() == Qt::Key_Up || key->key() == Qt::Key_Down || key->key() == Qt::Key_Return ||
                  key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape ||
                  ((key->key() == Qt::Key_PageUp || key->key() == Qt::Key_PageDown) && key->modifiers() == Qt::ControlModifier))) {
            event->accept();
            return true;
        }
    }
    return QLineEdit::event(event);
}
void EmojiLineEdit::keyPressEvent(QKeyEvent *event) {
    if (popup && popup->isVisible()) {
        const int key = event->key();
        if (key == Qt::Key_Escape) { dismissPopup(); event->accept(); return; }
        if (key == Qt::Key_Return || key == Qt::Key_Enter) { chooseCurrent(); event->accept(); return; }
        if ((key == Qt::Key_PageUp || key == Qt::Key_PageDown) && event->modifiers() == Qt::ControlModifier) {
            categories->setCurrentIndex((categories->currentIndex() + (key == Qt::Key_PageDown ? 1 : categories->count() - 1)) % categories->count());
            event->accept();
            return;
        }
        if ((key == Qt::Key_Down || key == Qt::Key_Up) && event->modifiers() == Qt::NoModifier) {
            if (emojiModel->rowCount() > 0) {
                const int row = std::clamp(choices->currentIndex().row() + (key == Qt::Key_Down ? 1 : -1),
                                           0, emojiModel->rowCount() - 1);
                choices->setCurrentIndex(emojiModel->index(row, 0));
                choices->scrollTo(choices->currentIndex());
            }
            event->accept();
            return;
        }
    }
    QLineEdit::keyPressEvent(event);
}
bool EmojiLineEdit::eventFilter(QObject *watched, QEvent *event) {
    if (!popup || !popup->isVisible()) return false;
    auto *widget = qobject_cast<QWidget *>(watched);
    // QWindow receives native mouse events before forwarding them to the QWidget.
    if (event->type() == QEvent::MouseButtonPress && widget) {
        if (!ownsPopupWidget(widget) && !within(widget, this)) dismissPopup();
    } else if (event->type() == QEvent::MouseButtonRelease && widget &&
               (ownsPopupWidget(widget) || within(widget, this))) {
        scheduleNavigationHint();
    } else if (event->type() == QEvent::FocusIn || event->type() == QEvent::ApplicationDeactivate ||
               (event->type() == QEvent::Hide && ownsPopupWidget(widget))) {
        queuePopupFocusCheck();
    } else if (widget && within(this, widget)) {
        switch (event->type()) {
        case QEvent::Hide:
        case QEvent::Close: dismissPopup(); break;
        case QEvent::WindowDeactivate: queuePopupFocusCheck(); break;
        case QEvent::Move:
        case QEvent::Resize: positionPopup(); break;
        default: break;
        }
    }
    return false;
}
}
