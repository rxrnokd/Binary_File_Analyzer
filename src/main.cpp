#include "window.h"
class Application final : public QApplication {
  public:
    using QApplication::QApplication;
    Window *window = nullptr;
    QStringList pending;
    bool event(QEvent *e) override {
        if (e->type() == QEvent::FileOpen) {
            auto path = static_cast<QFileOpenEvent *>(e)->file();
            if (!path.isEmpty()) {
                if (window)
                    window->open(path);
                else
                    pending << path;
            }
            return true;
        }
        return QApplication::event(e);
    }
};
int main(int argc, char **argv) {
    Application app(argc, argv);
    app.setApplicationName("HEXFORGE");
    app.setOrganizationName("Hexforge");
    app.setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#191f28"));
    palette.setColor(QPalette::Base, QColor("#141a22"));
    palette.setColor(QPalette::AlternateBase, QColor("#181f28"));
    palette.setColor(QPalette::Text, QColor("#d6e1ec"));
    palette.setColor(QPalette::WindowText, QColor("#d6e1ec"));
    palette.setColor(QPalette::Button, QColor("#222c37"));
    palette.setColor(QPalette::ButtonText, QColor("#d6e1ec"));
    palette.setColor(QPalette::Highlight, QColor("#245678"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#718191"));
    app.setPalette(palette);
    app.setStyleSheet(R"(
        QMainWindow::separator { background:#303b48; width:2px; height:2px; }
        QToolBar { background:#171d25; border-bottom:1px solid #35404d; spacing:4px; padding:5px; }
        QToolButton { padding:5px 8px; border-radius:4px; }
        QToolButton:hover,QToolButton:checked { background:#26485b; color:#a6f5ff; }
        QDockWidget::title { background:#202a35; padding:7px; color:#b9cbdc; }
        QTreeWidget,QPlainTextEdit { border:0; }
        QHeaderView::section { background:#1c2530; color:#8fa4b9; border:0; padding:5px; }
        QTabBar::tab { padding:7px 12px; background:#1a212a; border-bottom:2px solid transparent; }
        QTabBar::tab:selected { color:#8ee6f2; background:#202b36; border-bottom:2px solid #58cde4; }
        QStatusBar { background:#18232e; color:#a7c0d4; }
    )");
    Window window;
    app.window = &window;
    window.show();
    for (const auto &path : app.pending + app.arguments().mid(1))
        window.open(path);
    return app.exec();
}
