#include "launcher.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QSet>
#include <QStyle>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commoncontrols.h>
#endif

static QString normalized(QString path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toCaseFolded();
}
static QJsonObject json(const LaunchEntry& e) {
    return {{"id", e.id},
            {"name", e.name},
            {"target", e.target},
            {"kind", e.kind},
            {"source", e.source}};
}
LaunchEntry LauncherStore::explorer() {
    return {"builtin:explorer", "File Explorer", {}, "explorer", "Windows"};
}
LaunchEntry LauncherStore::recycle() {
    return {"builtin:recycle", "Recycle Bin", {}, "recycle", "Windows"};
}
bool LauncherStore::supportedFile(const QString& path) {
    const QFileInfo f(path);
    const QString ext = f.suffix().toLower();
    if (ext != "exe" && ext != "lnk" && ext != "url")
        return false;
#ifdef Q_OS_WIN

    const auto native = QDir::toNativeSeparators(f.absoluteFilePath()).toStdWString();
    const DWORD attributes = GetFileAttributesW(native.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    return f.isFile();
#endif
}
LaunchEntry LauncherStore::fileEntry(QString path, QString source) {
    path = QFileInfo(path).absoluteFilePath();
    return {"file:" + QString::fromLatin1(QCryptographicHash::hash(normalized(path).toUtf8(),
                                                                   QCryptographicHash::Sha256)
                                              .toHex()),
            QFileInfo(path).completeBaseName(), path, "file", source};
}
static LaunchEntry fromJson(const QJsonObject& o) {
    const QString kind = o.value("kind").toString();
    if (kind == "explorer")
        return LauncherStore::explorer();
    if (kind == "recycle")
        return LauncherStore::recycle();
    if (kind != "file" || o.value("target").toString().isEmpty())
        return {};
    auto e = LauncherStore::fileEntry(o.value("target").toString(),
                                      o.value("source").toString("Custom app"));

    if (!QStringList{"exe", "lnk", "url"}.contains(QFileInfo(e.target).suffix().toLower()))
        return {};
    e.name = o.value("name").toString(e.name).left(180);
    return e;
}
LauncherStore::LauncherStore(QString file, QObject* parent) : QObject(parent) {
    configPath_ = file.isEmpty()
                      ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                            .filePath("settings.json")
                      : file;
    pinned_ = {explorer(), recycle()};
    QFile f(configPath_);
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly)) {
        loadNotice =
            "Your saved settings could not be opened. File Explorer and Recycle Bin are shown for now.";
        return;
    }
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject() ||
        !doc.object().value("apps").isArray()) {
        QFile::copy(configPath_,
                    configPath_ + ".backup-" +
                        QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmsszzz"));
        loadNotice =
            "Settings were unreadable. A backup was kept; the default apps have been restored.";
        return;
    }
    const auto obj = doc.object();
    pinned_.clear();
    QSet<QString> ids;
    for (const auto& val : obj.value("apps").toArray()) {
        auto e = fromJson(val.toObject());
        if (e.id.isEmpty() || ids.contains(e.id))
            continue;
        pinned_.push_back(e);
        ids.insert(e.id);
        if (pinned_.size() >= 32)
            break;
    }
    for (const auto& val : obj.value("recent").toArray()) {
        auto e = fromJson(val.toObject());
        if (!e.id.isEmpty())
            recent_.push_back(e);
        if (recent_.size() >= 10)
            break;
    }
    const auto pos = obj.value("position").toObject();
    if (pos.contains("x") && pos.contains("y")) {
        savedPosition_ = QPoint(pos["x"].toInt(), pos["y"].toInt());
        hasPosition_ = true;
    }
}
bool LauncherStore::write(const QVector<LaunchEntry>& items, const QVector<LaunchEntry>& recent,
                          QString* error) {
    QJsonArray list, history;
    for (const auto& e : items)
        list.append(json(e));
    for (const auto& e : recent)
        history.append(json(e));
    QJsonObject obj{{"version", 3}, {"apps", list}, {"recent", history}};
    if (hasPosition_)
        obj["position"] = QJsonObject{{"x", savedPosition_.x()}, {"y", savedPosition_.y()}};
    QDir().mkpath(QFileInfo(configPath_).absolutePath());
    QSaveFile file(configPath_);
    const auto bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error)
            *error = "Could not save your dock: " + file.errorString();
        return false;
    }
    return true;
}
bool LauncherStore::contains(const QString& id) const {
    for (const auto& e : pinned_)
        if (e.id == id)
            return true;
    return false;
}
bool LauncherStore::add(const LaunchEntry& e, QString* error) {
    if (contains(e.id))
        return true;
    if (pinned_.size() >= 32) {
        if (error)
            *error = "Your dock is full (32 apps). Remove an app before adding another.";
        return false;
    }
    if (e.id.isEmpty() || (e.kind == "file" && !supportedFile(e.target))) {
        if (error)
            *error = "Choose an existing .exe, .lnk, or .url file.";
        return false;
    }
    auto next = pinned_;
    next.push_back(e);
    if (!write(next, recent_, error))
        return false;
    pinned_ = next;
    if (changed)
        changed();
    return true;
}
bool LauncherStore::remove(const QString& id, QString* error) {
    auto next = pinned_;
    next.erase(std::remove_if(next.begin(), next.end(), [&](const auto& e) { return e.id == id; }),
               next.end());
    if (!write(next, recent_, error))
        return false;
    pinned_ = next;
    if (changed)
        changed();
    return true;
}
bool LauncherStore::savePosition(QPoint point, QString* error) {
    const auto old = savedPosition_;
    const bool had = hasPosition_;
    savedPosition_ = point;
    hasPosition_ = true;
    if (write(pinned_, recent_, error))
        return true;
    savedPosition_ = old;
    hasPosition_ = had;
    return false;
}
#ifdef Q_OS_WIN
static QString windowsFolder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &path)))
        return {};
    QString result = QString::fromWCharArray(path);
    CoTaskMemFree(path);
    return result;
}
static QIcon fromHandle(HICON handle) {
    if (!handle)
        return {};
    const QImage image = QImage::fromHICON(handle).convertToFormat(QImage::Format_ARGB32);
    DestroyIcon(handle);
    if (image.isNull())
        return {};
    int left = image.width(), top = image.height(), right = -1, bottom = -1;
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(row[x]) > 2) {
                left = qMin(left, x);
                top = qMin(top, y);
                right = qMax(right, x);
                bottom = qMax(bottom, y);
            }
    }
    if (right < left || bottom < top)
        return {};
    const auto content = image.copy(QRect(QPoint(left, top), QPoint(right, bottom)))
                             .scaled(224, 224, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap canvas(256, 256);
    canvas.fill(Qt::transparent);
    QPainter p(&canvas);
    p.drawImage((256 - content.width()) / 2, (256 - content.height()) / 2, content);
    p.end();
    return QIcon(canvas);
}
#endif
QIcon LauncherStore::applicationIcon() {
    QIcon result;
    for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256})
        result.addFile(QString(":/icons/appmark-%1.png").arg(size), QSize(size, size));
    return result;
}
QIcon LauncherStore::icon(const LaunchEntry& e) {
    if (iconCache.contains(e.id))
        return iconCache.value(e.id);
    QIcon result;

    if (e.kind == "explorer")
        result = QIcon(":/icons/explorer-modern.png");
    else if (e.kind == "recycle")
        result = QIcon(":/icons/recycle-modern.png");
#ifdef Q_OS_WIN
    if (e.kind == "file") {
        SHFILEINFOW info{};
        const auto path = QDir::toNativeSeparators(e.target).toStdWString();
        if (SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX)) {

            for (int size : {SHIL_JUMBO, SHIL_EXTRALARGE}) {
                IImageList* images = nullptr;
                if (SUCCEEDED(SHGetImageList(size, __uuidof(IImageList),
                                             reinterpret_cast<void**>(&images))) &&
                    images) {
                    HICON handle = nullptr;
                    const HRESULT status = images->GetIcon(info.iIcon, ILD_TRANSPARENT, &handle);
                    images->Release();
                    if (SUCCEEDED(status) && handle)
                        result = fromHandle(handle);
                }
                if (!result.isNull())
                    break;
            }
        }
        if (result.isNull() &&
            SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON))
            result = fromHandle(info.hIcon);
    }
