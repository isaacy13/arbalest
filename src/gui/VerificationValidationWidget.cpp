#include "VerificationValidationWidget.h"
#include <Document.h>
#include "MainWindow.h"
using Result = VerificationValidation::Result;
using DefaultTests = VerificationValidation::DefaultTests;
using Parser = VerificationValidation::Parser;

#define SHOW_ERROR_POPUP true

TestItem::TestItem(QListWidgetItem* _testWidgetItem, int _id, QString _testName, QString _testCommand, bool _hasValArgs, QString _category){
    this->testWidgetItem = _testWidgetItem;
    this->id = _id;
    this->testName = _testName;
    this->testCommand = _testCommand;
    this->hasValArgs = _hasValArgs;
    this->category = _category;
}

// TODO: if checksum doesn't match current test file, notify user

VerificationValidationWidget::VerificationValidationWidget(MainWindow* mainWindow, Document* document, QWidget* parent) : 
document(document), statusBar(nullptr), mainWindow(mainWindow), parentDockable(mainWindow->getVerificationValidationDockable()),
testList(new QListWidget()), resultTable(new QTableWidget()), selectTestsDialog(new QDialog()),
suiteList(new QListWidget()), test_sa(new QListWidget()), suite_sa(new QListWidget()),
msgBoxRes(NO_SELECTION), folderName("atr"), dbConnectionName(""),
dbFilePath(folderName + "/untitled" + QString::number(document->getDocumentId()) + ".atr")
{
    if (!dbConnectionName.isEmpty()) return;
    if (!QDir(folderName).exists() && !QDir().mkdir(folderName)) popup("Failed to create " + folderName + " folder");
    
    try { dbConnect(dbFilePath); } catch (const std::runtime_error& e) { throw e; }
    dbInitTables();
    dbPopulateDefaults();

    if (msgBoxRes == OPEN) {
        showAllResults();
        msgBoxRes = NO_SELECTION;
    } else if (msgBoxRes == DISCARD) {
        dbClearResults();
        resultTable->setRowCount(0);
    }
}

VerificationValidationWidget::~VerificationValidationWidget() {
    dbClose();
}

void VerificationValidationWidget::showNewTestDialog() {
    statusBar->showMessage("Create new test...");
    SetupNewTestUI();
}

void VerificationValidationWidget::showRemoveTestSuiteDialog() {
    statusBar->showMessage("Remove test suite...");
    SetupRemoveTestSuiteUI();
}

void VerificationValidationWidget::showRemoveTestDialog() {
    statusBar->showMessage("Remove test...");
    SetupRemoveTestUI();
}

void VerificationValidationWidget::showNewTestSuiteDialog() {
    statusBar->showMessage("Create new test suite...");
    SetupNewTestSuiteUI();
}

void VerificationValidationWidget::showSelectTests() {
    statusBar->showMessage("Select tests to run...");
    setupUI();
    selectTestsDialog->exec();
    //connect(selectTestsDialog, SIGNAL(accepted()), this, SLOT(runTests()));
}

QString VerificationValidationWidget::constructTestCommand(TestItem item){
    QString cmd = item.testCommand;
    QSqlQuery* q = new QSqlQuery(getDatabase());
    
    q->prepare("Select arg, defaultVal from TestArg Where testID = :id ORDER by argIdx");
    q->bindValue(":id", item.id);
    dbExec(q);

    while(q->next()){
        cmd = cmd + " " + q->value(0).toString();
        if(q->value(1).toString() != NULL){
            cmd = cmd + " " + q->value(1).toString();
        }
    }
    
    return cmd;
}

QString* VerificationValidationWidget::runTest(const QString& cmd) {
    QString filepath = *(document->getFilePath());
    struct ged* dbp = mgedRun(cmd, filepath);
    QString* result = new QString(bu_vls_addr(dbp->ged_result_str));
    ged_close(dbp);

    return result;
}

void VerificationValidationWidget::runTests() {
    dbClearResults();
    resultTable->setRowCount(0);

    // Get list of checked tests
    QList<QListWidgetItem *> selected_tests;
    QListWidgetItem* item = 0;
    for (int i = 0; i < testList->count(); i++) {
		item = testList->item(i);
        if(item->checkState()){
            selected_tests.push_back(item);
        }
    }

    // Run tests
    int totalTests = selected_tests.count();
    if(totalTests ==  0){
        return;
    }

    QString status = "Finished running %1 / %2 tests";
    for(int i = 0; i < totalTests; i++){
        statusBar->showMessage(status.arg(i+1).arg(totalTests));
        int testID = testList->row(selected_tests[i]) + 1;
        QString testCommand = selected_tests[i]->toolTip();
        const QString* terminalOutput = runTest(testCommand);
                
        QString executableName = testCommand.split(' ').first();
        Result* result = nullptr;
        // find proper parser
        if (QString::compare(executableName, "search", Qt::CaseInsensitive) == 0)
            result = Parser::search(testCommand, terminalOutput);

        // if parser hasn't been implemented, default
        if (!result) {
            result = new Result;
            result->resultCode = Result::Code::UNPARSEABLE;
        }

        QString resultCode = QString::number(result->resultCode);
        
        // insert results into db
        QSqlQuery* q2 = new QSqlQuery(getDatabase());
        q2->prepare("INSERT INTO TestResults (modelID, testID, resultCode, terminalOutput) VALUES (?,?,?,?)");
        q2->addBindValue(modelID);
        q2->addBindValue(testID);
        q2->addBindValue(resultCode);
        q2->addBindValue((terminalOutput) ? *terminalOutput : "");
        dbExec(q2);

        QString testResultID = q2->lastInsertId().toString();

        // insert issues into db
        for (Result::ObjectIssue currentIssue : result->issues) {
            q2 = new QSqlQuery(getDatabase());
            q2->prepare("INSERT INTO ObjectIssue (objectName, issueDescription) VALUES (?,?)");
            q2->addBindValue(currentIssue.objectName);
            q2->addBindValue(currentIssue.issueDescription);
            dbExec(q2);

            QString objectIssueID = q2->lastInsertId().toString();
            q2 = new QSqlQuery(getDatabase());
            q2->prepare("INSERT INTO Issues (testResultID, objectIssueID) VALUES (?,?)");
            q2->addBindValue(testResultID);
            q2->addBindValue(objectIssueID);
            dbExec(q2);
        }

        showResult(testResultID);
    }

    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT md5Checksum, filePath FROM Model WHERE id = ?");
    q->addBindValue(modelID);
    dbExec(q);
    if (!q->next()) {
        popup("Failed to show modelID " + modelID);
        return;
    }

    QString md5 = q->value(0).toString();
    QString filePath = q->value(1).toString();
    delete q;
    QString dockableTitle = "Verification & Validation -- File Path: "+filePath+",    MD5: "+md5+",    Model ID: "+modelID;
    QLabel *title = new QLabel(dockableTitle);
    title->setObjectName("dockableHeader");
    parentDockable->setTitleBarWidget(title);
}

