#pragma once
#include "analysis.h"
#include "editor.h"
#include "formats.h"
#include <QtWidgets>
class Plot final : public QWidget {
  public:
    Report report;
    bool histogram;
    explicit Plot(bool hist) : histogram(hist) {
        setMinimumSize(180, 90);
        setMouseTracking(true);
    }

  protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
};
class Window final : public QMainWindow {
  public:
    Window();
    bool open(const QString &);
    Editor *editor() const;
    bool save(bool copy);

  protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    QTabWidget *tabs, *bottom;
    QTreeWidget *tree, *inspector, *hits;
    QComboBox *endian, *radix, *width;
    QPlainTextEdit *log;
    Plot *hist, *entropyPlot;
    QLabel *summary, *location, *mode;
    QAction *writeAction, *insertAction;
    QMenu *recent;
    std::shared_ptr<std::atomic_bool> stop;
    QStringList comparePaths;
    bool comparisonReady = false;
    quint64 treeRevision = 0;
    QTimer treeTimer;
    void update();
    void resetResults();
    void refreshTree();
    void recentMenu();
    void trackRecent(const QString &path);
    void run(Operation, bool selection = false, QByteArray query = {}, Snapshot other = {});
    void find();
    void compare();
    void extract();
    void exportDiff();
    void record(QString);
    bool closeTab(int);
};
