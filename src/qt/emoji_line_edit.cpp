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
#include <QVector>
#include <algorithm>

namespace m3::qt {
namespace {
constexpr int glyphRole = Qt::UserRole + 1;
constexpr char navigationHintShownProperty[] = "m3.emojiNavigationHintShown";
QString categoryLabel(const QString &group) {
    static constexpr const char *labels[] = {
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Smileys & Emotion"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "People & Body"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Animals & Nature"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Food & Drink"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Travel & Places"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Activities"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Objects"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Symbols"),
        QT_TRANSLATE_NOOP("m3::qt::EmojiLineEdit", "Flags")
    };
    // Translate only display labels; catalog order and English search stay unchanged.
    for (const auto *label : labels) {
        if (group == QLatin1String(label)) return QCoreApplication::translate("m3::qt::EmojiLineEdit", label);
    }
    return group;
}
struct Emoji {
    QString glyph, name, searchable;
    int category;
    bool featured = false;
};
struct Catalog {
    QStringList categories;
    QList<Emoji> entries;
    QList<int> allOrder;
};
const Catalog &catalog() {
    // Unicode 15.1, unmodified upstream data; notice embedded alongside the data.
    static const Catalog data = [] {
        QFile file(QStringLiteral(":/m3/emoji/emoji-test.txt"));
        if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot load embedded emoji catalog");
        Catalog result;
        QString group, line;
        QTextStream stream(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        stream.setCodec("UTF-8");
#endif
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
            QVector<char32_t> points;
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
        // Keep Unicode names/glyphs and category order; curate only All categories.
        static const struct { const char *name, *keywords; } featured[] = {
            {"check mark button", "todo task done complete"},
            {"clipboard", "todo task checklist management office stationery"},
            {"bullseye", "efficiency productivity focus goal target project"},
            {"rocket", "efficiency productivity project launch release engineer engineering aerospace"},
            {"briefcase", "project management work business office"},
            {"busts in silhouette", "management team meeting people office"},
            {"memo", "diary journal notes todo office stationery"},
            {"notebook", "diary journal daily reflection office stationery"},
            {"calendar", "management project schedule deadline diary office"},
            {"stopwatch", "efficiency productivity time focus timer"},
            {"light bulb", "efficiency productivity idea brainstorm"},
            {"bar chart", "management project report metrics review office"},
            {"star", "importance important priority favorite highlight"},
            {"glowing star", "importance important priority high special highlight"},
            {"red exclamation mark", "importance important priority high urgent attention"},
            {"double exclamation mark", "importance important priority high urgent critical"},
            {"exclamation question mark", "importance important priority urgent question attention"},
            {"red question mark", "importance important priority question clarify decision"},
            {"fire", "importance important priority high urgent hot"},
            {"police car light", "importance important priority high urgent critical emergency"},
            {"red circle", "importance important priority high urgent critical"},
            {"orange circle", "importance important priority high"},
            {"yellow circle", "importance important priority medium normal"},
            {"green circle", "importance important priority low"},
            {"blue circle", "importance important priority low"},
            {"white circle", "importance important priority optional none"},
            {"red square", "importance important priority high urgent critical"},
            {"orange square", "importance important priority high"},
            {"yellow square", "importance important priority medium normal"},
            {"green square", "importance important priority low"},
            {"blue square", "importance important priority low"},
            {"keycap: 0", "importance important priority number numbers numeric digit rank 0 zero zeroth"},
            {"keycap: 1", "importance important priority number numbers numeric digit rank 1 one first"},
            {"keycap: 2", "importance important priority number numbers numeric digit rank 2 two second"},
            {"keycap: 3", "importance important priority number numbers numeric digit rank 3 three third"},
            {"keycap: 4", "importance important priority number numbers numeric digit rank 4 four fourth"},
            {"keycap: 5", "importance important priority number numbers numeric digit rank 5 five fifth"},
            {"keycap: 6", "importance important priority number numbers numeric digit rank 6 six sixth"},
            {"keycap: 7", "importance important priority number numbers numeric digit rank 7 seven seventh"},
            {"keycap: 8", "importance important priority number numbers numeric digit rank 8 eight eighth"},
            {"keycap: 9", "importance important priority number numbers numeric digit rank 9 nine ninth"},
            {"keycap: 10", "importance important priority number numbers numeric digit rank 10 ten tenth"},
            {"TOP arrow", "importance important priority high top"},
            {"UP! button", "importance important priority escalate raise"},
            {"check box with check", "todo task checked complete"},
            {"hourglass not done", "todo task pending waiting progress"},
            {"hourglass done", "todo task overdue deadline"},
            {"play button", "todo task start progress"},
            {"pause button", "todo task paused hold"},
            {"cross mark", "todo task cancelled failed"},
            {"prohibited", "todo task blocked"},
            {"warning", "todo project risk issue attention importance important priority urgent"},
            {"triangular flag", "todo project priority importance important high milestone"},
            {"pushpin", "todo task important importance priority pinned office stationery"},
            {"bell", "todo reminder notification"},
            {"repeat button", "todo efficiency recurring routine habit"},
            {"alarm clock", "efficiency productivity reminder deadline wake"},
            {"high voltage", "efficiency productivity energy quick engineer engineering electrical"},
            {"brain", "efficiency productivity thinking brainstorm learning"},
            {"gear", "efficiency productivity workflow process automation engineer engineering mechanical automation"},
            {"magnifying glass tilted left", "efficiency project research review investigate"},
            {"trophy", "efficiency project achievement success"},
            {"hundred points", "efficiency quality achievement complete"},
            {"file folder", "project documents organize office"},
            {"open file folder", "project active documents office"},
            {"card index dividers", "project management organize categories office"},
            {"card file box", "project archive records office"},
            {"laptop", "office computer equipment workstation remote work engineer engineering software"},
            {"desktop computer", "office computer equipment workstation desk engineer engineering software"},
            {"printer", "office equipment print documents engineer engineering design"},
            {"keyboard", "office computer equipment typing input engineer engineering software"},
            {"computer mouse", "office computer equipment pointer input engineer engineering software"},
            {"trackball", "office computer equipment pointer input"},
            {"floppy disk", "office computer save backup storage"},
            {"telephone", "office call communication contact"},
            {"fax machine", "office equipment fax documents communication"},
            {"e-mail", "office email mail message communication"},
            {"envelope", "office email mail letter correspondence"},
            {"incoming envelope", "office email mail receive incoming"},
            {"envelope with arrow", "office email mail send outgoing"},
            {"inbox tray", "office email mail incoming receive documents"},
            {"outbox tray", "office email mail outgoing send documents"},
            {"page facing up", "office stationery documents report document"},
            {"page with curl", "office stationery documents document form"},
            {"scroll", "office documents contract policy agreement"},
            {"bookmark tabs", "office stationery documents reference index"},
            {"label", "office stationery organize tag label"},
            {"pen", "office stationery writing signature"},
            {"black nib", "office stationery writing signature"},
            {"ledger", "office accounting finance budget accounts"},
            {"notebook with decorative cover", "office stationery notes notebook diary journal"},
            {"card index", "office contacts directory address organize"},
            {"chart decreasing", "office management report metrics decline"},
            {"round pushpin", "office stationery location pin"},
            {"linked paperclips", "office stationery documents attachment"},
            {"straight ruler", "office stationery measure length engineer engineering measurement"},
            {"triangular ruler", "office stationery measure angle drafting engineer engineering drafting"},
            {"scissors", "office stationery cut supplies"},
            {"file cabinet", "office documents storage archive organize"},
            {"wastebasket", "office trash discard recycle cleanup"},
            {"receipt", "office accounting finance invoice expense"},
            {"credit card", "office accounting finance payment purchase"},
            {"abacus", "office accounting finance calculate"},
            {"office building", "office workplace company business headquarters"},
            {"office worker", "office workplace staff employee colleague business"},
            {"package", "project deliverable delivery release"},
            {"construction", "project todo progress work engineer engineering civil"},
            {"hammer and wrench", "project build maintenance engineer engineering tools"},
            {"wrench", "project fix maintenance engineer engineering tools mechanical"},
            {"technologist", "engineer engineering software developer programmer coding technology"},
            {"mechanic", "engineer engineering mechanical technician repair maintenance"},
            {"scientist", "engineer engineering research laboratory experiment"},
            {"construction worker", "engineer engineering civil construction safety hardhat"},
            {"factory worker", "engineer engineering industrial manufacturing production"},
            {"toolbox", "engineer engineering tools equipment maintenance"},
            {"hammer", "engineer engineering tools build construction"},
            {"hammer and pick", "engineer engineering tools mining construction"},
            {"pick", "engineer engineering tools mining excavation"},
            {"screwdriver", "engineer engineering tools assembly repair"},
            {"nut and bolt", "engineer engineering mechanical hardware fastener assembly"},
            {"clamp", "engineer engineering tools mechanical hold fixture"},
            {"carpentry saw", "engineer engineering tools carpentry cutting fabrication"},
            {"ladder", "engineer engineering tools construction maintenance"},
            {"magnet", "engineer engineering electrical physics magnetism"},
            {"electric plug", "engineer engineering electrical electronics power"},
            {"battery", "engineer engineering electrical electronics energy power"},
            {"control knobs", "engineer engineering electrical electronics controls tuning"},
            {"level slider", "engineer engineering electrical electronics controls adjustment"},
            {"satellite antenna", "engineer engineering electrical telecommunications signal radio"},
            {"satellite", "engineer engineering aerospace communications space"},
            {"microscope", "engineer engineering laboratory research inspection science"},
            {"telescope", "engineer engineering laboratory optics astronomy science"},
            {"test tube", "engineer engineering laboratory chemistry testing experiment"},
            {"petri dish", "engineer engineering laboratory biology testing experiment"},
            {"alembic", "engineer engineering laboratory chemistry distillation"},
            {"building construction", "engineer engineering civil construction crane building"},
            {"brick", "engineer engineering civil construction materials"},
            {"compass", "engineer engineering civil surveying navigation"},
            {"chains", "engineer engineering mechanical hardware linkage"},
            {"motorway", "engineer engineering civil infrastructure road transport"},
            {"link", "project dependency connection"},
            {"paperclip", "project attachment reference office stationery"},
            {"handshake", "management agreement collaboration delegation office"},
            {"speech balloon", "management discussion meeting feedback office"},
            {"telephone receiver", "management call meeting contact office"},
            {"megaphone", "management announcement communication office"},
            {"spiral calendar", "management project planning roadmap schedule office"},
            {"chart increasing", "management project progress growth office"},
            {"money bag", "management project budget finance office"},
            {"money with wings", "management project expense cost office"},
            {"balance scale", "management decision balance policy"},
            {"ballot box with ballot", "management vote decision"},
            {"locked", "management project confidential private"},
            {"key", "management project access responsibility"},
            {"spiral notepad", "diary journal notes meeting office stationery"},
            {"tear-off calendar", "diary journal daily date office"},
            {"open book", "diary journal reading study"},
            {"books", "diary learning study reference"},
            {"pencil", "diary journal writing draft office stationery"},
            {"fountain pen", "diary journal writing office stationery"},
            {"writing hand", "diary journal writing reflection office"},
            {"bookmark", "diary journal remember favorite office stationery"},
            {"camera", "diary journal photo memory"},
            {"sun behind small cloud", "diary journal weather mood"},
            {"smiling face with smiling eyes", "diary journal mood happy"},
            {"pensive face", "diary journal mood sad reflection"},
            {"red heart", "diary journal gratitude love"},
            {"seedling", "diary journal habit growth"},
            {"flexed biceps", "diary journal habit exercise health"},
            {"person in lotus position", "diary journal habit meditation mindfulness"},
            {"sleeping face", "diary journal sleep rest"},
            {"hot beverage", "diary journal break coffee"},
            {"party popper", "diary journal celebration project success"},
        };
        result.allOrder.reserve(result.entries.size());
        for (const auto &item : featured) {
            const auto entry = std::find_if(result.entries.begin(), result.entries.end(), [&item](const Emoji &emoji) {
                return emoji.name == QLatin1String(item.name);
            });
            if (entry == result.entries.end() || entry->featured) qFatal("Invalid featured emoji catalog");
            entry->searchable += QLatin1Char(' ') + QString::fromLatin1(item.keywords).toCaseFolded();
            entry->featured = true;
            result.allOrder.append(int(entry - result.entries.begin()));
        }
        for (qsizetype i = 0; i < result.entries.size(); ++i) {
            if (!result.entries[i].featured) result.allOrder.append(int(i));
        }
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
        const auto &data = catalog();
        for (qsizetype i = 0; i < data.entries.size(); ++i) {
            const int row = category < 0 ? data.allOrder[i] : int(i);
            const auto &entry = data.entries[row];
            if (category >= 0 && entry.category != category) continue;
            if (std::all_of(terms.begin(), terms.end(), [&entry](const QString &term) {
                return entry.searchable.contains(term) || entry.glyph.contains(term);
            })) rows.append(row);
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
        QStyleOptionViewItem item(option);
        initStyleOption(&item, index);
        const bool selected = item.state.testFlag(QStyle::State_Selected);
        const QRect bounds = option.rect.adjusted(2, 2, -2, -2);
        item.rect = bounds;
        // The picker is the active navigation target while focus stays in the input.
        item.state |= QStyle::State_Active;
        item.displayAlignment = Qt::AlignHCenter | Qt::AlignBottom;
        item.textElideMode = Qt::ElideRight;
        view.style()->drawControl(QStyle::CE_ItemViewItem, &item, painter, &view);
        if (selected) {
            painter->setPen(item.palette.color(QPalette::Text));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(bounds.adjusted(0, 0, -1, -1));
        }
        painter->setPen(item.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
        QFont glyphFont = item.font;
        glyphFont.setPixelSize(28);
        painter->setFont(glyphFont);
        painter->drawText(QRect(bounds.left(), bounds.top(), bounds.width(), 36), Qt::AlignCenter,
                          index.data(glyphRole).toString());
        painter->restore();
    }
private:
    const QListView &view;
};
bool within(QWidget *child, QWidget *parent) {
    return child && (child == parent || parent->isAncestorOf(child));
}
}
EmojiLineEdit::EmojiLineEdit(QWidget *parent, PickerMode mode) : QLineEdit(parent), mode(mode) {
    connect(this, &QLineEdit::textChanged, this, [this] {
        if (!inserting && hasFocus()) showPopup();
    });
    connect(this, &QLineEdit::cursorPositionChanged, this, [this] {
        if (!inserting && popup && popup->isVisible()) showPopup();
    });
    if (mode == PickerMode::Embedded) {
        createPopup();
        filterMatches();
    }
}
QWidget *EmojiLineEdit::pickerWidget() const {
    return mode == PickerMode::Embedded ? popup : nullptr;
}
EmojiLineEdit::~EmojiLineEdit() { dismissPopup(); }
void EmojiLineEdit::createPopup() {
    popup = mode == PickerMode::Embedded ? new QFrame(this)
        : new QFrame(this, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
    popup->setObjectName(mode == PickerMode::Embedded ? QStringLiteral("embeddedEmojiPicker") : QStringLiteral("emojiPopup"));
    popup->setAccessibleName(tr("Choose an emoji"));
    if (mode == PickerMode::Floating) popup->setAttribute(Qt::WA_ShowWithoutActivating);
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
    for (const auto &group : catalog().categories) categories->addItem(categoryLabel(group));
    if (mode == PickerMode::Embedded) {
        // Observe only the owned dropdown, never install the floating app filter.
        categories->view()->installEventFilter(this);
        categories->view()->window()->installEventFilter(this);
    }
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
    connect(categories, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { filterMatches(); });
    connect(categories, QOverload<int>::of(&QComboBox::activated), this, [this] { queuePopupFocusCheck(); });
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
    if (mode == PickerMode::Embedded) return;
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
    if (mode == PickerMode::Embedded) {
        QTimer::singleShot(0, this, [this] {
            if (isVisible() && isEnabled() && !categories->view()->isVisible()) setFocus(Qt::OtherFocusReason);
        });
        return;
    }
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
    if (mode == PickerMode::Embedded) {
        if (categories) categories->hidePopup();
        return;
    }
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
    if (mode == PickerMode::Floating) dismissPopup();
    inserting = true;
    QPointer<EmojiLineEdit> guard(this);
    setSelection(range.first, range.second - range.first);
    if (!guard) return;
    // One native edit: preserves undo and the host's synchronous textChanged contract.
    insert(replacement);
    if (!guard) return;
    inserting = false;
    if (mode == PickerMode::Embedded && isVisible() && isEnabled()) {
        setFocus(Qt::OtherFocusReason);
        if (guard) showPopup();
    }
}
void EmojiLineEdit::focusInEvent(QFocusEvent *event) {
    QLineEdit::focusInEvent(event);
    if (mode == PickerMode::Embedded) showPopup();
    else if (!popup || !popup->isVisible()) showPopup(true);
}
void EmojiLineEdit::focusOutEvent(QFocusEvent *event) {
    QLineEdit::focusOutEvent(event);
    if (mode == PickerMode::Floating && popup && popup->isVisible()) queuePopupFocusCheck();
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
        if (key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) {
            if (mode == PickerMode::Floating) dismissPopup();
        }
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
        if (key == Qt::Key_Escape) {
            if (categories->view()->isVisible()) {
                categories->hidePopup();
                queuePopupFocusCheck();
                event->accept();
            } else if (mode == PickerMode::Floating) {
                dismissPopup();
                event->accept();
            } else event->ignore();
            return;
        }
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
    if (widget && categories->view()->isVisible() &&
        within(widget, categories->view()->window()) &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        // Qt5 hides the combo during ShortcutOverride, leaking KeyPress to the card.
        if (event->type() == QEvent::KeyPress) {
            categories->hidePopup();
            queuePopupFocusCheck();
        }
        event->accept();
        return true;
    }
    if (mode == PickerMode::Embedded) {
        if (event->type() == QEvent::Hide && widget == categories->view()->window()) queuePopupFocusCheck();
        return false;
    }
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