void VerificationValidationWidget::dbConnect(const QString dbFilePath) {
    if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
        throw std::runtime_error("[Verification & Validation] ERROR: sqlite is not available");

    this->dbName = dbFilePath;
    QString* fp = document->getFilePath();
    if (fp) {
        QStringList fpList = fp->split("/");
        this->dbName = folderName + "/" + fpList.last() + ".atr";
        this->dbFilePath = QDir(this->dbName).absolutePath();
    }

    dbConnectionName = this->dbName + "-connection";
    QSqlDatabase db = getDatabase();

    // if SQL connection already open, just switch to that tab
    if (dbIsAlive(db)) {
        const std::unordered_map<int, Document*>* documents = mainWindow->getDocuments();
        Document* correctDocument = nullptr;
        Document* doc;
        for (auto it = documents->begin(); it != documents->end(); it++) {
            doc = it->second;
            if (doc && doc != this->document && doc->getVerificationValidationWidget()->getDBConnectionName() == dbConnectionName) {
                correctDocument = doc;
                break;
            }
        }
        if (correctDocument) mainWindow->getDocumentArea()->setCurrentIndex(correctDocument->getTabIndex());
        throw std::runtime_error("");
    }

    // if file exists, prompt before overwriting
    if (QFile::exists(this->dbName)) {
        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("Detected existing test results in " + this->dbName + ".\nDo you want to open or discard the results?");
        msgBox.setInformativeText("Changes cannot be reverted.");
        msgBox.setStandardButtons(QMessageBox::Open | QMessageBox::Cancel);
        QPushButton* discardButton = msgBox.addButton("Discard", QMessageBox::DestructiveRole);
        discardButton->setIcon(QIcon(":/icons/warning.png"));
        msgBox.setDefaultButton(QMessageBox::Cancel);

        int res = msgBox.exec();
        if (res == QMessageBox::Open) {
            msgBoxRes = OPEN;
            parentDockable->setVisible(true);
        }
        else if (msgBox.clickedButton() == discardButton) { 
            msgBoxRes = DISCARD;
        }
        else {
            msgBoxRes = CANCEL;
            throw std::runtime_error("No changes were made.");
        }
    }

    db = QSqlDatabase::addDatabase("QSQLITE", dbConnectionName);
    db.setDatabaseName(this->dbName);

    if (!db.open() || !db.isOpen())
        throw std::runtime_error("[Verification & Validation] ERROR: db failed to open: " + db.lastError().text().toStdString());
}

void VerificationValidationWidget::dbInitTables() {
    if (!getDatabase().tables().contains("Model"))
        delete dbExec("CREATE TABLE Model (id INTEGER PRIMARY KEY, filepath TEXT NOT NULL UNIQUE, md5Checksum TEXT NOT NULL)");
    if (!getDatabase().tables().contains("Tests"))
        dbExec("CREATE TABLE Tests (id INTEGER PRIMARY KEY, testName TEXT NOT NULL, testCommand TEXT NOT NULL, hasValArgs BOOL NOT NULL, category TEXT NOT NULL)");
        // dbExec("CREATE TABLE Tests (id INTEGER PRIMARY KEY, testName TEXT NOT NULL, testCommand TEXT NOT NULL UNIQUE)");
    if (!getDatabase().tables().contains("TestResults"))
        delete dbExec("CREATE TABLE TestResults (id INTEGER PRIMARY KEY, modelID INTEGER NOT NULL, testID INTEGER NOT NULL, resultCode TEXT, terminalOutput TEXT)");
    if (!getDatabase().tables().contains("Issues"))
        delete dbExec("CREATE TABLE Issues (id INTEGER PRIMARY KEY, testResultID INTEGER NOT NULL, objectIssueID INTEGER NOT NULL)");
    if (!getDatabase().tables().contains("ObjectIssue"))
        delete dbExec("CREATE TABLE ObjectIssue (id INTEGER PRIMARY KEY, objectName TEXT NOT NULL, issueDescription TEXT NOT NULL)");
    if (!getDatabase().tables().contains("TestSuites"))
        delete dbExec("CREATE TABLE TestSuites (id INTEGER PRIMARY KEY, suiteName TEXT NOT NULL, UNIQUE(suiteName))");
    if (!getDatabase().tables().contains("TestsInSuite"))
        delete dbExec("CREATE TABLE TestsInSuite (id INTEGER PRIMARY KEY, testSuiteID INTEGER NOT NULL, testID INTEGER NOT NULL)");
    if (!getDatabase().tables().contains("TestArgs"))
        dbExec("CREATE TABLE TestArg (id INTEGER PRIMARY KEY, testID INTEGER NOT NULL, argIdx INTEGER NOT NULL, arg TEXT NOT NULL, isVarArg BOOL NOT NULL, defaultVal TEXT)");
}

void VerificationValidationWidget::dbPopulateDefaults() {
    QSqlQuery* q;
    QString md5Checksum = "TODO: HASH USING BRLCAD INTERFACE";

    // if Model table empty, assume new db and insert model info
    q = new QSqlQuery(getDatabase());
    q->prepare("SELECT COUNT(id) FROM Model WHERE filepath=?");
    q->addBindValue(QDir(*document->getFilePath()).absolutePath());
    dbExec(q, !SHOW_ERROR_POPUP);

    int numEntries = 0;
    if (q->next()) numEntries = q->value(0).toInt();

    if (!numEntries) {
        delete q;
        q = new QSqlQuery(getDatabase());
        q->prepare("INSERT INTO Model (filepath, md5Checksum) VALUES (?, ?)");
        q->addBindValue(QDir(*document->getFilePath()).absolutePath());
        q->addBindValue(md5Checksum);
        dbExec(q);
        modelID = q->lastInsertId().toString();
    } else {
        modelID = q->value(0).toString();
    }

    delete q;

    // if Tests table empty, new db and insert tests
    // note: this doesn't repopulate deleted tests, unless all tests deleted
    q = dbExec("SELECT id FROM Tests", !SHOW_ERROR_POPUP);
    if (!q->next()) {
        for (int i = 0; i < DefaultTests::allTests.size(); i++) {
            q->prepare("INSERT INTO Tests (testName, testCommand, hasValArgs, category) VALUES (:testName, :testCommand, :hasValArgs, :category)");
            q->bindValue(":testName", DefaultTests::allTests[i].testName);
            q->bindValue(":testCommand", DefaultTests::allTests[i].testCommand);
            q->bindValue(":hasValArgs", DefaultTests::allTests[i].hasVariable);
            q->bindValue(":category", DefaultTests::allTests[i].category);
            dbExec(q);

            QString testID = q->lastInsertId().toString();
			
            q->prepare("INSERT OR IGNORE INTO TestSuites VALUES (NULL, ?)");
            q->addBindValue(DefaultTests::allTests[i].suiteName);
            dbExec(q);

            for (int j = 0; j < DefaultTests::allTests[i].ArgList.size(); j++){
                q->prepare("INSERT INTO TestArg (testID, argIdx, arg, isVarArg, defaultVal) VALUES (:testID, :argIdx, :arg, :isVarArg, :defaultVal)");
                q->bindValue(":testID", testID);
                q->bindValue(":argIdx", j+1);
                q->bindValue(":arg", DefaultTests::allTests[i].ArgList[j].argument);
                q->bindValue(":isVarArg", DefaultTests::allTests[i].ArgList[j].isVariable);
                q->bindValue(":defaultVal", DefaultTests::allTests[i].ArgList[j].defaultValue);
                dbExec(q);
            } 
			
			q->prepare("SELECT id FROM TestSuites WHERE suiteName = ?");
            q->addBindValue(DefaultTests::allTests[i].suiteName);
            dbExec(q);
            QString testSuiteID;
			while (q->next()){
				testSuiteID = q->value(0).toString();
			}

            q->prepare("INSERT INTO TestsInSuite (testID, testSuiteID) VALUES (?, ?)");
            q->addBindValue(testID);
            q->addBindValue(testSuiteID);
            dbExec(q);
        }
    }

    delete q;
}