#endif
    if (result.isNull())
        result = QFileIconProvider().icon(QFileInfo(e.target));
    if (result.isNull())
        result = qApp->style()->standardIcon(QStyle::SP_FileIcon);
    iconCache.insert(e.id, result);
    return result;
}
bool LauncherStore::launch(const LaunchEntry& e, QString* error) {
    if (e.kind == "file" && !supportedFile(e.target)) {
        if (error)
            *error =
                "This app or shortcut is no longer available. Remove it from the dock and add its new location.";
        return false;
    }
    bool ok = false;
#ifdef Q_OS_WIN
    QString target = e.target, args;
    if (e.kind == "explorer" || e.kind == "recycle") {
        target = QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath("explorer.exe");
        if (e.kind == "recycle")
            args = "shell:RecycleBinFolder";
    }
    const auto path = QDir::toNativeSeparators(target).toStdWString(), params = args.toStdWString();
    const auto dir = QDir::toNativeSeparators(QFileInfo(target).absolutePath()).toStdWString();
    SHELLEXECUTEINFOW sh{};
    sh.cbSize = sizeof(sh);
    sh.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    sh.lpVerb = L"open";
    sh.lpFile = path.c_str();
    sh.lpParameters = args.isEmpty() ? nullptr : params.c_str();
    sh.lpDirectory = dir.c_str();
    sh.nShow = SW_SHOWNORMAL;
    ok = ShellExecuteExW(&sh);
    if (!ok && error) {
        const DWORD code = GetLastError();
        LPWSTR text = nullptr;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                           FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
        *error = "Windows could not open " + e.name + ". " +
                 (text ? QString::fromWCharArray(text).trimmed() : QString("Error %1").arg(code));
        if (text)
            LocalFree(text);
    }
#else
    if (e.kind == "file")
        ok = QDesktopServices::openUrl(QUrl::fromLocalFile(e.target));
    else if (e.kind == "explorer")
        ok = QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::homePath()));
    else
        ok = QDesktopServices::openUrl(QUrl("trash:/"));
    if (!ok && error)
        *error = "The system could not open this item.";
