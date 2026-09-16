#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QVariantAnimation>
#include <QTimer>
#include <QScreen>
#include <QStyleHints>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QWindow>
#include <QLabel>
#include <QIcon>
#include <QImage>
#include <QDir>
#include <QPointer>
#include <QMoveEvent>
#include <QShowEvent>
#include <QFontMetricsF>
#include <QtMath>
#include "dock_placement.h"
#include "launcher.h"
#include "settings_dialog.h"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMessageBox>
#include <QCloseEvent>
#include <QStyleFactory>
#include <QAction>
#include <functional>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#include <ole2.h>
static UINT activationMessage = RegisterWindowMessageW(L"OPIA.MiniDock.v3.ShowSettings");
static HWND findExistingDock() {
    HWND result = nullptr;
    EnumWindows(
        [](HWND window, LPARAM data) -> BOOL {
            if (GetPropW(window, L"OPIA.MiniDock.v3.Window")) {
                *reinterpret_cast<HWND*>(data) = window;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&result));
    return result;
}

#endif
#include <array>
#include <algorithm>
#include <limits>

namespace T {

const QColor ink(242, 245, 254), muted(185, 196, 218), top(33, 43, 71, 230), mid(9, 18, 41, 247),
    bottom(15, 28, 57, 242);
const QColor edge(104, 166, 229, 150), lowerEdge(50, 106, 245, 170), reflection(200, 220, 251, 90);
const QColor glow(43, 117, 255, 12), shadow(1, 4, 17, 14), hover(185, 215, 252, 22),
    focus(159, 229, 251);
const QColor label(15, 23, 41, 248), error(255, 163, 150), success(139, 235, 190),
    transparent(0, 0, 0, 0);
constexpr int dockWidth = 456, dockHeight = 64, padding = 32, cell = 44, gap = 10;
const QString font = QStringLiteral("Segoe UI");
}
enum class Visual { Normal, Hover, Focus, Active, Disabled, Loading, Error, Success };
const std::array<QString, 8> stateNames = {"Default",  "Hover",   "Focus", "Active",
                                           "Disabled", "Loading", "Error", "Success"};

class AppButton final : public QToolButton {
    QPixmap art;
    bool keyboardFocus = false;
    qreal uiScale = 1;

public:
    Visual demo = Visual::Normal;
    explicit AppButton(const LaunchEntry& entry, const QIcon& icon, QWidget* parent)
        : QToolButton(parent), art(icon.pixmap(128, 128)) {
        setAccessibleName(entry.name);
        setAccessibleDescription("Open " + entry.name + ". Right-click to manage this dock item.");
        setProperty("entryId", entry.id);
        setCheckable(false);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(T::cell, 46);
    }
    void showKeyboardFocus() {
        keyboardFocus = true;
        update();
    }
    void refreshIcon(const QIcon& icon) {
        art = icon.pixmap(128, 128);
        update();
    }
    void setUiScale(qreal scale) {
        uiScale = scale;
        setFixedSize(qCeil(T::cell * scale), qCeil(46 * scale));
        update();
    }
    void setDemo(Visual state) {
        demo = state;
        setEnabled(state != Visual::Disabled);
        update();
    }

protected:
    void enterEvent(QEnterEvent* e) override {
        QToolButton::enterEvent(e);
        update();
    }
    void leaveEvent(QEvent* e) override {
        QToolButton::leaveEvent(e);
        update();
    }
    void focusInEvent(QFocusEvent* e) override {
        const auto reason = e->reason();
        keyboardFocus = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
                        reason == Qt::ShortcutFocusReason;
        QToolButton::focusInEvent(e);
        update();
    }
    void focusOutEvent(QFocusEvent* e) override {
        keyboardFocus = false;
        QToolButton::focusOutEvent(e);
        update();
    }
    void mousePressEvent(QMouseEvent* e) override {
        keyboardFocus = false;
        update();
        if (demo == Visual::Loading)
            return;
        QToolButton::mousePressEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Space)
            showKeyboardFocus();
        if (demo == Visual::Loading && (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return))
            return;
        if (e->key() == Qt::Key_Return) {
            click();
            e->accept();
            return;
        }
        QToolButton::keyPressEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        p.scale(uiScale, uiScale);
        constexpr qreal w = T::cell, h = 46;
        bool hoverState = underMouse() || demo == Visual::Hover;
        bool focusState = (hasFocus() && keyboardFocus) || demo == Visual::Focus;
        bool down = isDown() || demo == Visual::Active;

        if (isEnabled() && (hoverState || focusState || down)) {
            p.setPen(QPen(T::focus, 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(1.5, 1.5, w - 3, h - 3), 9, 9);
        }
        constexpr qreal size = 34;
        p.setOpacity(isEnabled() ? 1 : .28);
        p.drawPixmap(QRectF((w - size) / 2, (h - size) / 2, size, size), art, art.rect());
        p.setOpacity(1);
        if (demo == Visual::Loading) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(T::focus, 2));
            p.drawArc(QRectF(3, 3, w - 6, h - 6), 30 * 16, 270 * 16);
        }
        if (demo == Visual::Error || demo == Visual::Success) {
            p.setPen(Qt::NoPen);
            p.setBrush(T::label);
            p.drawEllipse(QRectF(w - 17, h - 19, 16, 16));
            p.setPen(demo == Visual::Error ? T::error : T::success);
            p.setFont(QFont(T::font, 9, QFont::Bold));
            p.drawText(QRectF(w - 17, h - 19, 16, 16), Qt::AlignCenter,
                       demo == Visual::Error ? QStringLiteral("!") : QString::fromUtf8("✓"));
        }
    }
};