void VerificationValidationWidget::searchTests(const QString &input)  {
    // Hide category when search
    if(input.isNull()){
        QListWidgetItem* item = 0;
        for (int i = 0; i < testList->count(); i++) {
            item->setHidden(false);
        }
    } else {
        QList<QListWidgetItem *> tests = testList->findItems(input, Qt::MatchContains);
        QListWidgetItem* item = 0;
        for (int i = 0; i < testList->count(); i++) {
            item = testList->item(i);
            if(!tests.contains(item) || item->toolTip() == "category")
                item->setHidden(true);
            else
                item->setHidden(false);
        }
    }
}

void VerificationValidationWidget::searchSuites(const QString &input)  {
    QList<QListWidgetItem *> suites = suiteList->findItems(input, Qt::MatchContains);
    QListWidgetItem* item = 0;
    for (int i = 0; i < suiteList->count(); i++) {
		item = suiteList->item(i);
        if(!suites.contains(item))
            item->setHidden(true);
        else
            item->setHidden(false);
    }
}

void VerificationValidationWidget::updateSuiteSelectAll(QListWidgetItem* sa_option) {
    QListWidgetItem* item = 0;
    for (int i = 0; i < suiteList->count(); i++) {
		item = suiteList->item(i);
		if(sa_option->checkState()){
			item->setCheckState(Qt::Checked);
		} else {
			item->setCheckState(Qt::Unchecked);
		}
        updateTestListWidget(item);
	}
}

void VerificationValidationWidget::updateTestSelectAll(QListWidgetItem* sa_option) {
    QListWidgetItem* item = 0;
    for (int i = 0; i < testItemMap.size(); i++) {
        auto it = testItemMap.begin();
        std::advance(it, i);
        item = it->first;
		if(sa_option->checkState()){
			item->setCheckState(Qt::Checked);
		} else {
			item->setCheckState(Qt::Unchecked);
		}
	}

    if(sa_option->checkState()){
		suite_sa->item(0)->setCheckState(Qt::Checked);
	} else {
        suite_sa->item(0)->setCheckState(Qt::Unchecked);
    }
    
    updateSuiteSelectAll(suite_sa->item(0));
}

void VerificationValidationWidget::checkSuiteSA() {
    QListWidgetItem* item = 0;
    for (int i = 0; i < suiteList->count(); i++) {
        item = suiteList->item(i);
        if(!item->checkState()){
            return;
        }
    }
    suite_sa->item(0)->setCheckState(Qt::Checked);
}

void VerificationValidationWidget::checkTestSA() {
    // Check if all checked
    QListWidgetItem* item = 0;
    for (int i = 0; i < testItemMap.size(); i++) {
        auto it = testItemMap.begin();
        std::advance(it, i);
        item = it->first;
        if(!item->checkState()){
            return;
        }
    }
    test_sa->item(0)->setCheckState(Qt::Checked);
}

void VerificationValidationWidget::updateTestListWidget(QListWidgetItem* suite_clicked) {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("Select testID from TestsInSuite Where testSuiteID = (SELECT id FROM TestSuites WHERE suiteName = :suiteName)");
    q->bindValue(":suiteName", suite_clicked->text());
    dbExec(q);

    QListWidgetItem* item = 0;
    while(q->next()){
        int id = q->value(0).toInt();
        // item = testList->item(row);
        item = testIdMap.at(id);

        if(suite_clicked->checkState()){
            item->setCheckState(Qt::Checked);
        } else {
            item->setCheckState(Qt::Unchecked);
        }
        testListSelection(item);
    }

    if(!suite_clicked->checkState()){
        suite_sa->item(0)->setCheckState(Qt::Unchecked);
    }
    checkSuiteSA();
    delete q;
}

void VerificationValidationWidget::updateSelectedTestList(QListWidgetItem* test_clicked){
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT id FROM Tests WHERE testName = :testName");
    q->bindValue(":testName", test_clicked->text());
    dbExec(q, !SHOW_ERROR_POPUP);
    if (q->first()) {
        if(test_clicked->checkState()) {
            selectedTests << q->value(0).toString();
        }
        else {
            int index = selectedTests.indexOf(QRegExp(q->value(0).toString()));
            selectedTests.removeAt(index);
        }
    }
    
}

void VerificationValidationWidget::updateSelectedSuiteList(QListWidgetItem* test_clicked){
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT id FROM TestSuites WHERE suiteName = :suiteName");
    q->bindValue(":suiteName", test_clicked->text());
    dbExec(q, !SHOW_ERROR_POPUP);
    if (q->first()) {
        if(test_clicked->checkState()) {
            selectedTests << q->value(0).toString();
        }
        else {
            int index = selectedTests.indexOf(QRegExp(q->value(0).toString()));
            selectedTests.removeAt(index);
        }
    }
    
}

void VerificationValidationWidget::updateDbwithNewSuite() {
    QSqlQuery* q = new QSqlQuery(getDatabase());

    q->prepare("INSERT OR IGNORE INTO TestSuites VALUES (NULL, ?)");
    q->addBindValue(newSuiteNameBox->text());
    dbExec(q);  

    QString testSuiteID = q->lastInsertId().toString();

    for (QString testID : selectedTests) {
        q->prepare("INSERT INTO TestsInSuite (testSuiteID, testID) VALUES (?, ?)");
        q->addBindValue(testSuiteID);
        q->addBindValue(testID);
        dbExec(q);  
    }

    ///testList->clear();
    ///suiteList->clear();
    ///selectedTests.clear();
    newTestSuiteDialog->close();
}

