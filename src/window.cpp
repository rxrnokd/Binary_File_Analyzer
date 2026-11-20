#include "window.h"
#include <QtConcurrent>
#include <cmath>
namespace {
QString hex(qint64 n) { return "0x" + QString::number(n, 16).toUpper(); }
QTreeWidget *table(QStringList header) {
    auto t = new QTreeWidget;
    t->setColumnCount(header.size());
    t->setHeaderLabels(header);
    t->setUniformRowHeights(true);
    t->setRootIsDecorated(false);
    t->setAlternatingRowColors(true);
    t->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    return t;
}
void addNode(QTreeWidgetItem *p, const Field &f) {
    auto child = new QTreeWidgetItem(p, {f.label, hex(f.offset)});
    child->setData(0, Qt::UserRole, f.offset);
    child->setData(1, Qt::UserRole, f.length);
    for (const auto &item : f.children)
        addNode(child, item);
}
} // namespace
void Plot::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#131921"));
    QRectF box = rect().adjusted(12, 12, -12, -23);
    if (box.width() < 1 || box.height() < 1)
        return;
    p.setPen(QColor("#2b3846"));
    for (int i = 0; i <= 4; ++i) {
        auto y = box.bottom() - box.height() * i / 4;
        p.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
    }
    if (report.hash.isEmpty())
        return;
    int n = histogram ? 256 : report.regions.size();
    auto peak = *std::max_element(report.frequency.begin(), report.frequency.end());
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath line;
    for (int i = 0; i < n; ++i) {
        double v = histogram ? double(report.frequency[i]) / std::max<quint64>(1, peak)
                             : report.regions[i] / 8;
        auto x = box.left() + box.width() * i / (histogram ? n : std::max(1, n - 1)),
             y = box.bottom() - box.height() * v;
        if (histogram)
            p.fillRect(QRectF(x, y, std::max(1., box.width() / 256), box.bottom() - y),
                       QColor("#4eaff2"));
        else {
            // Each estimate describes the entire byte interval, including the shorter final one.
            double left = box.left() + box.width() * (i * report.regionSize) / report.count;
            qint64 offset = i * report.regionSize;
            auto end = offset + std::min(report.regionSize, report.count - offset);
            double right = box.left() + box.width() * end / report.count;
            if (i)
                line.lineTo(left, y);
            else
                line.moveTo(left, y);
            line.lineTo(right, y);
        }
    }
    if (!histogram) {
        p.setPen(QPen(QColor("#5de8cf"), 2));
        p.drawPath(line);
    }
    p.setPen(QColor("#91a3b6"));
    p.drawText(12, height() - 5, histogram ? "00" : hex(report.start));
    p.drawText(rect().adjusted(0, 0, -12, -4), Qt::AlignRight | Qt::AlignBottom,
               histogram ? "FF" : hex(report.start + report.count));
}
void Plot::mouseMoveEvent(QMouseEvent *e) {
    if (report.hash.isEmpty())
        return;
    int n = histogram ? 256 : report.regions.size();
    if (!n)
        return;
    int i = std::clamp(int((e->position().x() - 12) / std::max(1, width() - 24) * n), 0, n - 1);
    if (!histogram) {
        double fraction = std::clamp((e->position().x() - 12) / std::max(1, width() - 24), 0., 1.);
        i = std::min(n - 1, int(fraction * (double(report.count) / report.regionSize)));
    }
    setToolTip(histogram ? QString("%1 · %2 bytes").arg(hex(i)).arg(report.frequency[i])
                         : QString("%1 · %2 bits/byte")
                               .arg(hex(report.start + i * report.regionSize))
                               .arg(report.regions[i], 0, 'f', 3));
}
Editor *Window::editor() const { return qobject_cast<Editor *>(tabs->currentWidget()); }
Window::Window() {
    setWindowTitle("HEXFORGE");
    setAcceptDrops(true);
    resize(1440, 880);
    tabs = new QTabWidget;
    tabs->setDocumentMode(true);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    setCentralWidget(tabs);
    auto dock = [&](QString title, QString id, Qt::DockWidgetArea area, QWidget *body) {
        auto d = new QDockWidget(title, this);
        d->setObjectName(id);
        d->setWidget(body);
        addDockWidget(area, d);
        return d;
    };
    auto file = menuBar()->addMenu("&File"), edit = menuBar()->addMenu("&Edit"),
         an = menuBar()->addMenu("&Analysis"), view = menuBar()->addMenu("&View");
    auto bar = addToolBar("Workspace");
    bar->setObjectName("workspace");
    bar->setMovable(false);
    auto brand = new QLabel("HEXFORGE");
    brand->setStyleSheet("color:#73ddf1;font-weight:800;font-size:16px;padding:0 16px 0 6px");
    bar->addWidget(brand);
    auto action = [&](QMenu *menu, QString name, QKeySequence key, auto fn) {
        auto a = menu->addAction(name);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn);
        return a;
    };
    bar->addAction(action(file, "Open…", QKeySequence::Open, [this] {
        for (const auto &p : QFileDialog::getOpenFileNames(this, "Open binary files"))
            open(p);
    }));
    recent = file->addMenu("Recent files");
    recentMenu();
    bar->addAction(action(file, "Save", QKeySequence::Save, [this] { save(false); }));
    action(file, "Save as…", QKeySequence::SaveAs, [this] { save(true); });
    action(file, "Extract selection…", {}, [this] { extract(); });
    action(file, "Export differences…", {}, [this] { exportDiff(); });
    action(file, "Close tab", QKeySequence::Close, [this] { closeTab(tabs->currentIndex()); });
    bar->addSeparator();
    writeAction = action(edit, "Edit mode", QKeySequence("Ctrl+E"), [this](bool on) {
        if (auto e = editor()) {
            e->doc.writable = on;
            e->select(e->cursor);
            update();
        }
    });
    writeAction->setCheckable(true);
    bar->addAction(writeAction);
    insertAction = action(edit, "Insert mode", QKeySequence(Qt::Key_Insert), [this](bool on) {
        if (auto e = editor()) {
            e->insertMode = on;
            update();
        }
    });
    insertAction->setCheckable(true);
    action(edit, "Undo", QKeySequence::Undo, [this] {
        if (auto e = editor(); e && e->doc.writable)
            e->doc.history.undo();
    });
    action(edit, "Redo", QKeySequence::Redo, [this] {
        if (auto e = editor(); e && e->doc.writable)
            e->doc.history.redo();
    });
    action(edit, "Copy", QKeySequence::Copy, [this] {
        if (editor())
            editor()->copy();
    });
    action(edit, "Paste", QKeySequence::Paste, [this] {
        if (editor())
            editor()->paste();
    });
    action(edit, "Delete selection", {}, [this] {
        if (editor())
            editor()->erase();
    });
    action(edit, "Select all", QKeySequence::SelectAll, [this] {
        if (auto e = editor()) {
            e->select(0);
            e->select(std::max<qint64>(0, e->doc.size - 1), true);
        }
    });
    bar->addAction(action(edit, "Find…", QKeySequence::Find, [this] { find(); }));
    action(edit, "Go to offset…", QKeySequence("Ctrl+G"), [this] {
        if (auto e = editor()) {
            bool ok;
            auto t = QInputDialog::getText(this, "Go to offset", "Decimal or 0x-prefixed address",
                                           QLineEdit::Normal, hex(e->cursor), &ok);
            if (!ok)
                return;
            auto n = t.toLongLong(&ok, t.startsWith("0x", Qt::CaseInsensitive) ? 16 : 10);
            if (ok && n >= 0 && n <= e->doc.size)
                e->select(n);
            else
                statusBar()->showMessage("Offset outside file", 4000);
        }
    });
    bar->addSeparator();
    bar->addAction(action(an, "Analyze file", QKeySequence("Ctrl+Shift+A"),
                          [this] { run(Operation::Statistics); }));
    action(an, "Load binary template…", {}, [this] {
        auto e = editor();
        if (!e)
            return;
        auto path = QFileDialog::getOpenFileName(this, "Load binary template", {},
                                                 "JSON templates (*.json)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, "Template", file.errorString());
            return;
        }
        auto data = file.read(65537);
        QString error;
        applyTemplate(e->doc, data, error);
        if (!error.isEmpty()) {
            QMessageBox::warning(this, "Template", error);
            return;
        }
        e->structureTemplate = data;
        refreshTree();
    });
    action(an, "Analyze selection", {}, [this] { run(Operation::Statistics, true); });
    action(an, "Extract strings", {}, [this] { run(Operation::Strings); });
    action(an, "Find repeating patterns", {}, [this] { run(Operation::Patterns); });
    bar->addAction(action(an, "Compare…", {}, [this] { compare(); }));
    action(an, "Cancel analysis", {}, [this] {
        if (stop)
            stop->store(true);
    });
    auto space = new QWidget;
    space->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(space);
    width = new QComboBox;
    width->addItems({"8 bytes / row", "16 bytes / row", "32 bytes / row"});
    width->setCurrentIndex(QSettings().value("width", 1).toInt());
    bar->addWidget(width);
    tree = table({"Region", "Offset"});
    tree->setRootIsDecorated(true);
    view->addAction(
        dock("File structure", "structure", Qt::LeftDockWidgetArea, tree)->toggleViewAction());
    auto panel = new QWidget;
    auto il = new QVBoxLayout(panel);
    auto opts = new QHBoxLayout;
    endian = new QComboBox;
    endian->addItems({"Little endian", "Big endian"});
    radix = new QComboBox;
    radix->addItems({"Decimal", "Hex", "Binary"});
    endian->setCurrentIndex(QSettings().value("endian", 0).toInt());
    radix->setCurrentIndex(QSettings().value("radix", 0).toInt());
    opts->addWidget(endian);
    opts->addWidget(radix);
    il->addLayout(opts);
    inspector = table({"Type", "Value"});
    il->addWidget(inspector);
    view->addAction(
        dock("Data inspector", "inspector", Qt::RightDockWidgetArea, panel)->toggleViewAction());
    bottom = new QTabWidget;
    bottom->setDocumentMode(true);
    auto overview = new QWidget;
    auto ol = new QVBoxLayout(overview);
    summary = new QLabel("Run Analyze file to calculate byte statistics.");
    summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ol->addWidget(summary);
    auto plots = new QHBoxLayout;
    hist = new Plot(true);
    entropyPlot = new Plot(false);
    for (auto pair : {std::pair<QString, Plot *>{"Byte frequency", hist},
                      {"Shannon entropy · bits/byte", entropyPlot}}) {
        auto l = new QVBoxLayout;
        l->addWidget(new QLabel(pair.first));
        l->addWidget(pair.second);
        plots->addLayout(l);
    }
    ol->addLayout(plots);
    bottom->addTab(overview, "Overview");
    hits = table({"Offset", "Bytes", "Result"});
    bottom->addTab(hits, "Results");
    log = new QPlainTextEdit;
    log->setReadOnly(true);
    log->setMaximumBlockCount(200);
    log->setPlainText(QSettings().value("journal").toStringList().join('\n'));
    bottom->addTab(log, "Activity");
    view->addAction(
        dock("Analysis", "analysis", Qt::BottomDockWidgetArea, bottom)->toggleViewAction());
    location = new QLabel;
    mode = new QLabel;
    statusBar()->addWidget(location, 1);
    statusBar()->addPermanentWidget(mode);
    treeTimer.setSingleShot(true);
    connect(&treeTimer, &QTimer::timeout, this, [this] {
        if (!editor())
            return;
        auto revision = treeRevision;
        Snapshot s = editor()->doc;
        auto layout = editor()->structureTemplate;
        auto watcher = new QFutureWatcher<Field>(this);
        connect(watcher, &QFutureWatcher<Field>::finished, this, [this, watcher, revision] {
            watcher->deleteLater();
            if (revision != treeRevision)
                return;
            addNode(tree->invisibleRootItem(), watcher->result());
            tree->expandToDepth(0);
        });
        watcher->setFuture(QtConcurrent::run([s, layout] {
            auto root = structure(s);
            if (!layout.isEmpty()) {
                QString error;
                root.children << applyTemplate(s, layout, error);
            }
            return root;
        }));
    });
    connect(tabs, &QTabWidget::tabCloseRequested, this, [this](int i) { closeTab(i); });
    connect(tabs, &QTabWidget::currentChanged, this, [this] {
        resetResults();
        refreshTree();
        update();
    });
    connect(tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) {
        if (auto e = editor()) {
            auto at = i->data(0, Qt::UserRole).toLongLong(),
                 n = i->data(1, Qt::UserRole).toLongLong();
            e->select(n > 1 ? at + n - 1 : at);
            if (n > 1)
                e->select(at, true);
        }
    });
    connect(hits, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) {
        if (auto e = editor()) {
            auto at = i->data(0, Qt::UserRole).toLongLong(),
                 n = i->data(1, Qt::UserRole).toLongLong();
            e->select(n > 1 ? at + n - 1 : at);
            if (n > 1)
                e->select(at, true);
        }
    });
    connect(endian, &QComboBox::currentIndexChanged, this, [this] { update(); });
    connect(radix, &QComboBox::currentIndexChanged, this, [this] { update(); });
    connect(width, &QComboBox::currentIndexChanged, this, [this] {
        if (auto e = editor()) {
            e->columns = 8 << width->currentIndex();
            e->refresh();
        }
    });
    restoreGeometry(QSettings().value("geometry").toByteArray());
    restoreState(QSettings().value("layout").toByteArray());
    update();
}
void Window::record(QString message) {
    QString entry = QDateTime::currentDateTime().toString("MM-dd HH:mm:ss") + "   " + message;
    log->appendPlainText(entry);
    auto history = QSettings().value("journal").toStringList();
    history << entry;
    while (history.size() > 100)
        history.removeFirst();
    QSettings().setValue("journal", history);
}
void Window::trackRecent(const QString &path) {
    auto list = QSettings().value("recent").toStringList();
    list.removeAll(path);
    list.prepend(path);
    QSettings().setValue("recent", list.mid(0, 12));
    recentMenu();
}
void Window::recentMenu() {
    recent->clear();
    for (auto path : QSettings().value("recent").toStringList())
        recent->addAction(path, this, [this, path] { open(path); });
}
bool Window::open(const QString &path) {
    auto canonical = QFileInfo(path).canonicalFilePath();
    for (int i = 0; i < tabs->count(); ++i)
        if (static_cast<Editor *>(tabs->widget(i))->doc.path == canonical) {
            tabs->setCurrentIndex(i);
            return true;
        }
    auto e = new Editor;
    if (!e->doc.open(path)) {
        QMessageBox::warning(this, "Open", e->doc.error);
        delete e;
        return false;
    }
    e->columns = 8 << width->currentIndex();
    e->doc.history.setUndoLimit(256);
    connect(e, &Editor::changed, this, [this, e] {
        if (editor() == e)
            update();
    });
    connect(e, &Editor::notice, this, [this](QString m) { statusBar()->showMessage(m, 4000); });
    connect(&e->doc.history, &QUndoStack::indexChanged, this, [this, e] {
        if (editor() == e) {
            resetResults();
            refreshTree();
            update();
        }
    });
    auto idx = tabs->addTab(e, QFileInfo(path).fileName());
    tabs->setCurrentIndex(idx);
    tabs->setTabToolTip(idx, canonical);
    trackRecent(canonical);
    record("Opened " + canonical);
    e->refresh();
    e->setFocus();
    return true;
}
void Window::refreshTree() {
    ++treeRevision;
    tree->clear();
    treeTimer.stop();
    if (editor())
        treeTimer.start(80);
}
void Window::update() {
    auto e = editor();
    inspector->clear();
    writeAction->setEnabled(e);
    insertAction->setEnabled(e && e->doc.writable);
    writeAction->setChecked(e && e->doc.writable);
    insertAction->setChecked(e && e->insertMode);
    if (!e) {
        location->setText("Open a binary file or drop it here");
        mode->clear();
        return;
    }
    width->blockSignals(true);
    width->setCurrentIndex(e->columns == 8 ? 0 : e->columns == 32 ? 2 : 1);
    width->blockSignals(false);
    int base = radix->currentIndex() == 1 ? 16 : radix->currentIndex() == 2 ? 2 : 10;
    for (auto row : inspect(e->doc.read(e->cursor, 8), endian->currentIndex() == 0, base))
        new QTreeWidgetItem(inspector, row.split('\t'));
    tabs->setTabText(tabs->currentIndex(),
                     QFileInfo(e->doc.path).fileName() + (e->doc.history.isClean() ? "" : " *"));
    location->setText(QString("Offset  %1      Selection  %2 B      File  %3 B")
                          .arg(hex(e->cursor))
                          .arg(e->length())
                          .arg(e->doc.size));
    mode->setText(e->doc.writable ? (e->insertMode ? "INSERT" : "OVERWRITE") : "READ ONLY");
}
bool Window::save(bool as) {
    auto e = editor();
    if (!e)
        return false;
    if (!e->doc.writable) {
        statusBar()->showMessage("Enable Edit mode to save", 4000);
        return false;
    }
    QString path =
        as ? QFileDialog::getSaveFileName(this, "Save binary as", e->doc.path) : e->doc.path;
    if (path.isEmpty())
        return false;
    for (int i = 0; i < tabs->count(); ++i) {
        auto other = static_cast<Editor *>(tabs->widget(i));
        if (other != e && other->doc.path == QFileInfo(path).canonicalFilePath()) {
            QMessageBox::warning(this, "Save", "Destination is open in another tab");
            return false;
        }
    }
    if (!e->doc.save(path)) {
        QMessageBox::warning(this, "Save", e->doc.error);
        return false;
    }
    e->differences.clear();
    resetResults();
    e->refresh();
    refreshTree();
    tabs->setTabToolTip(tabs->currentIndex(), e->doc.path);
    trackRecent(e->doc.path);
    record("Saved " + e->doc.path);
    return true;
}
bool Window::closeTab(int i) {
    if (i < 0)
        return true;
    auto e = static_cast<Editor *>(tabs->widget(i));
    if (!e->doc.history.isClean()) {
        tabs->setCurrentIndex(i);
        auto choice =
            QMessageBox::question(this, "Unsaved edits", "Save changes to " + e->doc.path + "?",
                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (choice == QMessageBox::Cancel)
            return false;
        if (choice == QMessageBox::Save) {
            e->doc.writable = true;
            if (!save(false))
                return false;
        }
    }
    tabs->removeTab(i);
    e->deleteLater();
    return true;
}
void Window::closeEvent(QCloseEvent *e) {
    while (tabs->count())
        if (!closeTab(tabs->currentIndex())) {
            e->ignore();
            return;
        }
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("layout", saveState());
    s.setValue("width", width->currentIndex());
    s.setValue("endian", endian->currentIndex());
    s.setValue("radix", radix->currentIndex());
    e->accept();
}
void Window::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *e) {
    for (auto url : e->mimeData()->urls())
        if (url.isLocalFile())
            open(url.toLocalFile());
}
void Window::resetResults() {
    if (stop)
        stop->store(true);
    stop.reset();
    comparisonReady = false;
    comparePaths.clear();
    hits->clear();
    statusBar()->clearMessage();
    if (editor())
        editor()->differences.clear();
    hist->report = {};
    entropyPlot->report = {};
    hist->update();
    entropyPlot->update();
    summary->setText("Run Analyze file to calculate byte statistics.");
}
void Window::run(Operation op, bool selection, QByteArray query, Snapshot other) {
    auto e = editor();
    if (!e)
        return;
    resetResults();
    auto token = std::make_shared<std::atomic_bool>(false);
    stop = token;
    Snapshot s = e->doc;
    auto at = selection ? e->first() : 0, n = selection ? e->length() : s.size;
    statusBar()->showMessage("Analyzing…");
    auto watcher = new QFutureWatcher<Report>(this);
    connect(watcher, &QFutureWatcher<Report>::finished, this, [this, watcher, op, token, e] {
        auto r = watcher->result();
        watcher->deleteLater();
        if (stop != token || editor() != e)
            return;
        statusBar()->clearMessage();
        if (!r.error.isEmpty()) {
            record(r.error);
            return;
        }
        if (op == Operation::Statistics) {
            summary->setText(QString("%1 bytes   ·   %2 bits/byte   ·   SHA-256   %3")
                                 .arg(r.count)
                                 .arg(r.entropy, 0, 'f', 3)
                                 .arg(r.hash));
            hist->report = r;
            entropyPlot->report = r;
            hist->update();
            entropyPlot->update();
            bottom->setCurrentIndex(0);
        } else {
            for (auto &f : r.hits) {
                auto item =
                    new QTreeWidgetItem(hits, {hex(f.offset), QString::number(f.length), f.text});
                item->setData(0, Qt::UserRole, f.offset);
                item->setData(1, Qt::UserRole, f.length);
                if (op == Operation::Compare)
                    e->differences << qMakePair(f.offset, f.offset + f.length);
            }
            comparisonReady = op == Operation::Compare;
            bottom->setCurrentIndex(1);
            e->viewport()->update();
            statusBar()->showMessage(QString("%1 result(s)%2")
                                         .arg(r.hits.size())
                                         .arg(r.limited ? " · first 4096 shown" : ""),
                                     8000);
        }
        record(QString("Analysis complete · %1%2")
                   .arg(QFileInfo(e->doc.path).fileName(),
                        r.limited ? " · results limited to 4096" : ""));
    });
    watcher->setFuture(
        QtConcurrent::run([=] { return analyze(s, at, n, op, query, other, token.get()); }));
}
void Window::find() {
    if (!editor())
        return;
    QDialog dlg(this);
    dlg.setWindowTitle("Find data");
    QFormLayout layout(&dlg);
    QComboBox kind;
    kind.addItems({"Hex bytes", "UTF-8 text"});
    QLineEdit term;
    layout.addRow("Search as", &kind);
    layout.addRow("Pattern", &term);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addRow(&buttons);
    connect(&buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(&buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    term.setFocus();
    if (dlg.exec() != QDialog::Accepted)
        return;
    auto t = term.text();
    QByteArray data;
    if (kind.currentIndex() == 0) {
        t.remove(QRegularExpression("\\s+"));
        if (t.size() % 2 || t.contains(QRegularExpression("[^a-fA-F0-9]"))) {
            statusBar()->showMessage("Invalid hex pattern", 4000);
            return;
        }
        data = QByteArray::fromHex(t.toLatin1());
    } else
        data = t.toUtf8();
    if (!data.isEmpty())
        run(Operation::Search, false, data);
}
void Window::compare() {
    if (!editor())
        return;
    auto p = QFileDialog::getOpenFileName(this, "Compare with binary");
    if (p.isEmpty())
        return;
    Document other;
    if (!other.open(p)) {
        QMessageBox::warning(this, "Compare", other.error);
        return;
    }
    run(Operation::Compare, false, {}, other);
    comparePaths = {editor()->doc.path, other.path};
}
void Window::extract() {
    auto e = editor();
    if (!e || !e->length())
        return;
    auto p = QFileDialog::getSaveFileName(this, "Extract selected bytes");
    if (p.isEmpty())
        return;
    for (int i = 0; i < tabs->count(); ++i)
        if (QFileInfo(p).canonicalFilePath() == static_cast<Editor *>(tabs->widget(i))->doc.path) {
            QMessageBox::warning(this, "Extract",
                                 "Choose a destination outside the open documents");
            return;
        }
    if (!e->doc.extract(p, e->first(), e->length()))
        QMessageBox::warning(this, "Extract", e->doc.error);
    else
        record("Extracted " + p);
}
void Window::exportDiff() {
    if (!comparisonReady) {
        statusBar()->showMessage("Run Compare and open Results first", 4000);
        return;
    }
    auto p = QFileDialog::getSaveFileName(this, "Export displayed differences", {}, "CSV (*.csv)");
    if (p.isEmpty())
        return;
    auto dest = QFileInfo(p).canonicalFilePath();
    for (int i = 0; i < tabs->count(); ++i)
        if (dest == static_cast<Editor *>(tabs->widget(i))->doc.path)
            return;
    if (comparePaths.contains(dest))
        return;
    QSaveFile file(p);
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream out(&file);
        out << "offset,length\n";
        for (int i = 0; i < hits->topLevelItemCount(); ++i) {
            auto item = hits->topLevelItem(i);
            out << item->text(0) << ',' << item->text(1) << '\n';
        }
        out.flush();
        if (out.status() == QTextStream::Ok && file.commit()) {
            record("Exported " + p);
            return;
        }
    }
    QMessageBox::warning(this, "Export", file.errorString());
}