class Dock final : public QWidget {
    QVector<AppButton*> buttons;
    LauncherStore& store;
    bool initialized = false;
    QTimer tooltipDelay;
    int hoverIndex = -1, shownTip = -1;
    QPointF dragAnchor;
    qreal uiScale = 1;
    bool constrainQueued = false;
    QPointer<QScreen> constrainedScreen;
    bool dragging = false;

    bool embedded = false;

public:
    std::function<void()> requestSettings;
    explicit Dock(LauncherStore& launcher, QWidget* parent = nullptr)
        : QWidget(parent), store(launcher), embedded(parent != nullptr) {
        if (!embedded)
            setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowTitle("OPIA's Minidock");
        setFixedSize(DockPlacement::windowWidth, DockPlacement::windowHeight);
        setAccessibleName("OPIA's Minidock");
        tooltipDelay.setSingleShot(true);
        tooltipDelay.setInterval(800);
        connect(&tooltipDelay, &QTimer::timeout, this, [this] {
            shownTip = hoverIndex;
            update();
        });
        rebuild();
        auto* binRefresh = new QTimer(this);
        binRefresh->setInterval(8000);
        connect(binRefresh, &QTimer::timeout, this, [this] {
            store.refreshRecycleIcon();
            for (int i = 0; i < buttons.size(); ++i)
                if (store.entries()[i].kind == "recycle")
                    buttons[i]->refreshIcon(store.icon(store.entries()[i]));
        });
        binRefresh->start();
        if (!embedded) {
            for (auto* screen : QGuiApplication::screens())
                watchScreen(screen);
            connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
                watchScreen(screen);
                queueConstrain();
            });
            connect(qApp, &QGuiApplication::screenRemoved, this,
                    [this](QScreen*) { queueConstrain(); });
        }
    }

    QRectF panelGeometry() const {
        return DockPlacement::panel(uiScale, barWidth());
    }
    int barWidth() const {
        return buttons.isEmpty()
                   ? 190
                   : qMax(132, 34 + int(buttons.size()) * (T::cell + T::gap) - T::gap);
    }
    QRectF tooltipGeometry(int index) const {
        return DockPlacement::scaled(labelInDesignUnits(index), uiScale);
    }
    void moveInsideWorkArea(QPoint wanted, QScreen* target = nullptr) {
        if (embedded) {
            move(wanted);
            return;
        }
        if (target && !QGuiApplication::screens().contains(target))
            target = nullptr;
        if (!target)
            target = nearestScreen(wanted + panelGeometry().center().toPoint());
        if (!target)
            return;
        constrainedScreen = target;
        applyScale(DockPlacement::scaleForArea(target->availableGeometry(), barWidth()));
        move(DockPlacement::clampOrigin(wanted, target->availableGeometry(), uiScale, barWidth()));
        update();
    }
    void rebuild() {
        const QPoint centre = mapToGlobal(panelGeometry().center().toPoint());
        tooltipDelay.stop();
        hoverIndex = shownTip = -1;
        for (auto* button : buttons) {
            button->hide();
            button->deleteLater();
        }
        buttons.clear();
        for (const auto& entry : store.entries()) {
            auto* button = new AppButton(entry, store.icon(entry), this);
            buttons.push_back(button);
            button->installEventFilter(this);
            connect(button, &QToolButton::clicked, this, [this, id = entry.id] { openEntry(id); });
            if (buttons.size() > 1)
                setTabOrder(buttons[buttons.size() - 2], button);
            button->show();
        }
        applyScale(uiScale);
        if (initialized && !embedded)
            moveInsideWorkArea(centre - panelGeometry().center().toPoint(), constrainedScreen);
        initialized = true;
        update();
    }
    QMenu* makeMenu(const QString& id = {}, QWidget* parent = nullptr) {
        auto* menu = new QMenu(parent);
        menu->setStyleSheet(miniDockDialogStyle());
        if (!id.isEmpty()) {
            auto* open = menu->addAction("Open");
            open->setObjectName("openAppAction");
            connect(open, &QAction::triggered, this, [this, id] { openEntry(id); });
            auto* remove = menu->addAction("Remove from dock");
            remove->setObjectName("removeAppAction");
            connect(remove, &QAction::triggered, this, [this, id] {
                QString error;
                if (!store.remove(id, &error))
                    QMessageBox::warning(this, "Could not save dock", error);
            });
            menu->addSeparator();
        }
        auto* settings = menu->addAction("Settings");
        settings->setObjectName("settingsAction");
        connect(settings, &QAction::triggered, this, [this] {
            if (requestSettings)
                requestSettings();
        });
        menu->addSeparator();
        auto* quit = menu->addAction("Exit MiniDock");
        quit->setObjectName("exitAction");
        connect(quit, &QAction::triggered, qApp, &QCoreApplication::quit);
        return menu;
    }
    void openEntry(const QString& id) {
        QPointer<AppButton> button;
        for (auto* b : buttons)
            if (b->property("entryId").toString() == id)
                button = b;
        if (!button || button->demo == Visual::Loading)
            return;
        button->setDemo(Visual::Loading);
        QTimer::singleShot(0, this, [this, id, button] {
            QString error;
            bool ok = store.launchId(id, &error);
            if (button) {
                button->setDemo(ok ? Visual::Normal : Visual::Error);
            }
            if (!ok) {
                QMessageBox::warning(this, "Could not open app", error);
                if (button)
                    button->setDemo(Visual::Normal);
            }
        });
    }