void VerificationValidationWidget::updateDbwithRemovedSuite() {
    QSqlQuery* q = new QSqlQuery(getDatabase());

    for (QString suite : selectedTests)
    {
        q->prepare("DELETE FROM TestSuites WHERE id = :suiteID");
        q->bindValue(":suiteID", suite);
        dbExec(q,!SHOW_ERROR_POPUP); 

        q->prepare("DELETE FROM TestsInSuite WHERE testSuiteID = :suiteID");
        q->bindValue(":suiteID", suite);
        dbExec(q,!SHOW_ERROR_POPUP); 
    }

    removeTestSuiteDialog->close();
}

void VerificationValidationWidget::updateDbwithNewTest() {
    QSqlQuery* q = new QSqlQuery(getDatabase());

    bool isVariable = false;
    for (int i = 0; i < groupBoxVector.size(); i++) {
        QList<QCheckBox*> checkboxList = groupBoxVector.at(i)->findChildren<QCheckBox*>();
        if (checkboxList.at(0)->isChecked())
            isVariable = true;
    }

    QList<QLineEdit*> testInfoList = groupbox1->findChildren<QLineEdit*>();
    QString name = testInfoList.at(0)->text();
    QString command = testInfoList.at(1)->text();
    QString catagory = testInfoList.at(2)->text();

    q->prepare("INSERT INTO Tests (testName, testCommand, hasValArgs, category) VALUES (?, ?, ?, ?)");
    //q->addBindValue(DefaultTests::allTests[i].testName);
    //q->addBindValue(DefaultTests::allTests[i].testCommand);
    //dbExec(q);
    q->addBindValue(name);
    q->addBindValue(command);
    q->addBindValue(isVariable);
    q->addBindValue(catagory);
    dbExec(q); 

    QString testID = q->lastInsertId().toString();

    for(int i = 0; i < groupBoxVector.size(); i++)
    {
        QList<QLineEdit*> textList = groupBoxVector.at(i)->findChildren<QLineEdit*>();
        QString arg = textList.at(0)->text();

        if (!arg.isEmpty()) {
            QList<QLineEdit*> textList = groupBoxVector.at(i)->findChildren<QLineEdit*>();
            QList<QCheckBox*> boolList = groupBoxVector.at(i)->findChildren<QCheckBox*>();

            QString defaultValue = textList.at(1)->text();
            bool isVariable = boolList.at(0)->isChecked();

            if (isVariable) {
                q->prepare("INSERT INTO TestArg (testID, argIdx, arg, isVarArg, defaultVal) VALUES (?, ?, ?, ?, ?)");
                q->addBindValue(testID);
                q->addBindValue(i);
                q->addBindValue(arg);
                q->addBindValue(isVariable);
                q->addBindValue(defaultValue);
            }
            else {
                q->prepare("INSERT INTO TestArg (testID, argIdx, arg, isVarArg) VALUES (?, ?, ?, ?)");
                q->addBindValue(testID);
                q->addBindValue(i);
                q->addBindValue(arg);
                q->addBindValue(isVariable);
            }

            dbExec(q); 
        }
    }

   newTestsDialog->close();
}

void VerificationValidationWidget::testListSelection(QListWidgetItem* test_clicked) {
    QSqlQuery* q1 = new QSqlQuery(getDatabase());
    QSqlQuery* q2 = new QSqlQuery(getDatabase());
    
    q1->prepare("Select testSuiteID from TestsInSuite Where testID = :id");
    q1->bindValue(":id", testItemMap.at(test_clicked).id);
    dbExec(q1);
    while(q1->next()){
        QListWidgetItem* suite = suiteList->item(q1->value(0).toInt()-1);
        if(!test_clicked->checkState()){
            // If any test unchekced -> update test sa and suite containing test_clicked
            if(suite->checkState()){
                suite->setCheckState(Qt::Unchecked);
                suite_sa->item(0)->setCheckState(Qt::Unchecked);
            }
        } else {
            // Check if all test in a suite is checked  -> check suite
            q2->prepare("Select testID from TestsInSuite Where testSuiteID = :suiteID");
            q2->bindValue(":suiteID", q1->value(0).toInt());
            dbExec(q2);
            while(q2->next()){
                // QListWidgetItem* test = testList->item(q2->value(0).toInt()-1);
                QListWidgetItem* test = testIdMap.at(q2->value(0).toInt());

                if(!test->checkState()){
                    return;
                }
            }
            suite->setCheckState(Qt::Checked);
        }
    }

    if(!test_clicked->checkState()){
        test_sa->item(0)->setCheckState(Qt::Unchecked);
    }
    checkSuiteSA();
    checkTestSA();

    delete q1;
    delete q2;
}

void VerificationValidationWidget::updateDbwithRemovedTest() {
    QSqlQuery* q = new QSqlQuery(getDatabase());

    for (QString test : selectedTests)
    {
        q->prepare("DELETE FROM Tests WHERE id = :testID");
        q->bindValue(":testID", test);
        dbExec(q,!SHOW_ERROR_POPUP); 
        
        q->prepare("DELETE FROM TestsInSuite WHERE testID = :testID");
        q->bindValue(":testID", test);
        dbExec(q,!SHOW_ERROR_POPUP); 

        // uncommennt when the database is update to have args
        q->prepare("DELETE FROM TestArg WHERE testID = :testID");
        q->bindValue(":testID", test);
        dbExec(q,!SHOW_ERROR_POPUP);
    }

    removeTestDialog->close();
}

void VerificationValidationWidget::SetupRemoveTestUI() {
    testList->clear();
    suiteList->clear();
    selectedTests.clear();

    removeTestDialog = new QDialog();
    removeTestDialog->setModal(true);
    removeTestDialog->setWindowTitle("Remove test");

    // Get test list from db
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    query.exec("Select testName, testCommand from Tests ORDER BY id ASC");
    QStringList tests;
    QStringList testCmds;
    while(query.next()){
    	tests << query.value(0).toString();
        testCmds << query.value(1).toString();
    }
    
    // Insert test list into tests checklist widget
    testList->addItems(tests);
    QListWidgetItem* item = 0;
    for (int i = 0; i < testList->count(); i++) {
        item = testList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        }

    // Tests checklist add to dialog
   	testList->setMinimumWidth(testList->sizeHintForColumn(0)+40);

   	// Populate Search bar
    QHBoxLayout* searchBar = new QHBoxLayout();
    QLabel* searchLabel = new QLabel("Search: ");
    searchBox = new QLineEdit("");
    searchBar->addWidget(searchLabel);
    searchBar->addWidget(searchBox);
	
    // format and populate Select Tests dialog box
    QGridLayout* grid = new QGridLayout();
    
    QGroupBox* groupbox2 = new QGroupBox("Test List");
    QVBoxLayout* r_vbox = new QVBoxLayout();
    r_vbox->addLayout(searchBar);
    r_vbox->addSpacing(5);
    r_vbox->addSpacing(5);
    r_vbox->addWidget(testList);
    groupbox2->setLayout(r_vbox);
    
    QGroupBox* groupbox3 = new QGroupBox();
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QHBoxLayout* hbox = new QHBoxLayout();
    hbox->addWidget(buttonOptions);
    groupbox3->setLayout(hbox);
    
    //grid->addWidget(groupbox1, 0, 0);
    grid->addWidget(groupbox2, 0, 1);
    grid->addWidget(groupbox3, 1, 0, 1, 2);
    removeTestDialog->setLayout(grid);
	
    // Test select signal connect function
    connect(testList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateSelectedTestList(QListWidgetItem*)));

    // Search button pressed signal select function
    connect(searchBox, SIGNAL(textEdited(const QString &)), this, SLOT(searchTests(const QString &)));

    connect(buttonOptions, SIGNAL(clicked(QAbstractButton *)), this, SLOT(updateDbwithRemovedTest()));
    connect(buttonOptions, &QDialogButtonBox::rejected, removeTestDialog, &QDialog::reject);

    removeTestDialog->exec();
}