#endif
    if (ok) {
        auto recent = recent_;
        recent.erase(std::remove_if(recent.begin(), recent.end(),
                                    [&](const auto& item) { return item.id == e.id; }),
                     recent.end());
        recent.prepend(e);
        while (recent.size() > 10)
            recent.removeLast();
        QString warning;
        if (write(pinned_, recent, &warning))
            recent_ = recent;
        else
            loadNotice = warning;
    }
    return ok;
}
bool LauncherStore::launchId(const QString& id, QString* error) {
    for (const auto& e : pinned_)
        if (e.id == id)
            return launch(e, error);
    if (error)
        *error = "This item is no longer in the dock.";
    return false;
}
QVector<LaunchEntry> LauncherStore::discover(const QString& category) {
    QVector<LaunchEntry> result;
    QStringList roots;
    bool recurse = category == "Installed apps";
#ifdef Q_OS_WIN
    if (category == "Desktop shortcuts")
        roots = {windowsFolder(FOLDERID_Desktop), windowsFolder(FOLDERID_PublicDesktop)};
    else if (category == "Pinned apps")
        roots = {QDir(windowsFolder(FOLDERID_RoamingAppData))
                     .filePath("Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar")};
    else {
        roots = {windowsFolder(FOLDERID_Programs), windowsFolder(FOLDERID_CommonPrograms)};
        result = {explorer(), recycle()};
        const QDir system(QDir(qEnvironmentVariable("WINDIR", "C:/Windows")).filePath("System32"));
        for (const auto& pair :
             QVector<QPair<QString, QString>>{{"notepad.exe", "Notepad"},
                                              {"calc.exe", "Calculator"},
                                              {"SnippingTool.exe", "Snipping Tool"},
                                              {"mspaint.exe", "Paint"}}) {
            QString path = system.filePath(pair.first);
            if (QFileInfo::exists(path)) {
                auto e = fileEntry(path, "Windows");
                e.name = pair.second;
                result.push_back(e);
            }
        }
    }
#else
    roots = category == "Desktop shortcuts"
                ? QStringList{QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)}
                : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation);
    if (category == "Installed apps")
        result = {explorer(), recycle()};
#endif
    QSet<QString> seen;
    for (const auto& e : result)
        seen.insert(e.id);
    for (const auto& root : roots) {
        if (root.isEmpty() || !QDir(root).exists())
            continue;
        QDirIterator it(root, QStringList{"*.lnk", "*.url", "*.exe"}, QDir::Files | QDir::System,
                        recurse ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            const QString path = it.next();
            if (!supportedFile(path))
                continue;
            auto e = fileEntry(path, category);
            if (seen.contains(e.id))
                continue;
            seen.insert(e.id);
            result.push_back(e);
            if (result.size() >= 2000)
                break;
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return result;
}