private:
    QScreen* nearestScreen(QPoint point) const {
        if (auto* screen = QGuiApplication::screenAt(point))
            return screen;
        QScreen* best = QGuiApplication::primaryScreen();
        qint64 distance = std::numeric_limits<qint64>::max();
        for (auto* screen : QGuiApplication::screens()) {
            const QRect area = screen->geometry();
            const qint64 dx = qint64(point.x()) - std::clamp(point.x(), area.left(), area.right());
            const qint64 dy = qint64(point.y()) - std::clamp(point.y(), area.top(), area.bottom());
            const qint64 d = dx * dx + dy * dy;
            if (d < distance) {
                distance = d;
                best = screen;
            }
        }
        return best;
    }
    void applyScale(qreal scale) {
        if (qFuzzyCompare(scale, uiScale) && width() == qCeil((barWidth() + 64) * scale)) {
            for (int i = 0; i < buttons.size(); ++i) {
                buttons[i]->setUiScale(scale);
                buttons[i]->move(qRound((T::padding + 17 + i * (T::cell + T::gap)) * scale),
                                 qRound(60 * scale));
            }
            return;
        }
        uiScale = scale;
        setFixedSize(qCeil((barWidth() + 64) * scale), qCeil(DockPlacement::windowHeight * scale));
        for (int i = 0; i < buttons.size(); ++i) {
            buttons[i]->setUiScale(scale);
            buttons[i]->move(qRound((T::padding + 17 + i * (T::cell + T::gap)) * scale),
                             qRound(60 * scale));
        }
    }
    void watchScreen(QScreen* screen) {
        connect(screen, &QScreen::availableGeometryChanged, this, [this] { queueConstrain(); });
        connect(screen, &QScreen::geometryChanged, this, [this] { queueConstrain(); });
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { queueConstrain(); });
    }
    void queueConstrain() {
        if (embedded || constrainQueued)
            return;
        constrainQueued = true;
        QTimer::singleShot(0, this, [this] {
            constrainQueued = false;

            const QPoint centre = mapToGlobal(panelGeometry().center().toPoint());
            QScreen* target = QGuiApplication::screenAt(centre);
            if (!target && constrainedScreen &&
                QGuiApplication::screens().contains(constrainedScreen))
                target = constrainedScreen;
            if (!target)
                target = nearestScreen(centre);
            moveInsideWorkArea(pos(), target);
        });
    }
    QRect labelWorkArea() const {
        if (embedded)
            return QRect(parentWidget()->mapToGlobal(QPoint(0, 0)), parentWidget()->size());
        auto* target = constrainedScreen
                           ? constrainedScreen.data()
                           : nearestScreen(mapToGlobal(panelGeometry().center().toPoint()));
        return target ? target->availableGeometry() : QRect(mapToGlobal(QPoint(0, 0)), size());
    }
    QString labelText(int index) const {
        return store.entries().value(index).name;
    }
    QRectF labelInDesignUnits(int index) const {
        const QFontMetricsF metrics(QFont(T::font, 9));
        const qreal textWidth = metrics.horizontalAdvance(labelText(index)) + 22;
        const qreal centreX = T::padding + 17 + index * (T::cell + T::gap) + T::cell / 2.;
        const auto bounds = DockPlacement::labelBounds(mapToGlobal(QPoint(0, 0)), labelWorkArea(),
                                                       uiScale, barWidth());
        return DockPlacement::placeLabel(textWidth, centreX, bounds);
    }