void VerificationValidationWidget::SetupRemoveTestSuiteUI() {
    testList->clear();
    suiteList->clear();
    selectedTests.clear();

    removeTestSuiteDialog = new QDialog();
    removeTestSuiteDialog->setModal(true);
    removeTestSuiteDialog->setWindowTitle("Remove test suite");

   	// Populate Search bar
    QHBoxLayout* searchBar = new QHBoxLayout();
    QLabel* searchLabel = new QLabel("Search: ");
    searchBox = new QLineEdit("");
    searchBar->addWidget(searchLabel);
    searchBar->addWidget(searchBox);
	
    // format and populate Select Tests dialog box
    QGridLayout* grid = new QGridLayout();

    
    // Get suite list from db
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    query.exec("Select suiteName from TestSuites ORDER by id ASC");
    QStringList  testSuites;
    while(query.next()){
    	testSuites << query.value(0).toString();
    }
    // Insert suite list into suites checklist widget
    suiteList->addItems(testSuites);
    
    QListWidgetItem* item = 0;
    for (int i = 0; i < suiteList->count(); i++) {
        item = suiteList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
    }
    
    QGroupBox* groupbox2 = new QGroupBox("Test Suite List");
    QVBoxLayout* r_vbox = new QVBoxLayout();
    r_vbox->addLayout(searchBar);
    r_vbox->addSpacing(5);
    r_vbox->addSpacing(5);
    r_vbox->addWidget(suiteList);
    groupbox2->setLayout(r_vbox);
    
    QGroupBox* groupbox3 = new QGroupBox();
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QHBoxLayout* hbox = new QHBoxLayout();
    hbox->addWidget(buttonOptions);
    groupbox3->setLayout(hbox);
    
    grid->addWidget(groupbox2, 0, 1);
    grid->addWidget(groupbox3, 1, 0, 1, 2);
    removeTestSuiteDialog->setLayout(grid);
	
    // Test select signal connect function
    connect(suiteList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateSelectedSuiteList(QListWidgetItem*)));

    // Search button pressed signal select function
    connect(searchBox, SIGNAL(textEdited(const QString &)), this, SLOT(searchSuites(const QString &)));

    connect(buttonOptions, SIGNAL(clicked(QAbstractButton *)), this, SLOT(updateDbwithRemovedSuite()));
    connect(buttonOptions, &QDialogButtonBox::rejected, removeTestSuiteDialog, &QDialog::reject);

    removeTestSuiteDialog->exec();
}

void VerificationValidationWidget::SetupNewTestUI() {
    testList->clear();
    suiteList->clear();
    selectedTests.clear();
    groupBoxVector.clear();

    newTestsDialog = new QDialog();
    newTestsDialog->setModal(true);
    newTestsDialog->setWindowTitle("Create new test");

    //*********************


    QGridLayout* grid = new QGridLayout();

    // New test name
    QHBoxLayout* newTestName = new QHBoxLayout();
    QLabel* newTestLabel = new QLabel("Test name: ");
    QLineEdit* newTestNameBox = new QLineEdit("");
    newTestName->addWidget(newTestLabel);
    newTestName->addWidget(newTestNameBox);

    // New test command
    QHBoxLayout* testCommand = new QHBoxLayout();
    QLabel* commandLabel = new QLabel("Test command: ");
    QLineEdit* newCommandBox = new QLineEdit("");
    testCommand->addWidget(commandLabel);
    testCommand->addWidget(newCommandBox);

    // New test catagory
    QHBoxLayout* testCatagory = new QHBoxLayout();
    QLabel* catagoryLabel = new QLabel("Test catagory: ");
    QLineEdit* newCatagoryBox = new QLineEdit();
    testCatagory->addWidget(catagoryLabel);
    testCatagory->addWidget(newCatagoryBox);

    groupbox1 = new QGroupBox("Test List");
    QVBoxLayout* r_vbox = new QVBoxLayout();
    r_vbox->addLayout(newTestName);
    r_vbox->addSpacing(5);
    r_vbox->addLayout(testCommand);
    r_vbox->addSpacing(5);
    r_vbox->addLayout(testCatagory);
    r_vbox->addSpacing(5);
    r_vbox->addSpacing(5);
    groupbox1->setLayout(r_vbox);

    int maxCol = 4;
    int numArgs = 15;
    for (int i = 0; i < numArgs; i++) {
        QString index = QString::number(i + 1);
        groupBoxVector.push_back(new QGroupBox("Argument " + index));
        QVBoxLayout* arg_vbox = new QVBoxLayout();

        // Tops comment for users
        QHBoxLayout* tops = new QHBoxLayout();
        QLabel* topsLabel = new QLabel("If for tops write PATH or NAME \nin arguments");
        tops->addWidget(topsLabel);


        // argument
        QHBoxLayout* arg = new QHBoxLayout();
        QLabel* argLabel = new QLabel("Argument: ");
        QLineEdit* argInputBox = new QLineEdit();
        arg->addWidget(argLabel);
        arg->addWidget(argInputBox);

        //default value
        QHBoxLayout* defaultValue = new QHBoxLayout();
        QLabel* defaultLabel = new QLabel("Default value: ");
        QLineEdit*  defaultInputBox = new QLineEdit();
        defaultValue->addWidget(defaultLabel);
        defaultValue->addWidget(defaultInputBox);

        // variable checkbox
        QHBoxLayout* isArgsVariable = new QHBoxLayout();
        QLabel* isArgsVariableLabel = new QLabel("Variable arguments?: ");
        QCheckBox* checkbox = new QCheckBox();
        isArgsVariable->addWidget(isArgsVariableLabel);
        isArgsVariable->addWidget(checkbox);


        QVBoxLayout* vbox = new QVBoxLayout();
        vbox->addLayout(tops);
        vbox->addSpacing(5);
        vbox->addLayout(arg);
        vbox->addSpacing(5);
        vbox->addLayout(defaultValue);
        vbox->addSpacing(5);
        vbox->addLayout(isArgsVariable);
        vbox->addSpacing(5);
        //vbox->addSpacing(5);
        groupBoxVector.at(i)->setLayout(vbox);

        int row = (i + 1) / maxCol;
        int column = (i + 1) % maxCol;
        grid->addWidget(groupBoxVector.at(i), row, column, 1, 1);
    }

    // accept reject buttons
    QGroupBox* groupbox3 = new QGroupBox();
    //QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok);
    QHBoxLayout* hbox = new QHBoxLayout();
    hbox->addWidget(buttonOptions);
    groupbox3->setLayout(hbox);

    grid->addWidget(groupbox1, 0, 0);
    grid->addWidget(groupbox3, (numArgs/maxCol) + 1, maxCol - 1, 1, 2);
    newTestsDialog->setLayout(grid);
    //newTestsDialog->layout()->addWidget(scrollArea);


    //connect(testList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateSelectedTestList(QListWidgetItem*)));

    connect(buttonOptions, SIGNAL(clicked(QAbstractButton *)), this, SLOT(updateDbwithNewTest()));
    //connect(buttonOptions, &QDialogButtonBox::accepted, this, SLOT(updateDbwithNewTest()));
    //connect(buttonOptions, &QDialogButtonBox::rejected, newTestsDialog, &QDialog::reject);

    newTestsDialog->exec();
}

