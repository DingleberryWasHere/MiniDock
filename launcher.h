#pragma once
#include <QObject>
#include <QIcon>
#include <QPoint>
#include <QVector>
#include <QHash>
#include <QStringList>
#include <functional>

struct LaunchEntry {
    QString id, name, target, kind = "file", source = "Installed apps";
};
class LauncherStore : public QObject {
    QVector<LaunchEntry> pinned_, recent_;
    QString configPath_;
    QHash<QString, QIcon> iconCache;
    QPoint savedPosition_;
    bool hasPosition_ = false;
    bool write(const QVector<LaunchEntry>& items, const QVector<LaunchEntry>& recent,
               QString* error);

public:
    QString loadNotice;
    std::function<void()> changed;
    explicit LauncherStore(QString configFile = {}, QObject* parent = nullptr);
    const QVector<LaunchEntry>& entries() const {
        return pinned_;
    }
    const QVector<LaunchEntry>& recent() const {
        return recent_;
    }
    bool contains(const QString& id) const;
    bool add(const LaunchEntry&, QString* error = nullptr);
    bool remove(const QString&, QString* error = nullptr);
    bool launch(const LaunchEntry&, QString* error = nullptr);
    bool launchId(const QString&, QString* error = nullptr);
    QIcon icon(const LaunchEntry&);
    void refreshRecycleIcon() {
        iconCache.remove("builtin:recycle");
    }
    bool savePosition(QPoint point, QString* error = nullptr);
    QPoint savedPosition() const {
        return savedPosition_;
    }
    bool hasPosition() const {
        return hasPosition_;
    }
    QString configPath() const {
        return configPath_;
    }
    static LaunchEntry explorer();
    static LaunchEntry recycle();
    static LaunchEntry fileEntry(QString path, QString source = "Custom app");
    static bool supportedFile(const QString& path);
    static QVector<LaunchEntry> discover(const QString& category = "Installed apps");
    static QIcon applicationIcon();
};