protected:
    void showEvent(QShowEvent* e) override {
        QWidget::showEvent(e);
        if (!embedded) {
            if (windowHandle())
                connect(windowHandle(), &QWindow::screenChanged, this,
                        [this] { queueConstrain(); });
            queueConstrain();
        }
    }
    void moveEvent(QMoveEvent* e) override {
        QWidget::moveEvent(e);
        update();

        if (!embedded && !dragging)
            queueConstrain();
    }
    bool eventFilter(QObject* obj, QEvent* event) override {
        int index = -1;
        for (int i = 0; i < buttons.size(); ++i)
            if (obj == buttons[i])
                index = i;
        if (index < 0)
            return QWidget::eventFilter(obj, event);
        if (event->type() == QEvent::ContextMenu) {
            auto* e = static_cast<QContextMenuEvent*>(event);
            std::unique_ptr<QMenu> menu(makeMenu(store.entries()[index].id, this));
            menu->exec(e->globalPos());
            return true;
        }
        if (event->type() == QEvent::Enter) {
            hoverIndex = index;
            shownTip = -1;
            tooltipDelay.start();
            update();
        }
        if (event->type() == QEvent::Leave) {
            tooltipDelay.stop();
            hoverIndex = -1;
            shownTip = -1;
            update();
        }
        if (event->type() == QEvent::FocusIn) {
            const auto reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason ||
                reason == Qt::ShortcutFocusReason) {
                tooltipDelay.stop();
                shownTip = index;
                update();
            }
        }
        if (event->type() == QEvent::FocusOut) {
            shownTip = -1;
            update();
        }
        if (event->type() == QEvent::MouseButtonPress) {
            tooltipDelay.stop();
            shownTip = -1;
            update();
        }
        if (event->type() == QEvent::KeyPress) {
            auto* e = static_cast<QKeyEvent*>(event);
            if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right || e->key() == Qt::Key_Home ||
                e->key() == Qt::Key_End) {
                const int count = int(buttons.size());
                int next = e->key() == Qt::Key_Home ? 0
                           : e->key() == Qt::Key_End
                               ? count - 1
                               : (index + (e->key() == Qt::Key_Left ? count - 1 : 1)) % count;
                if (!buttons[next]->isEnabled())
                    next = (next + (e->key() == Qt::Key_Left ? int(buttons.size()) - 1 : 1)) %
                           int(buttons.size());
                buttons[next]->setFocus(Qt::TabFocusReason);
                buttons[next]->showKeyboardFocus();
                tooltipDelay.stop();
                shownTip = next;
                update();
                return true;
            }
            if (e->key() == Qt::Key_Escape) {
                window()->close();
                return true;
            }
        }
        return QWidget::eventFilter(obj, event);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        if (!embedded) {
            p.setCompositionMode(QPainter::CompositionMode_Source);
            p.fillRect(rect(), Qt::transparent);
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(uiScale, uiScale);
        QRectF box(T::padding, 52, barWidth(), T::dockHeight);
        p.setBrush(Qt::NoBrush);
        static const QPixmap shadow(":/dock-shadow.png");
        const qreal total = barWidth() + 64, cut = 80, ratio = shadow.width() / 520.;
        p.drawPixmap(QRectF(0, 0, cut, 150), shadow, QRectF(0, 0, cut * ratio, shadow.height()));
        p.drawPixmap(QRectF(cut, 0, total - 2 * cut, 150), shadow,
                     QRectF(cut * ratio, 0, (520 - 2 * cut) * ratio, shadow.height()));
        p.drawPixmap(QRectF(total - cut, 0, cut, 150), shadow,
                     QRectF((520 - cut) * ratio, 0, cut * ratio, shadow.height()));
        QLinearGradient glass(box.topLeft(), box.bottomLeft());
        glass.setColorAt(0, T::top);
        glass.setColorAt(.58, T::mid);
        glass.setColorAt(1, T::bottom);
        p.setPen(QPen(T::edge, 1));
        p.setBrush(glass);
        p.drawRoundedRect(box, 18, 18);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(T::hover, 1));
        p.drawRoundedRect(box.adjusted(2, 2, -2, -2), 15, 15);
        QLinearGradient rim(box.topLeft(), box.topRight());
        rim.setColorAt(0, T::transparent);
        rim.setColorAt(.3, T::reflection);
        rim.setColorAt(.7, T::reflection);
        rim.setColorAt(1, T::transparent);
        p.setPen(QPen(QBrush(rim), 1));
        p.drawLine(box.topLeft() + QPointF(18, 1), box.topRight() + QPointF(-18, 1));
        QLinearGradient lower(box.bottomLeft(), box.bottomRight());
        lower.setColorAt(0, T::transparent);
        lower.setColorAt(.5, T::lowerEdge);
        lower.setColorAt(1, T::transparent);
        p.setPen(QPen(QBrush(lower), 1));
        p.drawLine(box.bottomLeft() + QPointF(18, -1), box.bottomRight() + QPointF(-18, -1));
        if (buttons.isEmpty()) {
            p.setFont(QFont(T::font, 9));
            p.setPen(T::muted);
            p.drawText(box, Qt::AlignCenter, "Right-click to add apps");
        }
        int tip = shownTip;
        if (tip >= 0 && tip < store.entries().size()) {
            QString text = labelText(tip);
            p.setFont(QFont(T::font, 9));
            QRectF pill = labelInDesignUnits(tip);
            p.setPen(Qt::NoPen);
            p.setBrush(T::label);
            p.drawRoundedRect(pill, 7, 7);
            p.setPen(T::ink);
            p.drawText(pill, Qt::AlignCenter, text);
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (!embedded && e->button() == Qt::LeftButton && panelGeometry().contains(e->position())) {
            dragging = true;
            dragAnchor = e->position() / uiScale;
            e->accept();
        } else
            QWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging)
            return;
        if (!(e->buttons() & Qt::LeftButton)) {
            dragging = false;
            return;
        }
        auto* target = nearestScreen(e->globalPosition().toPoint());
        if (!target)
            return;

        applyScale(DockPlacement::scaleForArea(target->availableGeometry(), barWidth()));
        moveInsideWorkArea((e->globalPosition() - dragAnchor * uiScale).toPoint(), target);
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            dragging = false;
            queueConstrain();
            store.savePosition(pos());
            e->accept();
        }
    }
    bool event(QEvent* e) override {
        if (e->type() == QEvent::UngrabMouse || e->type() == QEvent::WindowDeactivate)
            dragging = false;
        return QWidget::event(e);
    }
    void contextMenuEvent(QContextMenuEvent* e) override {
        std::unique_ptr<QMenu> menu(makeMenu({}, this));
        menu->exec(e->globalPos());
    }
    void closeEvent(QCloseEvent* e) override {
        if (!embedded)
            store.savePosition(pos());
        if (!embedded && QSystemTrayIcon::isSystemTrayAvailable()) {
            hide();
            e->ignore();
        } else {
            e->accept();
            if (!embedded)
                qApp->quit();
        }
    }