void VerificationValidationWidget::SetupNewTestSuiteUI() {
    testList->clear();
    suiteList->clear();
    selectedTests.clear();

    newTestSuiteDialog = new QDialog();
    newTestSuiteDialog->setModal(true);
    newTestSuiteDialog->setWindowTitle("Create new test suite");

    // Get test list from db
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    query.exec("Select testName, testCommand from Tests ORDER BY id ASC");
    QStringList tests;
    QStringList testCmds;
    while(query.next()){
    	tests << query.value(0).toString();
        testCmds << query.value(1).toString();
    }
    
    // Insert test list into tests checklist widget
    testList->addItems(tests);
    QListWidgetItem* item = 0;
    for (int i = 0; i < testList->count(); i++) {
        item = testList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        }

    // Tests checklist add to dialog
   	testList->setMinimumWidth(testList->sizeHintForColumn(0)+40);
    

    // Suite name entry
    QHBoxLayout* newSuiteName = new QHBoxLayout();
    QLabel* newSuiteLabel = new QLabel("New test Suite Name: ");
    newSuiteNameBox = new QLineEdit("");
    newSuiteName->addWidget(newSuiteLabel);
    newSuiteName->addWidget(newSuiteNameBox);

   	// Populate Search bar
    QHBoxLayout* searchBar = new QHBoxLayout();
    QLabel* searchLabel = new QLabel("Search: ");
    searchBox = new QLineEdit("");
    searchBar->addWidget(searchLabel);
    searchBar->addWidget(searchBox);
	
    // format and populate Select Tests dialog box
    QGridLayout* grid = new QGridLayout();
    
    QGroupBox* groupbox2 = new QGroupBox("Test List");
    QVBoxLayout* r_vbox = new QVBoxLayout();
    r_vbox->addLayout(newSuiteName);
    r_vbox->addSpacing(5);
    r_vbox->addLayout(searchBar);
    r_vbox->addSpacing(5);
    r_vbox->addSpacing(5);
    r_vbox->addWidget(testList);
    groupbox2->setLayout(r_vbox);
    
    QGroupBox* groupbox3 = new QGroupBox();
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QHBoxLayout* hbox = new QHBoxLayout();
    hbox->addWidget(buttonOptions);
    groupbox3->setLayout(hbox);
    
    //grid->addWidget(groupbox1, 0, 0);
    grid->addWidget(groupbox2, 0, 1);
    grid->addWidget(groupbox3, 1, 0, 1, 2);
    newTestSuiteDialog->setLayout(grid);
	
    // Test select signal connect function
    connect(testList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateSelectedTestList(QListWidgetItem*)));

    // Search button pressed signal select function
    connect(searchBox, SIGNAL(textEdited(const QString &)), this, SLOT(searchTests(const QString &)));

    connect(buttonOptions, SIGNAL(clicked(QAbstractButton *)), this, SLOT(updateDbwithNewSuite()));
    connect(buttonOptions, &QDialogButtonBox::rejected, newTestSuiteDialog, &QDialog::reject);

    newTestSuiteDialog->exec();
}


// void VerificationValidationWidget::updateVarArgs(QString testName, int index, QString input){
//     std::cout << "called"  << std::endl;
//     std::cout << testName.toStdString() << std::endl;
//     std::cout << index << std::endl;
//     std::cout << input.toStdString() << std::endl;
// }

void VerificationValidationWidget::updateVarArgs() {
    std::cout << "called"  << std::endl;
}

void VerificationValidationWidget::userInputDialogUI(QListWidgetItem* test) {
    if(testItemMap.find(test) != testItemMap.end()){
        if(testItemMap.at(test).hasValArgs) {
            QDialog* userInputDialog = new QDialog();
            userInputDialog->setModal(true);
            userInputDialog->setWindowTitle("Custom Inputs");

            QVBoxLayout* vLayout = new QVBoxLayout();
            QFormLayout* formLayout = new QFormLayout();

            vLayout->addWidget(new QLabel("Test Name: "+ test->text()));
            vLayout->addWidget(new QLabel("Test Command: "+ constructTestCommand(testItemMap.at(test))));
            
            vLayout->addSpacing(15);

            QSqlQuery* q = new QSqlQuery(getDatabase());
            q->prepare("Select arg, defaultVal from TestArg Where testID = :id AND isVarArg = 1 ORDER by argIdx");
            q->bindValue(":id", testItemMap.at(test).id);
            dbExec(q);

            while(q->next()){
                formLayout->addRow(q->value(0).toString(), new QLineEdit(q->value(1).toString()));
                formLayout->setSpacing(10);
            }
            
            vLayout->addLayout(formLayout);
            

            // QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            // vLayout->addWidget(buttonOptions);
            
            QPushButton* setBtn = new QPushButton("Set");
            vLayout->addWidget(setBtn);

            userInputDialog->setLayout(vLayout);
            userInputDialog->exec();
            
            // NEED HELP: send lineqdit input from fromlayout to updateVarArgs (C++11 lambda expressions)
            // Get input with formLayout-> itemAt -> widget (?)

            connect(setBtn, SIGNAL(clicked()), this, SLOT(updateVarArgs()));
        }
    }
}

