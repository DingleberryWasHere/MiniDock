#include "settings_dialog.h"
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include <QListView>
#include <QStyledItemDelegate>
#include <QPainter>
#include <QPainterPath>
#include <QApplication>
#include <QMouseEvent>
#include <QWindow>
#include <QScreen>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QBoxLayout>
#include <QFileDialog>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QTimer>
#include <QStyle>
#include <QScrollArea>
#include <QStandardPaths>
#include <QFrame>
#include <memory>

namespace U {
const QString ink = "#e0efff", muted = "#8ca6bd", paper = "#071521", panel = "#091d2c",
              raised = "#102b40", edge = "#244b68", accent = "#79bffa", error = "#ffaea6",
              success = "#99dbbc";
const QColor paperColor(7, 21, 33), edgeColor(36, 75, 104);
constexpr qreal windowRadius = 12;
const QColor inkColor(224, 239, 255), mutedColor(140, 166, 189), hover(20, 49, 72),
    focus(121, 191, 250), rule(30, 65, 91);
}
QString miniDockDialogStyle() {
    return QString(R"(
 QDialog#settings {background:transparent;color:%2;font-family:'Segoe UI';font-size:12px;border:0;}
 QLabel {color:%2;background:transparent;font-size:12px;}
 QLabel[muted="true"] {color:%3;}
 QLabel#title {font-size:17px;font-weight:600;}
 QLabel#section {font-size:12px;font-weight:600;}
 QLabel#status {color:%3;font-size:11px;}
 QLabel#status[error="true"] {color:%7;}
 QLineEdit {background:%4;color:%2;border:1px solid %5;border-radius:6px;padding:9px 10px;selection-background-color:%5;}
 QLineEdit:focus {border:1px solid %6;}
 QListView {background:transparent;color:%2;border:0;outline:0;}
 QScrollBar:vertical {background:transparent;width:6px;margin:2px;}
 QScrollBar::handle:vertical {background:%5;min-height:28px;border-radius:2px;}
 QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
 QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical {background:transparent;}
 QToolButton {background:transparent;color:%2;border:1px solid transparent;border-radius:6px;padding:7px;text-align:left;}
 QToolButton:hover {background:%4;}
 QToolButton:focus {border:1px solid %6;}
 QToolButton:pressed {background:%5;}
 QToolButton:disabled {color:%3;}
 QToolButton[selected="true"] {background:%4;}
 QPushButton {color:%2;background:%4;border:1px solid %5;border-radius:6px;padding:7px 16px;}
 QPushButton:hover {border:1px solid %6;}
 QPushButton:focus {border:2px solid %6;padding:6px 15px;}
 QPushButton:pressed {background:%5;}
 QPushButton:disabled {color:%3;}
 QFrame#divider {background:%5;max-height:1px;}
 QFrame#drop {background:%4;border:1px dashed %5;border-radius:8px;}
 QFrame#drop[dragging="true"] {background:%5;border:1px dashed %6;}
 QMenu {background:%1;color:%2;border:1px solid %5;padding:5px;font-family:'Segoe UI';font-size:12px;}
 QMenu::item {padding:8px 28px 8px 12px;border-radius:4px;}
 QMenu::item:selected {background:%4;}
 QMenu::separator {height:1px;background:%5;margin:4px 7px;}
 )")
        .arg(U::paper, U::ink, U::muted, U::raised, U::edge, U::accent, U::error);
}
class InventoryThread final : public QThread {
    QString category;

public:
    QVector<LaunchEntry> results;
    explicit InventoryThread(QString source) : category(std::move(source)) {}
    void run() override {
        results = LauncherStore::discover(category);
    }
};
class EntryListModel final : public QAbstractListModel {
    LauncherStore& store;
    QVector<LaunchEntry> rows;

public:
    EntryListModel(LauncherStore& s, QObject* parent) : QAbstractListModel(parent), store(s) {}
    int rowCount(const QModelIndex& p = {}) const override {
        return p.isValid() ? 0 : int(rows.size());
    }
    QVariant data(const QModelIndex& i, int role) const override {
        if (!i.isValid() || i.row() >= rows.size())
            return {};
        const auto& e = rows[i.row()];
        if (role == Qt::DisplayRole)
            return e.name;
        if (role == Qt::DecorationRole)
            return const_cast<LauncherStore&>(store).icon(e);
        if (role == Qt::ToolTipRole)
            return e.target.isEmpty() ? e.name : e.target;
        if (role == Qt::UserRole)
            return e.id;
        if (role == Qt::UserRole + 1)
            return store.contains(e.id);
        if (role == Qt::AccessibleTextRole)
            return e.name +
                   (store.contains(e.id) ? ", already in dock" : ", press Enter to add to dock");
        return {};
    }
    const LaunchEntry& entry(int row) const {
        return rows[row];
    }
    void setRows(QVector<LaunchEntry> list) {
        beginResetModel();
        rows = std::move(list);
        endResetModel();
    }
    void refresh() {
        if (!rows.isEmpty())
            emit dataChanged(index(0), index(int(rows.size()) - 1));
    }
};
class AppDelegate final : public QStyledItemDelegate {
public:
    std::function<void(const QModelIndex&)> add;
    explicit AppDelegate(QObject* p) : QStyledItemDelegate(p) {}
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return {180, 43};
    }
    void paint(QPainter* p, const QStyleOptionViewItem& o, const QModelIndex& i) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        QRectF r = o.rect.adjusted(0, 2, -2, -2);
        if (o.state & (QStyle::State_MouseOver | QStyle::State_Selected)) {
            p->setPen(Qt::NoPen);
            p->setBrush(U::hover);
            p->drawRoundedRect(r, 6, 6);
        }
        if (o.state & QStyle::State_HasFocus) {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(U::focus, 1));
            p->drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), 6, 6);
        }
        const auto icon = qvariant_cast<QIcon>(i.data(Qt::DecorationRole));
        icon.paint(p, QRect(o.rect.left() + 7, o.rect.center().y() - 13, 26, 26));
        const QRect text(o.rect.left() + 42, o.rect.top(), o.rect.width() - 84, o.rect.height());
        p->setPen(U::inkColor);
        p->setFont(o.font);
        p->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                    o.fontMetrics.elidedText(i.data().toString(), Qt::ElideRight, text.width()));
        const QPointF centre(o.rect.right() - 18, o.rect.center().y());
        const bool pinned = i.data(Qt::UserRole + 1).toBool();
        p->setBrush(QColor(20, 52, 76));
        p->setPen(Qt::NoPen);
        p->drawEllipse(centre, 10, 10);
        p->setPen(
            QPen(pinned ? QColor(153, 219, 188) : U::focus, 1.6, Qt::SolidLine, Qt::RoundCap));
        if (pinned) {
            p->drawLine(centre + QPointF(-4, 0), centre + QPointF(-1, 3));
            p->drawLine(centre + QPointF(-1, 3), centre + QPointF(4, -3));
        } else {
            p->drawLine(centre + QPointF(-4, 0), centre + QPointF(4, 0));
            p->drawLine(centre + QPointF(0, -4), centre + QPointF(0, 4));
        }
        p->restore();
    }
    bool editorEvent(QEvent* event, QAbstractItemModel*, const QStyleOptionViewItem& o,
                     const QModelIndex& index) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* e = static_cast<QMouseEvent*>(event);
            if (e->button() == Qt::LeftButton && e->position().x() >= o.rect.right() - 36) {
                if (add)
                    add(index);
                return true;
            }
        }
        return false;
    }
};
class Header final : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && window()->windowHandle())
            window()->windowHandle()->startSystemMove();
    }
};
class CloseButton final : public QToolButton {
public:
    explicit CloseButton(QWidget* parent) : QToolButton(parent) {
        setObjectName("closeSettings");
        setFixedSize(32, 32);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName("Close settings");
        setToolTip("Close settings");
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QToolButton::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(isEnabled() ? U::inkColor : U::mutedColor, 1.7, Qt::SolidLine, Qt::RoundCap));
        const QPointF centre(width() / 2.0, height() / 2.0);
        p.drawLine(centre + QPointF(-5.5, -5.5), centre + QPointF(5.5, 5.5));
        p.drawLine(centre + QPointF(-5.5, 5.5), centre + QPointF(5.5, -5.5));
    }
};
class DropZone final : public QFrame {
    void highlight(bool active) {
        setProperty("dragging", active);
        style()->unpolish(this);
        style()->polish(this);
    }

public:
    std::function<void()> browse;
    std::function<void(QStringList)> files;
    explicit DropZone(QWidget* parent) : QFrame(parent) {
        setObjectName("drop");
        setAcceptDrops(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName("Drop applications here, or press Enter to browse");
        setMinimumHeight(175);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* e) override {
        for (const auto& url : e->mimeData()->urls())
            if (url.isLocalFile() && LauncherStore::supportedFile(url.toLocalFile())) {
                highlight(true);
                e->acceptProposedAction();
                return;
            }
        e->ignore();
    }
    void dragLeaveEvent(QDragLeaveEvent*) override {
        highlight(false);
    }
    void dropEvent(QDropEvent* e) override {
        highlight(false);
        QStringList paths;
        for (const auto& url : e->mimeData()->urls())
            if (url.isLocalFile())
                paths << url.toLocalFile();
        if (files)
            files(paths);
        e->acceptProposedAction();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && browse)
            browse();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
            if (browse)
                browse();
        } else
            QFrame::keyPressEvent(e);
    }
    void paintEvent(QPaintEvent* e) override {
        QFrame::paintEvent(e);
        if (hasFocus()) {
            QPainter p(this);
            p.setPen(QPen(U::focus, 2));
            p.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 7, 7);
        }
    }
};
SettingsDialog::SettingsDialog(LauncherStore& s, QWidget* parent) : QDialog(parent), store(s) {
    setObjectName("settings");
    setWindowTitle("Add Apps - OPIA's Minidock");
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::MSWindowsFixedSizeDialogHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);
    setStyleSheet(miniDockDialogStyle());
    setFixedSize(580, 648);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setSizeGripEnabled(false);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 10, 16, 14);
    root->setSpacing(14);
    auto* header = new Header(this);
    auto* h = new QHBoxLayout(header);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* title = new QLabel("Add Apps", header);
    title->setObjectName("title");
    title->setAttribute(Qt::WA_TransparentForMouseEvents);
    h->addWidget(title);
    h->addStretch();
    auto* close = new CloseButton(header);
    connect(close, &QToolButton::clicked, this, &QDialog::hide);
    h->addWidget(close);
    root->addWidget(header);
    auto* line = new QFrame(this);
    line->setObjectName("divider");
    line->setFixedHeight(1);
    root->addWidget(line);
    auto* body = new QHBoxLayout;
    body->setSpacing(18);
    root->addLayout(body, 1);
    auto* left = new QVBoxLayout;
    left->setSpacing(9);
    body->addLayout(left, 6);
    search = new QLineEdit(this);
    search->setObjectName("searchApps");
    search->setPlaceholderText("Search for an app...");
    search->setClearButtonEnabled(true);
    search->setAccessibleName("Search available apps");
    left->addWidget(search);
    model = new EntryListModel(store, this);
    proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setFilterKeyColumn(0);
    list = new QListView(this);
    list->setObjectName("availableApps");
    list->setModel(proxy);
    list->setMouseTracking(true);
    list->setUniformItemSizes(true);
    list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list->setAccessibleName("Available apps. Press Enter to add the selected app.");
    auto* delegate = new AppDelegate(list);
    delegate->add = [this](const QModelIndex& i) {
        addEntry(model->entry(proxy->mapToSource(i).row()));
    };
    list->setItemDelegate(delegate);
    left->addWidget(list, 1);
    empty = new QLabel("Looking for your apps...", this);
    empty->setProperty("muted", true);
    empty->setWordWrap(true);
    empty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    left->addWidget(empty);
    connect(search, &QLineEdit::textChanged, this, [this](const QString& text) {
        proxy->setFilterFixedString(text);
        updateEmpty();
    });
    connect(list, &QListView::activated, this, [this](const QModelIndex& i) {
        if (i.isValid())
            addEntry(model->entry(proxy->mapToSource(i).row()));
    });
    auto* right = new QVBoxLayout;
    right->setSpacing(10);
    body->addLayout(right, 5);
    auto* dropLabel = new QLabel("Drag & Drop", this);
    dropLabel->setObjectName("section");
    right->addWidget(dropLabel);
    auto* drop = new DropZone(this);
    drop->setObjectName("drop");
    drop->browse = [this] { browse(); };
    drop->files = [this](const QStringList& p) { addFiles(p); };
    auto* dropLayout = new QVBoxLayout(drop);
    dropLayout->setContentsMargins(12, 22, 12, 22);
    dropLayout->addStretch();
    auto* dropIcon = new QLabel(drop);
    dropIcon->setPixmap(QIcon(":/icons/drop-app.png").pixmap(32, 32));
    dropIcon->setAlignment(Qt::AlignCenter);
    dropLayout->addWidget(dropIcon);
    auto* dropTitle = new QLabel("Drop apps here", drop);
    dropTitle->setAlignment(Qt::AlignCenter);
    dropLayout->addWidget(dropTitle);
    auto* dropHelp = new QLabel("or click to browse your files\n.exe, .lnk and .url", drop);
    dropHelp->setWordWrap(true);
    dropHelp->setAlignment(Qt::AlignCenter);
    dropHelp->setProperty("muted", true);
    dropLayout->addWidget(dropHelp);
    dropLayout->addStretch();
    for (auto* label : drop->findChildren<QLabel*>())
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
    right->addWidget(drop);
    auto* quick = new QLabel("Quick Add", this);
    quick->setObjectName("section");
    right->addWidget(quick);
    const QVector<QPair<QString, QString>> categories = {
        {"Desktop shortcuts", ":/icons/quick-desktop.png"},
        {"Recent apps", ":/icons/quick-recent.png"},
        {"Pinned apps", ":/icons/quick-pinned.png"},
        {"Installed apps", ":/icons/quick-installed.png"}};
    for (const auto& item : categories) {
        auto* button = new QToolButton(this);
        button->setText(item.first);
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setIcon(QIcon(item.second));
        button->setIconSize(QSize(18, 18));
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setAccessibleName(item.first);
        button->setFixedHeight(36);
        if (item.first == "Recent apps")
            button->setToolTip("Apps recently opened from MiniDock");
        if (item.first == "Pinned apps")
            button->setToolTip("Taskbar shortcut files exposed by Windows");
        connect(button, &QToolButton::clicked, this, [this, name = item.first] { scan(name); });
        right->addWidget(button);
        quickButtons.push_back(button);
    }
    right->addStretch();
    auto* hint = new QLabel("To remove an app, right-click its dock icon.", this);
    hint->setWordWrap(true);
    hint->setProperty("muted", true);
    right->addWidget(hint);
    auto* footer = new QHBoxLayout;
    status = new QLabel(this);
    status->setObjectName("status");
    status->setWordWrap(true);
    footer->addWidget(status, 1);
    auto* done = new QPushButton("Done", this);
    done->setObjectName("doneButton");
    connect(done, &QPushButton::clicked, this, &QDialog::hide);
    footer->addWidget(done);
    root->addLayout(footer);
    syncPinned();
    if (!store.loadNotice.isEmpty())
        message(store.loadNotice, true);
    QTimer::singleShot(0, this, [this] { scan("Installed apps"); });
}
void SettingsDialog::paintEvent(QPaintEvent*) {

    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(U::paperColor);
    p.setPen(QPen(U::edgeColor, 1));
    p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), U::windowRadius, U::windowRadius);
}
SettingsDialog::~SettingsDialog() {
    if (scanner) {
        scanner->wait();
        delete scanner;
        scanner = nullptr;
    }
}
void SettingsDialog::message(QString text, bool error) {
    const int serial = ++feedbackSerial;
    status->setText(text);
    status->setProperty("error", error);
    status->style()->unpolish(status);
    status->style()->polish(status);
    if (!error && !text.isEmpty())
        QTimer::singleShot(2400, this, [this, serial] {
            if (feedbackSerial == serial)
                status->clear();
        });
}
void SettingsDialog::syncPinned() {
    model->refresh();
    message({});
}
void SettingsDialog::updateEmpty() {
    const bool none = proxy->rowCount() == 0;
    empty->setVisible(none);
    list->setVisible(!none);
    if (scanner)
        empty->setText("Looking for your apps...");
    else if (!search->text().isEmpty())
        empty->setText("No matching apps. Try another name or browse for a file.");
    else if (category == "Recent apps")
        empty->setText("Apps you open from MiniDock will appear here.");
    else if (category == "Pinned apps")
        empty->setText(
            "No taskbar shortcut files were found. Windows may not expose all pins; use Installed apps or Browse instead.");
    else
        empty->setText("No shortcuts found here. Drop an app or click the box to browse.");
}
void SettingsDialog::scan(QString source) {
    if (scanner)
        return;
    category = source;
    search->clear();
    for (auto* w : quickButtons) {
        w->setProperty("selected", qobject_cast<QToolButton*>(w)->text() == source);
        w->style()->unpolish(w);
        w->style()->polish(w);
    }
    if (source == "Recent apps") {
        model->setRows(store.recent());
        updateEmpty();
        return;
    }
    for (auto* w : quickButtons)
        w->setEnabled(false);
    auto* worker = new InventoryThread(source);
    scanner = worker;
    model->setRows({});
    updateEmpty();
    QThread* thread = scanner;
    connect(thread, &QThread::finished, this, [this, thread, worker] {
        if (scanner != thread)
            return;
        scanner = nullptr;
        model->setRows(worker->results);
        for (auto* w : quickButtons)
            w->setEnabled(true);
        updateEmpty();
        thread->deleteLater();
    });
    thread->start();
}
void SettingsDialog::addEntry(const LaunchEntry& entry) {
    if (store.contains(entry.id)) {
        message(entry.name + " is already in your dock.");
        return;
    }
    QString error;
    if (!store.add(entry, &error)) {
        message(error, true);
        return;
    }
    model->refresh();
    message(entry.name + " added to your dock.");
}
void SettingsDialog::addFiles(const QStringList& paths) {
    int added = 0;
    QStringList errors;
    for (const auto& path : paths) {
        if (!LauncherStore::supportedFile(path)) {
            errors << "Only existing .exe, .lnk and .url files can be added.";
            continue;
        }
        const auto e = LauncherStore::fileEntry(path);
        if (store.contains(e.id))
            continue;
        QString error;
        if (store.add(e, &error))
            ++added;
        else
            errors << error;
    }
    model->refresh();
    if (!errors.isEmpty())
        message(QString("%1 added. ").arg(added) + errors.first(), true);
    else if (added)
        message(QString("%1 %2 added to your dock.").arg(added).arg(added == 1 ? "app" : "apps"));
    else
        message("Those apps are already in your dock.");
}
void SettingsDialog::browse() {
    const auto files = QFileDialog::getOpenFileNames(
        this, "Add apps to MiniDock",
        QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
        "Apps and shortcuts (*.exe *.lnk *.url)", nullptr, QFileDialog::DontResolveSymlinks);
    if (!files.isEmpty())
        addFiles(files);
}
void SettingsDialog::open() {
    if (auto* screen = QGuiApplication::screenAt(QCursor::pos())) {
        const auto area = screen->availableGeometry();
        const QSize fitted(qMin(580, area.width() - 24), qMin(648, area.height() - 24));

        setFixedSize(fitted.expandedTo(QSize(460, 500)));
        move(area.center() - QPoint(width() / 2, height() / 2));
    }
    show();
    raise();
    activateWindow();
    search->setFocus(Qt::OtherFocusReason);
}