#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override {
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == activationMessage) {
            if (requestSettings)
                requestSettings();
            *result = 0;
            return true;
        }
        return QWidget::nativeEvent(type, message, result);
    }
#endif
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape)
            window()->close();
        else
            QWidget::keyPressEvent(e);
    }
};

class Scene final : public QWidget {
    QPixmap wallpaper{":/wallpaper.jpg"};
    Dock* dock;

public:
    explicit Scene(LauncherStore& store) {
        setWindowTitle("OPIA's Minidock");
        resize(1100, 650);
        dock = new Dock(store, this);
    }

protected:
    void resizeEvent(QResizeEvent*) override {
        dock->move((width() - dock->width()) / 2, int(height() * .54) - 84);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPixmap scaled =
            wallpaper.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
        p.setFont(QFont(T::font, 10, QFont::Medium));
        p.setPen(T::ink);
        p.drawText(28, height() - 25, "OPIA's MiniDock");
        p.setFont(QFont(T::font, 9));
        p.setPen(T::muted);
        p.drawText(142, height() - 25, "UI mock-up · 02");
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape)
            close();
        else
            QWidget::keyPressEvent(e);
    }
};
static void configureTray(QSystemTrayIcon& tray, QMenu& menu, Dock& dock,
                          const std::function<void()>& openSettings) {
    menu.setStyleSheet(miniDockDialogStyle());
    auto* settingsAction = menu.addAction("Settings");
    settingsAction->setObjectName("traySettingsAction");
    QObject::connect(settingsAction, &QAction::triggered, &dock, openSettings);
    auto* show = menu.addAction("Show dock");
    show->setObjectName("showDockAction");
    QObject::connect(show, &QAction::triggered, &dock, [&dock] {
        dock.show();
        dock.raise();
        dock.activateWindow();
    });
    menu.addSeparator();
    QObject::connect(menu.addAction("Exit MiniDock"), &QAction::triggered, qApp,
                     &QCoreApplication::quit);
    tray.setToolTip("OPIA's Minidock");
    tray.setContextMenu(&menu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, &dock,
                     [openSettings](QSystemTrayIcon::ActivationReason why) {
                         if (why == QSystemTrayIcon::DoubleClick)
                             openSettings();
                     });
}

