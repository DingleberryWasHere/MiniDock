#pragma once
#include <QDialog>
#include <QThread>
#include "launcher.h"
class QLineEdit;
class QLabel;
class QListView;
class QSortFilterProxyModel;
class EntryListModel;
class SettingsDialog final : public QDialog {
    LauncherStore& store;
    EntryListModel* model;
    QSortFilterProxyModel* proxy;
    QLineEdit* search;
    QListView* list;
    QLabel* status;
    QLabel* empty;
    QThread* scanner = nullptr;
    int feedbackSerial = 0;
    QString category = "Installed apps";
    QVector<QWidget*> quickButtons;
    void scan(QString source);
    void updateEmpty();
    void addFiles(const QStringList& paths);

protected:
    void paintEvent(QPaintEvent*) override;

public:
    explicit SettingsDialog(LauncherStore&, QWidget* parent = nullptr);
    ~SettingsDialog() override;
    void syncPinned();
    void open();
    void addEntry(const LaunchEntry&);
    void browse();
    void message(QString text, bool error = false);
};
QString miniDockDialogStyle();