void VerificationValidationWidget::setupUI() {
    testList->clear();
    suiteList->clear();
    selectedTests.clear();
    // TODO: allow input
    // TODO: select tops
    // TODO: add test categories in test lists
	
    // setup result table's column headers
    QStringList columnLabels;
    columnLabels << "   " << "Test Name" << "Description" << "Object Path";
    resultTable->setColumnCount(columnLabels.size());
    resultTable->setHorizontalHeaderLabels(columnLabels);
    resultTable->verticalHeader()->setVisible(false);
    resultTable->horizontalHeader()->setStretchLastSection(true);
    resultTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    addWidget(resultTable);

    // Get test list from db
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    query.exec("Select id, testName, testCommand, hasValArgs, category from Tests ORDER BY category ASC");

    QStringList testIdList;
    QStringList tests;
    QStringList testCmds;
    QStringList hasVariableList;
    QStringList categoryList;

    while(query.next()){
        testIdList << query.value(0).toString();
    	tests << query.value(1).toString();
        testCmds << query.value(2).toString();
        hasVariableList << query.value(3).toString();
        categoryList << query.value(4).toString();
    }

    // Creat test widget item
    QIcon edit_icon(":/icons/editIcon.png");
    for (int i = 0; i < tests.size(); i++) {
        QListWidgetItem* item = new QListWidgetItem(tests[i]);
        int id = testIdList[i].toInt();
        bool hasValArgs = hasVariableList[i].toInt();

        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
        if(hasValArgs) {
            item->setIcon(edit_icon);
        }
        testItemMap.insert({item, TestItem(new QListWidgetItem(tests[i]), id, tests[i], testCmds[i], hasValArgs, categoryList[i])});
        testIdMap.insert(make_pair(id, item));
        // TODO: rebuild command with args
        item->setToolTip(constructTestCommand(testItemMap.at(item)));
        testList->addItem(item);
    }

    // Add test categories in test lists
    int offset = 0;
    for (int i = 0; i < categoryList.size(); i++) {
        QList<QListWidgetItem *> items = testList->findItems(categoryList[i], Qt::MatchExactly);
        if (items.size() == 0) {
            QListWidgetItem* item = new QListWidgetItem(categoryList[i]);
            item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
            item->setToolTip("Category");
            testList->insertItem(i+offset, item);
            offset += 1;
        }
    }

    // Tests checklist add to dialog
   	testList->setMinimumWidth(testList->sizeHintForColumn(0)+40);

    // Get suite list from db
    query.exec("Select suiteName from TestSuites ORDER by id ASC");
    QStringList testSuites;
    while(query.next()){
    	testSuites << query.value(0).toString();
    }
    // Insert suite list into suites checklist widget
    suiteList->addItems(testSuites);
    QListWidgetItem* item = 0;
    for (int i = 0; i < suiteList->count(); i++) {
        item = suiteList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
    }
    
    // Select ALL Suites
   	QListWidgetItem* suite_sa_item = new QListWidgetItem("Select All Suites");
   	suite_sa_item->setFlags(suite_sa_item->flags() | Qt::ItemIsUserCheckable);
   	suite_sa_item->setCheckState(Qt::Unchecked);
   	suite_sa->addItem(suite_sa_item);
   	suite_sa->setFixedHeight(20);
    suite_sa_item->setFlags(suite_sa_item->flags() &  ~Qt::ItemIsSelectable);
   	
   	// Select ALL Tests
   	QListWidgetItem* test_sa_item = new QListWidgetItem("Select All Tests");
   	test_sa_item->setFlags(test_sa_item->flags() | Qt::ItemIsUserCheckable);
   	test_sa_item->setCheckState(Qt::Unchecked);
   	test_sa->addItem(test_sa_item);
   	test_sa->setFixedHeight(20);
   	test_sa_item->setFlags(test_sa_item->flags() &  ~Qt::ItemIsSelectable);

   	// Popuulate Search bar
    QHBoxLayout* searchBar = new QHBoxLayout();
    QLabel* searchLabel = new QLabel("Search: ");
    searchBox = new QLineEdit("");
    searchBar->addWidget(searchLabel);
    searchBar->addWidget(searchBox);
	
    // format and populate Select Tests dialog box
    selectTestsDialog->setModal(true);
    selectTestsDialog->setWindowTitle("Select Tests");
    QGridLayout* grid = new QGridLayout();
	
    QGroupBox* groupbox1 = new QGroupBox("Select Test Categories");
    QVBoxLayout* l_vbox = new QVBoxLayout();
    l_vbox->addWidget(suite_sa);
    l_vbox->addSpacing(10);
    l_vbox->addWidget(suiteList);
    groupbox1->setLayout(l_vbox);
    
    QGroupBox* groupbox2 = new QGroupBox("Test List");
    QVBoxLayout* r_vbox = new QVBoxLayout();
    r_vbox->addLayout(searchBar);
    r_vbox->addSpacing(5);
    r_vbox->addWidget(test_sa);
    r_vbox->addSpacing(5);
    r_vbox->addWidget(testList);
    groupbox2->setLayout(r_vbox);
    
    QGroupBox* groupbox3 = new QGroupBox();
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QHBoxLayout* hbox = new QHBoxLayout();
    hbox->addWidget(buttonOptions);
    groupbox3->setLayout(hbox);
    
    grid->addWidget(groupbox1, 0, 0);
    grid->addWidget(groupbox2, 0, 1);
    grid->addWidget(groupbox3, 1, 0, 1, 2);
    selectTestsDialog->setLayout(grid);
	
    // Select all signal connect function
    connect(suite_sa, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateSuiteSelectAll(QListWidgetItem *)));
    connect(test_sa, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateTestSelectAll(QListWidgetItem *)));
    
    // Suite select signal connect function
    connect(suiteList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateTestListWidget(QListWidgetItem *)));
    // Test select signal connect function
    connect(testList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(testListSelection(QListWidgetItem*)));

    // Search button pressed signal select function
    connect(searchBox, SIGNAL(textEdited(const QString &)), this, SLOT(searchTests(const QString &)));

    // Test input for gqa
    connect(testList, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(userInputDialogUI(QListWidgetItem *)));

    connect(buttonOptions, &QDialogButtonBox::accepted, selectTestsDialog, &QDialog::accept);
    connect(buttonOptions, &QDialogButtonBox::accepted, this, &VerificationValidationWidget::runTests);
    connect(buttonOptions, &QDialogButtonBox::rejected, selectTestsDialog, &QDialog::reject);

    connect(resultTable, SIGNAL(cellDoubleClicked(int, int)), this, SLOT(setupDetailedResult(int, int)));
}

QSqlQuery* VerificationValidationWidget::dbExec(QString command, bool showErrorPopup) {
    QSqlQuery* query = new QSqlQuery(command, getDatabase());
    if (showErrorPopup && !query->isActive())
        popup("[Verification & Validation]\nERROR: query failed to execute: " + query->lastError().text() + "\n\n" + command);
    return query;
}

void VerificationValidationWidget::dbExec(QSqlQuery*& query, bool showErrorPopup) {
    query->exec();
    if (showErrorPopup && !query->isActive())
        popup("[Verification & Validation]\nERROR: query failed to execute: " + query->lastError().text() + "\n\n" + query->lastQuery());
}

