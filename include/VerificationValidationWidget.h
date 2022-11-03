//
// Created by isaacy13 on 09/28/2022.
//

#ifndef VVWIDGET_H
#define VVWIDGET_H

#include <iostream>
#include <QTableView>
#include <QtSql/QSqlTableModel>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QtWidgets>
#include <QMessageBox>
#include <QHBoxWidget.h>
#include "VerificationValidation.h"
#include "Utils.h"
#include <vector>
#include "map"

#define RESULT_CODE_COLUMN 0
#define TEST_NAME_COLUMN 1
#define DESCRIPTION_COLUMN 2
#define OBJPATH_COLUMN 3

#define NO_SELECTION -1
#define OPEN 0
#define DISCARD 1
#define CANCEL 2

class MainWindow;
class Document;

class TestItem : public QListWidgetItem {
public:
    QListWidgetItem* testWidgetItem;
    int id;
    QString testName;
    QString testCommand;
    bool hasValArgs;
    QString category;

    TestItem(QListWidgetItem* _testWidgetItem, int id, QString _testName, QString testCommand, bool _hasValArgs, QString _category);    
};

class Dockable;
class VerificationValidationWidget : public QHBoxWidget
{

    Q_OBJECT
public:
    explicit VerificationValidationWidget(MainWindow *mainWindow, Document *document, QWidget *parent = nullptr);
    ~VerificationValidationWidget();
    void showSelectTests();
    void showNewTestDialog();
    void showNewTestSuiteDialog();
    void showRemoveTestSuiteDialog();
    void showRemoveTestDialog();
    //QString* runTest(const QString& cmd);
    //void runTests();
    void setStatusBar(QStatusBar* statusBar) { this->statusBar = statusBar; }
    //QDialog* getDialog() {return selectTestsDialog;};
    QString getDBConnectionName() const { return dbConnectionName; }

public slots:
    QString *runTest(const QString &cmd);
    void runTests();

private slots:
	void updateSuiteSelectAll(QListWidgetItem*);
	void updateTestSelectAll(QListWidgetItem*);
	void updateTestListWidget(QListWidgetItem*);
    void testListSelection(QListWidgetItem*);
    void setupDetailedResult(int row, int  column);
    void searchTests(const QString &input);
    void userInputDialogUI(QListWidgetItem*);
    void updateSelectedTestList(QListWidgetItem*);
    void updateSelectedSuiteList(QListWidgetItem*);
    void updateDbwithNewSuite();
    void searchSuites(const QString &input);
    void updateDbwithRemovedSuite();
    void updateDbwithRemovedTest();
    void updateDbwithNewTest();
    // void updateVarArgs(QString testName, int index, QString input);
    void updateVarArgs();

private:
    MainWindow *mainWindow;
    Dockable *parentDockable;
    int msgBoxRes;
    QString folderName;

    // widget-specific data
    Document *document;
    QString modelID;
    QString dbFilePath;
    QString dbName;
    QString dbConnectionName; 
    QStringList selectedTests;
    QVector<QGroupBox*> groupBoxVector;

    // user interface data
    QTableWidget* resultTable;
    QListWidget* testList;
    QListWidget* testListSuiteCreation;
    QListWidget* suiteList;
    QListWidget* test_sa;
    QListWidget* suite_sa;
    QLineEdit* searchBox;
    QLineEdit* newSuiteNameBox;
    QDialog* selectTestsDialog;
    QDialog* newTestSuiteDialog;
    QDialog* removeTestSuiteDialog;
    QDialog* removeTestDialog;
    QDialog* newTestsDialog;
    QStatusBar* statusBar;
    QGroupBox* groupbox1;
    QGroupBox* newTestInfoGroupbox;
    std::map<QListWidgetItem*, TestItem> testItemMap;
    std::map<int, QListWidgetItem*> testIdMap;

    // init functions
    void dbConnect(QString dbFilePath);
    void dbInitTables();
    void dbPopulateDefaults();
    void setupUI();
    void SetupNewTestUI();
    void SetupNewTestSuiteUI();
    void SetupRemoveTestSuiteUI();
    void SetupRemoveTestUI();
    
    // database functions
    QSqlQuery *dbExec(QString command, bool showErrorPopup = true);
    void dbExec(QSqlQuery *&query, bool showErrorPopup = true);
    void dbClose() {
        { 
            QSqlDatabase db = getDatabase();
            if (db.isOpen()) db.close();
        }
        QSqlDatabase::removeDatabase(dbConnectionName);
    }

    QSqlDatabase getDatabase() const
    {
        return QSqlDatabase::database(dbConnectionName, false);
    }

    bool dbIsAlive(QSqlDatabase db) {
        if (!db.isOpen()) return false;
        QSqlQuery q("SELECT 1 FROM Tests", db);
        if (!q.isActive()) return false;
        return true;
    }

    void dbClearResults();

    // events
    void resizeEvent(QResizeEvent *event) override;

    // ui stuff
    void showResult(const QString &testResultID);
    void showAllResults();

    // Other
    void checkSuiteSA();
    void checkTestSA();
    QString constructTestCommand(TestItem item);
};

#endif // VVWIDGET_H