int main(int argc, char** argv) {
#ifdef Q_OS_WIN
    struct ComApartment {
        HRESULT result = OleInitialize(nullptr);
        ~ComApartment() {
            if (SUCCEEDED(result))
                OleUninitialize();
        }
    } com;
#endif
    QApplication app(argc, argv);
    app.setOrganizationName("OPIA");
    app.setApplicationName("MiniDock");
    app.setApplicationDisplayName("OPIA's Minidock");
    app.setApplicationVersion("0.3.4");
    app.setWindowIcon(LauncherStore::applicationIcon());
    app.setFont(QFont(T::font, 10));
    app.setStyle(QStyleFactory::create("Fusion"));
    const auto args = app.arguments();
    QString config;
    int configFlag = args.indexOf("--config");
    if (configFlag >= 0 && configFlag + 1 < args.size())
        config = args[configFlag + 1];
#ifdef Q_OS_WIN
    HANDLE mutex = nullptr;
    if (!args.contains("--snapshot") && !args.contains("--settings-snapshot") &&
        !args.contains("--no-single-instance")) {
        mutex = CreateMutexW(nullptr, FALSE, L"Local\\OPIA_MiniDock_v3");
        if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND existing = nullptr;
            for (int i = 0; i < 30 && !existing; ++i) {
                existing = findExistingDock();
                if (!existing)
                    QThread::msleep(50);
            }
            if (existing) {
                if (GetPropW(existing, L"OPIA.MiniDock.Build") == reinterpret_cast<HANDLE>(0x0304))
                    PostMessageW(existing, activationMessage, 0, 0);
                else
                    MessageBoxW(
                        nullptr,
                        L"An older MiniDock is still running. Exit it from its tray menu (or right-click the bar), then open this update again. Your apps and position are kept.",
                        L"OPIA's Minidock", MB_OK | MB_ICONINFORMATION);
            }
            CloseHandle(mutex);
            return 0;
        }
    }