void VerificationValidationWidget::dbClearResults() {
    delete dbExec("DELETE FROM TestResults");
    delete dbExec("DELETE FROM Issues");
    delete dbExec("DELETE FROM ObjectIssue");
}

void VerificationValidationWidget::resizeEvent(QResizeEvent* event) {
    resultTable->setColumnWidth(0, this->width() * 0.025);
    resultTable->setColumnWidth(1, this->width() * 0.125);
    resultTable->setColumnWidth(2, this->width() * 0.60);
    resultTable->setColumnWidth(3, this->width() * 0.25);

    QHBoxWidget::resizeEvent(event);
}

void VerificationValidationWidget::setupDetailedResult(int row, int column) {
    QDialog* detail_dialog = new QDialog();
    detail_dialog->setModal(true);
    detail_dialog->setWindowTitle("Details");

    QVBoxLayout* mainLayout = new QVBoxLayout();
    QVBoxLayout* detailLayout = new QVBoxLayout();
    QWidget* viewport = new QWidget();
    QScrollArea* scrollArea = new QScrollArea();
    QString resultCode;
    QString testName = resultTable->item(row, TEST_NAME_COLUMN)->text();
    QString description = resultTable->item(row, DESCRIPTION_COLUMN)->text();
    QString objPath = resultTable->item(row, OBJPATH_COLUMN)->text();
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT id, testCommand FROM Tests WHERE testName = ?");
    q->addBindValue(testName);
    dbExec(q);
    if (!q->next()) {
        popup("Failed to show testName: " + testName);
        return;
    }

    int testID = q->value(0).toInt();
    QString testCommand = q->value(1).toString();

    QSqlQuery* q2 = new QSqlQuery(getDatabase());
    q2->prepare("SELECT terminalOutput, resultCode FROM TestResults WHERE testID = ?");
    q2->addBindValue(testID);
    dbExec(q2);
    if (!q2->next()) {
        popup("Failed to show testID: " + testID);
        return;
    }

    QString terminalOutput = q2->value(0).toString();
    int code = q2->value(1).toInt();
    if(code == Result::Code::PASSED)
        resultCode = "Passed";
    else if(code == Result::Code::WARNING)
        resultCode = "Warning";
    else if(code == Result::Code::FAILED)
        resultCode = "Failed";
    else
        resultCode = "Unparseable";

    QLabel *testNameHeader = new QLabel("Test Name:");
    testNameHeader->setStyleSheet("font-weight: bold");
    QLabel *commandHeader = new QLabel("Command:");
    commandHeader->setStyleSheet("font-weight: bold");
    QLabel *resultCodeHeader = new QLabel("Result Code:");
    resultCodeHeader->setStyleSheet("font-weight: bold");
    QLabel *descriptionHeader = new QLabel("Description:");
    descriptionHeader->setStyleSheet("font-weight: bold");
    QLabel *rawOutputHeader = new QLabel("Raw Output:");
    rawOutputHeader->setStyleSheet("font-weight: bold");

    detailLayout->addWidget(testNameHeader);
    detailLayout->addWidget(new QLabel(testName+"\n"));
    detailLayout->addWidget(commandHeader);
    detailLayout->addWidget(new QLabel(testCommand+"\n"));
    detailLayout->addWidget(resultCodeHeader);
    detailLayout->addWidget(new QLabel(resultCode+"\n"));
    detailLayout->addWidget(descriptionHeader);
    detailLayout->addWidget(new QLabel(description+"\n"));
    detailLayout->addWidget(rawOutputHeader);
    detailLayout->addWidget(new QLabel(terminalOutput));
    viewport->setLayout(detailLayout);
    scrollArea->setWidget(viewport);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    mainLayout->addWidget(scrollArea);
    detail_dialog->setLayout(mainLayout);
    detail_dialog->exec();
}

void VerificationValidationWidget::showResult(const QString& testResultID) {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT Tests.testName, TestResults.resultCode, TestResults.terminalOutput FROM Tests INNER JOIN TestResults ON Tests.id=TestResults.testID WHERE TestResults.id = ?");
    q->addBindValue(testResultID);
    dbExec(q);

    if (!q->next()) {
        popup("Failed to show Test Result #" + testResultID);
        return;
    }

    QString testName = q->value(0).toString();
    int resultCode = q->value(1).toInt();
    QString terminalOutput = q->value(2).toString();

    QSqlQuery* q2 = new QSqlQuery(getDatabase());
    q2->prepare("SELECT objectIssueID FROM Issues WHERE testResultID = ?");
    q2->addBindValue(testResultID);
    dbExec(q2, !SHOW_ERROR_POPUP);

    while (q2->next()) {
        QString objectIssueID = q2->value(0).toString();

        QSqlQuery* q3 = new QSqlQuery(getDatabase());
        q3->prepare("SELECT objectName, issueDescription FROM ObjectIssue WHERE id = ?");
        q3->addBindValue(objectIssueID);
        dbExec(q3);

        if (!q3->next()) {
            popup("Failed to retrieve Object Issue #" + objectIssueID);
            return;
        }

        QString objectName = q3->value(0).toString();
        QString issueDescription = q3->value(1).toString();

        resultTable->insertRow(resultTable->rowCount());

        QString iconPath = "";
        if (resultCode == VerificationValidation::Result::Code::UNPARSEABLE)
            iconPath = ":/icons/unparseable.png";
        else if (resultCode == VerificationValidation::Result::Code::FAILED)
            iconPath = ":/icons/error.png";
        else if (resultCode == VerificationValidation::Result::Code::WARNING)
            iconPath = ":/icons/warning.png";
        else if (resultCode == VerificationValidation::Result::Code::PASSED)
            iconPath = ":/icons/passed.png";

        // Change to hide icon image path from showing
        QTableWidgetItem* icon_item = new QTableWidgetItem;
        QIcon icon(iconPath);
        icon_item->setIcon(icon);
        resultTable->setItem(resultTable->rowCount()-1, RESULT_CODE_COLUMN, icon_item);
        resultTable->setItem(resultTable->rowCount()-1, TEST_NAME_COLUMN, new QTableWidgetItem(testName));
        resultTable->setItem(resultTable->rowCount()-1, DESCRIPTION_COLUMN, new QTableWidgetItem(issueDescription));
        resultTable->setItem(resultTable->rowCount()-1, OBJPATH_COLUMN, new QTableWidgetItem(objectName));

        delete q3;
    }
    // Only select rows, disable edit
    resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    delete q;
    delete q2;
}

void VerificationValidationWidget::showAllResults() {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT id FROM TestResults WHERE modelID = ?");
    q->addBindValue(modelID);
    dbExec(q);

    QString testResultID;
    while (q && q->next()) {
        testResultID = q->value(0).toString();
        showResult(testResultID);
    }

    delete q;
}
