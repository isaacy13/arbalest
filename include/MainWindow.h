#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPushButton>
#include <QtWidgets/QMdiArea>
#include <unordered_map>
#include <QtWidgets/QLabel>
#include "Document.h"
#include "Dockable.h"
#include "Properties.h"
#include "QSSPreprocessor.h"
#include <QStatusBar>
#include <QMenuBar>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
private:
    Ui::MainWindow *ui;
    Dockable *objectTreeDockable;
    Dockable *objectPropertiesDockable;
    Dockable *toolboxDockable;
    QStatusBar *statusBar;
    QMenuBar* menuTitleBar;
    QTabWidget *documentArea;
    // Stores pointers to all the currently opened documents. Item removed when document is closed. Key is documents ID.
    std::unordered_map<int, Document*> documents;

    // Total number of documents ever opened in application's life time. This is not decreased when closing documents.
    // A document's ID is set to documentsCount at the moment of opening it.
    int documentsCount = 0;

    // The ID of the active document.
    int activeDocumentId = -1;

    void openFileDialog();
    void saveFileDialog();
    void openFile(const QString& filePath);


    void minimizeButtonPressed();
    void maximizeButtonPressed();

    QPushButton * maximizeButton;
    QLabel *statusBarPathLabel;

    void prepareDockables();

protected:

    void prepareUi();
    void changeEvent(QEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    void onActiveDocumentChanged(int newIndex);
    void closeButtonPressed();
    void tabCloseRequested(int i);
    void objectTreeSelectionChanged(QString fullPath);

};
#endif // MAINWINDOW_H
