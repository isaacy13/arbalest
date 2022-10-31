//
// Created by isaacy13 on 09/28/2022.
//

#ifndef VVWIDGET_H
#define VVWIDGET_H

#include <ged.h>
#include <bu.h>
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

#define RESULT_CODE_COLUMN  0
#define TEST_NAME_COLUMN    1
#define DESCRIPTION_COLUMN  2
#define OBJPATH_COLUMN      3

class Document;
class VerificationValidationWidget : public QHBoxWidget {
    Q_OBJECT
public:
    explicit VerificationValidationWidget(Document* document, QWidget* parent = nullptr);
    ~VerificationValidationWidget();
    void showSelectTests();
    void showNewTestDialog();
    void showNewTestSuiteDialog();
    void showRemoveTestSuiteDialog();
    void showRemoveTestDialog();
    QString* runTest(const QString& cmd);
    //void runTests();
    void setStatusBar(QStatusBar* statusBar) { this->statusBar = statusBar; }
    //QDialog* getDialog() {return selectTestsDialog;};

public slots:
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

private:
    // widget-specific data
    Document* document;
    QString modelID;
    QString dbName;
    QString dbConnectionName; 
    QStringList selectedTests;

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

    // init functions
    void dbConnect();
    void dbInitTables();
    void dbPopulateDefaults();
    void setupUI();
    void SetupNewTestUI();
    void SetupNewTestSuiteUI();
    void SetupRemoveTestSuiteUI();
    void SetupRemoveTestUI();
    
    // database functions
    QSqlQuery* dbExec(QString command, bool showErrorPopup = true);
    void dbExec(QSqlQuery*& query, bool showErrorPopup = true);
    QSqlDatabase getDatabase() const {
        return QSqlDatabase::database(dbConnectionName);
    }

    // events
    void resizeEvent(QResizeEvent* event) override;

    // ui stuff
    void showResult(const QString& testResultID);

    // Other
    void checkSuiteSA();
    void checkTestSA();
};

#endif // VVWIDGET_H
