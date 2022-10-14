#include "VerificationValidationWidget.h"
#include <Document.h>

#define SHOW_ERROR_POPUP true
// TODO: if checksum doesn't match current test file, notify user

VerificationValidationWidget::VerificationValidationWidget(Document* document, QWidget* parent) : document(document), testList(new QListWidget()), resultTable(new QTableWidget()), selectTestsDialog(new QDialog()), statusBar(nullptr) {
    dbConnect();
    dbInitTables();
    dbPopulateDefaults();
    setupUI();
}

VerificationValidationWidget::~VerificationValidationWidget() {
    QSqlDatabase::removeDatabase(dbConnectionName);
}


void VerificationValidationWidget::showSelectTests() {
    statusBar->showMessage("Select tests to run...");
    selectTestsDialog->exec();
}

void VerificationValidationWidget::runTests() {
    statusBar->showMessage("Finished running 0/XXX tests");
    // TODO: run tests
    // TODO: update statusBar to tell how many tests finished
    // TODO: update GUI to show results of test
}

void VerificationValidationWidget::dbConnect() {
    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        throw std::runtime_error("[Verification & Validation] ERROR: sqlite is not available");
        return;
    }

    dbName = "tmpfile.sqlite"; 
    if (document->getFilePath()) dbName = document->getFilePath()->split("/").last() + ".sqlite";
    dbConnectionName = dbName + "-connection";

    // check if SQL connection already open
    QSqlDatabase db = QSqlDatabase::database(dbConnectionName, false);
    // TODO: instead of throwing + popping up error, open correct document
    if (db.isOpen())
        throw std::runtime_error("[Verification & Validation] ERROR: SQL connection already exists");
    
    //// TODO: opening multiple new files crashes
    //// TODO: whenever user saves, sqlite file name should be updated from tmpfile.sqlite to <newfilename>.sqlite
    db = QSqlDatabase::addDatabase("QSQLITE", dbConnectionName);
    db.setDatabaseName(dbName);

    if (!db.open() || !db.isOpen())
        throw std::runtime_error("[Verification & Validation] ERROR: db failed to open: " + db.lastError().text().toStdString());
}

void VerificationValidationWidget::dbInitTables() {
    if (!getDatabase().tables().contains("Model"))
        dbExec("CREATE TABLE Model (id INTEGER PRIMARY KEY, filepath TEXT NOT NULL UNIQUE, sha256Checksum TEXT NOT NULL)");
    if (!getDatabase().tables().contains("Tests"))
        dbExec("CREATE TABLE Tests (id INTEGER PRIMARY KEY, testName TEXT NOT NULL, testCommand TEXT NOT NULL UNIQUE)");
    if (!getDatabase().tables().contains("TestResults"))
        dbExec("CREATE TABLE TestResults (id INTEGER PRIMARY KEY, modelID INTEGER NOT NULL, testID INTEGER NOT NULL, resultCode TEXT, terminalOutput TEXT)");
    if (!getDatabase().tables().contains("Issues"))
        dbExec("CREATE TABLE Issues (id INTEGER PRIMARY KEY, testID INTEGER NOT NULL, objectIssueID INTEGER NOT NULL)");
    if (!getDatabase().tables().contains("ObjectIssue"))
        dbExec("CREATE TABLE ObjectIssue (id INTEGER PRIMARY KEY, objectName TEXT NOT NULL, issueDescription TEXT NOT NULL)");
    if (!getDatabase().tables().contains("TestsSuites"))
        dbExec("CREATE TABLE TestsSuites (id INTEGER PRIMARY KEY, suiteName TEXT NOT NULL)");
    if (!getDatabase().tables().contains("TestsInSuite"))
        dbExec("CREATE TABLE TestsInSuite (id INTEGER PRIMARY KEY, testSuiteID INTEGER NOT NULL, testID INTEGER NOT NULL)");
}

void VerificationValidationWidget::dbPopulateDefaults() {
    QSqlQuery* qResult;

    // if Model table empty, assume new db and insert model info
    QString cmd = "SELECT id FROM Model WHERE filePath='" + dbName + "'";
    qResult = dbExec(cmd, !SHOW_ERROR_POPUP);
    if (!qResult->next()) {
        cmd = "INSERT INTO Model (filepath, sha256Checksum) VALUES ('" + dbName + "', '" + "TODO: HASH SHA256 FROM OPENSSL" + "')";
        dbExec(cmd);
    }

    // if Tests table empty, new db and insert tests
    // note: this doesn't repopulate deleted tests, unless all tests deleted
    qResult = dbExec("SELECT id FROM Tests", !SHOW_ERROR_POPUP);
    if (!qResult->next()) {
        for (int i = 0; i < defaultTests.size(); i++) {
            cmd = "INSERT INTO Tests (testName, testCommand) VALUES ('" + defaultTests[i].testName + "', '" + defaultTests[i].testCommand + "')";
            qResult = dbExec(cmd);

            QString testID = qResult->lastInsertId().toString();

            cmd = "INSERT INTO TestsSuites (suiteName) VALUES ('" + defaultTests[i].suiteName + "')";
            qResult = dbExec(cmd);

            QString testSuiteID = qResult->lastInsertId().toString();

            cmd = "INSERT INTO TestsInSuite (testID, testSuiteID) VALUES (" + testID + "," + testSuiteID + ")";
            dbExec(cmd);
        }
    }
}

void VerificationValidationWidget::setupUI() {
    // setup result table's column headers
    QStringList columnLabels;
    columnLabels << "   " << "   " << "Test Name" << "Description" << "Object Path";
    resultTable->setColumnCount(columnLabels.size());
    resultTable->setHorizontalHeaderLabels(columnLabels);
    resultTable->verticalHeader()->setVisible(false);
    resultTable->horizontalHeader()->setStretchLastSection(true);
    resultTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    addWidget(resultTable);

    // populate checkbox list with tests
    QStringList tests;
    tests << "test 1" << "test 2" << "test 3" << "test 4";
    testList->addItems(tests);

    QListWidgetItem* item = 0;
    for (int i = 0; i < testList->count(); i++) {
        item = testList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
    }

    // format and populate Select Tests dialog box
    selectTestsDialog->setModal(true);
    selectTestsDialog->setWindowTitle("Select Tests");
    selectTestsDialog->setLayout(new QVBoxLayout);
    selectTestsDialog->layout()->addWidget(testList);

    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    selectTestsDialog->layout()->addWidget(buttonOptions);
    connect(buttonOptions, &QDialogButtonBox::accepted, selectTestsDialog, &QDialog::accept);
    connect(buttonOptions, &QDialogButtonBox::rejected, selectTestsDialog, &QDialog::reject);
}

QSqlQuery* VerificationValidationWidget::dbExec(QString command, bool showErrorPopup) {
    QSqlQuery* query = new QSqlQuery(command, getDatabase());
    if (showErrorPopup && !query->isActive())
        popup("[Verification & Validation] ERROR: query failed to execute: " + query->lastError().text());
    return query;
}

void VerificationValidationWidget::resizeEvent(QResizeEvent* event) {
    resultTable->setColumnWidth(0, this->width() * 0.025);
    resultTable->setColumnWidth(1, this->width() * 0.025);
    resultTable->setColumnWidth(2, this->width() * 0.10);
    resultTable->setColumnWidth(3, this->width() * 0.60);
    resultTable->setColumnWidth(4, this->width() * 0.25);

    QHBoxWidget::resizeEvent(event);
}