#endif
    LauncherStore store(config);
    if (args.contains("--scene") || args.contains("--snapshot")) {
        Scene scene(store);
        scene.setFocusPolicy(Qt::StrongFocus);
        scene.show();
        scene.setFocus(Qt::OtherFocusReason);
        int flag = args.indexOf("--snapshot");
        if (flag >= 0) {
            if (flag + 1 >= args.size())
                return 2;
            QTimer::singleShot(150, &scene,
                               [&] { app.exit(scene.grab().save(args[flag + 1]) ? 0 : 3); });
        }
        return app.exec();
    }
    app.setQuitOnLastWindowClosed(false);
    Dock dock(store);
    QPointer<SettingsDialog> settings;
#ifdef Q_OS_WIN
    SetPropW(reinterpret_cast<HWND>(dock.winId()), L"OPIA.MiniDock.v3.Window",
             reinterpret_cast<HANDLE>(1));
    SetPropW(reinterpret_cast<HWND>(dock.winId()), L"OPIA.MiniDock.Build",
             reinterpret_cast<HANDLE>(0x0304));
#endif
    auto openSettings = [&] {
        if (!settings)
            settings = new SettingsDialog(store, &dock);
        settings->open();
    };
    dock.requestSettings = openSettings;
    store.changed = [&] {
        dock.rebuild();
        if (settings)
            settings->syncPinned();
    };
    QMenu trayMenu;
    QSystemTrayIcon tray(LauncherStore::applicationIcon());
    configureTray(tray, trayMenu, dock, openSettings);
    tray.show();
    const QRect screen = app.primaryScreen()->availableGeometry();
    dock.moveInsideWorkArea(
        store.hasPosition() ? store.savedPosition()
                            : QPoint(screen.center().x() - dock.width() / 2, screen.bottom() - 158),
        store.hasPosition() ? nullptr : app.primaryScreen());
    dock.show();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&] {
        store.savePosition(dock.pos());
        tray.hide();
    });
    if (args.contains("--settings") || args.contains("--settings-snapshot"))
        openSettings();
    int flag = args.indexOf("--settings-snapshot");
    if (flag >= 0) {
        if (flag + 1 >= args.size())
            return 2;
        QTimer::singleShot(1600, &app, [&] {
            app.exit(settings && settings->grab().save(args[flag + 1]) ? 0 : 3);
        });
    }
    int result = app.exec();
#ifdef Q_OS_WIN
    if (mutex)
        CloseHandle(mutex);
#endif
    return result;
}
