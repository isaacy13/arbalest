#include "VerificationValidationWidget.h"
#include <Document.h>
#include "MainWindow.h"
#include <QAction>
#include <fstream>

using Result = VerificationValidation::Result;
using DefaultTests = VerificationValidation::DefaultTests;
using Parser = VerificationValidation::Parser;

#define SHOW_ERROR_POPUP true

VerificationValidationWidget::VerificationValidationWidget(MainWindow* mainWindow, Document* document, QWidget* parent) : 
document(document), mainWindow(mainWindow), parentDockable(mainWindow->getVerificationValidationDockable()),
terminal(NULL), testList(new QListWidget()), resultTable(new QTableWidget()), selectTestsDialog(new QDialog()),
suiteList(new QListWidget()), test_sa(new QListWidget()), suite_sa(new QListWidget()),
msgBoxRes(NO_SELECTION), dbConnectionName(""), runningTests(false), btnCollapseTerminal(new QPushButton()), mgedWorkerThread(nullptr)
{
    if (!dbConnectionName.isEmpty()) return;

    // get BRL-CAD cache path
    char cache[MAXPATHLEN];
    bu_dir(cache, MAXPATHLEN, BU_DIR_CACHE, ".atr", NULL);
    cacheFolder = QString(cache);
    
    // create cache if doesn't already exist
    QDir dirCacheFolder(cache);
    if (!dirCacheFolder.exists() && !dirCacheFolder.mkpath(".")) throw std::runtime_error("Failed to create atr cache folder");
   
    QString dbFilePath = cacheFolder + "/untitled/" + QString::number(document->getDocumentId()) + ".atr";;
    try { dbConnect(dbFilePath); } catch (const std::runtime_error& e) { throw e; }
    dbInitTables();
    dbPopulateDefaults();
    
    btnCollapseTerminal->setIcon(QIcon(":/icons/terminal.png"));
    btnCollapseTerminal->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
    setupUI();
    updateDockableHeader();
    
    validateChecksum();
    if (msgBoxRes == OPEN) {
        showAllResults();
        msgBoxRes = NO_SELECTION;
    } else if (msgBoxRes == DISCARD) {
        dbClearResults();
        resultTable->setRowCount(0);
    }
}

VerificationValidationWidget::~VerificationValidationWidget() {
    QString dockableTitle = "Verification & Validation";
    QLabel *title = new QLabel(dockableTitle);
    title->setObjectName("dockableHeader");
    parentDockable->setTitleBarWidget(title);
    dbClose();
}

QString addDoubleQuote(QString str){
    str.insert(0, "\"");
    str.insert(str.size(), "\"");
    return str;
}

void VerificationValidationWidget::exportToCSV(){
    if (resultTable->rowCount() == 0) return;
	QString filePath = QFileDialog::getSaveFileName(this, tr("Export test results as CSV"), QString(), "CSV (*.csv)");
    if (!filePath.isEmpty()) {
        if(!filePath.endsWith(".csv")){
            filePath.append(".csv");
        }

        std::ofstream csvFile;
        csvFile.open(filePath.toStdString());
        // CSV Header
        csvFile << "Error Type,Test Name,Description,Issue Object,Full Path\n";

        QSqlQuery* q = new QSqlQuery(getDatabase());
        QSqlQuery* q1 = new QSqlQuery(getDatabase());
        QSqlQuery* q2 = new QSqlQuery(getDatabase());
        QSqlQuery* q3 = new QSqlQuery(getDatabase());
        q->prepare("SELECT Tests.testName, TestResults.id, TestResults.resultCode, TestResults.terminalOutput FROM Tests INNER JOIN TestResults ON Tests.id=TestResults.testID");
        dbExec(q);
        while(q->next()){
            QString testName = q->value(0).toString();
            int testResultID = q->value(1).toInt();
            int resultCode = q->value(2).toInt();
            QString terminalOutput = q->value(3).toString();

            q1->prepare("SELECT TestArg.arg FROM TestArg INNER JOIN TestResults ON TestArg.id = TestResults.objectArgID WHERE TestResults.id = ?");
            q1->addBindValue(testResultID);
            dbExec(q1);
            
            QString object;
            while(q1->next()){
                object = q1->value(0).toString();
            }
            QString objectName;
            QString issueDescription;

            if (resultCode == Result::Code::PASSED) {
                csvFile << "Passed" << ",";
                csvFile << addDoubleQuote(testName).toStdString() << "\n";
            } else if (resultCode == Result::Code::UNPARSEABLE) {
                csvFile << "Unparseable" << ",";
                csvFile << addDoubleQuote(testName).toStdString() << ",";
                q2->prepare("SELECT terminalOutput FROM TestResults WHERE id = ?");
                q2->addBindValue(testResultID);
                dbExec(q2, !SHOW_ERROR_POPUP);

                while (q2->next()) {
                    QString terminalOutput = q2->value(0).toString().replace("\n", "");;
                    csvFile << addDoubleQuote(terminalOutput).toStdString() << "\n";
                }
            } else {
                q2->prepare("SELECT objectIssueID FROM Issues WHERE testResultID = ?");
                q2->addBindValue(testResultID);
                dbExec(q2, !SHOW_ERROR_POPUP);

                while (q2->next()) {
                    QString objectIssueID = q2->value(0).toString();

                    q3->prepare("SELECT objectName, issueDescription FROM ObjectIssue WHERE id = ?");
                    q3->addBindValue(objectIssueID);
                    dbExec(q3);

                    while(q3->next()){
                        objectName = q3->value(0).toString();
                        issueDescription = q3->value(1).toString().replace("\n", "");

                        if (resultCode == VerificationValidation::Result::Code::FAILED){
                            csvFile << "Failed" << ",";
                        }
                        else if (resultCode == VerificationValidation::Result::Code::WARNING){
                            csvFile << "Warning" << ",";
                        }
                        
                        QStringList objectTree = objectName.split("/");

                        csvFile << addDoubleQuote(testName).toStdString() << ",";
                        csvFile << addDoubleQuote(issueDescription).toStdString() << ",";
                        csvFile << addDoubleQuote(objectTree.at(objectTree.size()-1)).toStdString() << ",";
                        csvFile << addDoubleQuote(objectName).toStdString() << "\n";
                    }
                }
            }
        }
        delete q;
        delete q1;
        delete q2;
        delete q3;

        csvFile.close();
        popup("[Verification & Validation]\nSuccessfully exported test results to "+filePath);
    }
}

void VerificationValidationWidget::showSelectTests() {
    emit mainWindow->setStatusBarMessage("Select tests to run...");
    selectTestsDialog->exec();
}

void VerificationValidationWidget::dbConnect(const QString& dbFilePath) {
    if (!QSqlDatabase::isDriverAvailable("QSQLITE"))
        throw std::runtime_error("[Verification & Validation] ERROR: sqlite is not available");
    
    this->dbFilePath = dbFilePath;
    QString* fp = document->getFilePath();
    
    // if persistent titled file, create UUID and store accordingly
    if (fp) {
        QString* uuid = generateUUID(*fp);
        if (!uuid) throw std::runtime_error("Failed to generate UUID for " + fp->toStdString());
        QDir dbFolder(cacheFolder + "/" + uuid->left(2) + "/" + uuid->right(uuid->size() - 2));
        if (!dbFolder.exists()) dbFolder.mkpath(".");

        this->dbFilePath = dbFolder.absolutePath() + "/" + fp->split("/").last() + ".atr";
    }

    dbConnectionName = this->dbFilePath + "-connection";
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
    if (QFile::exists(this->dbFilePath)) {
        QMessageBox msgBox; 
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("Detected existing test results in " + this->dbFilePath + ".\n\nDo you want to open or discard the results?");
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
    db.setDatabaseName(this->dbFilePath);

    if (!db.open() || !db.isOpen())
        throw std::runtime_error("[Verification & Validation] ERROR: db failed to open: " + db.lastError().text().toStdString());
}

void VerificationValidationWidget::dbInitTables() {
    if (!getDatabase().tables().contains("Model"))
        delete dbExec("CREATE TABLE Model (id INTEGER PRIMARY KEY, filepath TEXT NOT NULL UNIQUE, uuid TEXT NOT NULL)");
    if (!getDatabase().tables().contains("Tests"))
        delete dbExec("CREATE TABLE Tests (id INTEGER PRIMARY KEY, testName TEXT NOT NULL, testCommand TEXT NOT NULL, category TEXT NOT NULL)");
    if (!getDatabase().tables().contains("TestResults"))
        delete dbExec("CREATE TABLE TestResults (id INTEGER PRIMARY KEY, modelID INTEGER NOT NULL, testID INTEGER NOT NULL, objectArgID INTEGER NOT NULL, resultCode TEXT, terminalOutput TEXT)");
    if (!getDatabase().tables().contains("Issues"))
        delete dbExec("CREATE TABLE Issues (id INTEGER PRIMARY KEY, testResultID INTEGER NOT NULL, objectIssueID INTEGER NOT NULL)");
    if (!getDatabase().tables().contains("ObjectIssue"))
        delete dbExec("CREATE TABLE ObjectIssue (id INTEGER PRIMARY KEY, objectName TEXT NOT NULL, issueDescription TEXT NOT NULL)");
    if (!getDatabase().tables().contains("TestSuites"))
        delete dbExec("CREATE TABLE TestSuites (id INTEGER PRIMARY KEY, suiteName TEXT NOT NULL, UNIQUE(suiteName))");
    if (!getDatabase().tables().contains("TestsInSuite"))
        delete dbExec("CREATE TABLE TestsInSuite (id INTEGER PRIMARY KEY, testSuiteID INTEGER NOT NULL, testID INTEGER NOT NULL)");
    if (!getDatabase().tables().contains("TestArg"))
        delete dbExec("CREATE TABLE TestArg (id INTEGER PRIMARY KEY, testID INTEGER NOT NULL, argIdx INTEGER NOT NULL, arg TEXT NOT NULL, argType INTEGER NOT NULL, defaultVal TEXT)");
    if (!getDatabase().tables().contains("RunningTests"))
        delete dbExec("CREATE TABLE RunningTests (id INTEGER PRIMARY KEY, testID TEXT NOT NULL, hasFinished TEXT NOT NULL)");
    if (!getDatabase().tables().contains("ObjectTree"))
        delete dbExec("CREATE TABLE ObjectTree (id INTEGER PRIMARY KEY, object TEXT NOT NULL)");
}

void VerificationValidationWidget::dbPopulateDefaults() {
    QSqlQuery* q;
    QString gFilePath = *document->getFilePath();
    QString* uuid = generateUUID(gFilePath);

    if (!uuid) throw std::runtime_error("Failed to generate UUID for " + gFilePath.toStdString());

    // if Model table empty, assume new db and insert model info
    q = new QSqlQuery(getDatabase());
    q->prepare("SELECT id FROM Model WHERE filepath=?");
    q->addBindValue(QDir(*document->getFilePath()).absolutePath());
    dbExec(q, !SHOW_ERROR_POPUP);

    if (!q->next()) {
        q->prepare("INSERT INTO Model (filepath, uuid) VALUES (?, ?)");
        q->addBindValue(QDir(*document->getFilePath()).absolutePath());
        q->addBindValue(*uuid);
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
            Test* t = DefaultTests::allTests[i];
			
            for (const QString& suiteName : t->suiteNames) {
                q->prepare("INSERT OR IGNORE INTO TestSuites VALUES (NULL, ?)");
                q->addBindValue(suiteName);
                dbExec(q);
            }
            
            q->prepare("INSERT INTO Tests (testName, testCommand, category) VALUES (:testName, :testCommand, :category)");
            q->bindValue(":testName", t->testName);
            q->bindValue(":testCommand", t->testCommand);
            q->bindValue(":category", t->category);
            dbExec(q);
            QString testID = q->lastInsertId().toString();

            for (int j = 0; j < DefaultTests::allTests[i]->ArgList.size(); j++) {
                Arg::Type type = DefaultTests::allTests[i]->ArgList[j].type;

                int cnt = 0;
                q->prepare("SELECT COUNT(*) FROM TestArg WHERE testID = ? AND argIdx = ? AND arg = ? AND argType = ?");
                q->addBindValue(testID);
                q->addBindValue(DefaultTests::allTests[i]->ArgList[j].argIdx);
                q->addBindValue(DefaultTests::allTests[i]->ArgList[j].argument);
                q->addBindValue(type);
                dbExec(q);

                if (q->next()) cnt = q->value(0).toInt();

                if (!cnt) {
                    q->prepare("INSERT INTO TestArg (testID, argIdx, arg, argType, defaultVal) VALUES (?,?,?,?,?)");
                    q->addBindValue(testID);
                    q->addBindValue(DefaultTests::allTests[i]->ArgList[j].argIdx);
                    q->addBindValue(DefaultTests::allTests[i]->ArgList[j].argument);
                    q->addBindValue(type);
                    q->addBindValue(DefaultTests::allTests[i]->ArgList[j].defaultValue);
                    dbExec(q);
                }
            }
            
            for (const QString& suiteName : t->suiteNames) {
                q->prepare("SELECT id FROM TestSuites WHERE suiteName = ?");
                q->addBindValue(suiteName);
                dbExec(q);
                if (!q->next()) continue;
                QString testSuiteID = q->value(0).toString();
                q->prepare("INSERT INTO TestsInSuite (testID, testSuiteID) VALUES (?, ?)");
                q->addBindValue(testID);
                q->addBindValue(testSuiteID);
                dbExec(q);
            }
        }
    }
    delete q;
}

void VerificationValidationWidget::searchTests_run(const QString &input)  {
    // Hide category when search
    if(input.isEmpty()){
        QListWidgetItem* item = 0;
        for (int i = 0; i < testList->count(); i++) {
            item = testList->item(i);
            item->setHidden(false);
        }
    } else {
        QList<QListWidgetItem *> tests = testList->findItems(input, Qt::MatchContains);
        QListWidgetItem* item = 0;
        for (int i = 0; i < testList->count(); i++) {
            item = testList->item(i);
            if(!tests.contains(item) || item->toolTip() == "Category")
                item->setHidden(true);
            else
                item->setHidden(false);
        }
    }
    searchTests_SA();
}

void VerificationValidationWidget::searchTests_rm(const QString &input)  {
    // Hide category when search
    if(input.isEmpty()){
        QListWidgetItem* item = 0;
        for (int i = 0; i < rmTestList->count(); i++) {
            item = rmTestList->item(i);
            item->setHidden(false);
        }
    } else {
        QList<QListWidgetItem *> tests = rmTestList->findItems(input, Qt::MatchContains);
        QListWidgetItem* item = 0;
        for (int i = 0; i < rmTestList->count(); i++) {
            item = rmTestList->item(i);
            if(!tests.contains(item) || item->toolTip() == "Category")
                item->setHidden(true);
            else
                item->setHidden(false);
        }
    }
}

void VerificationValidationWidget::searchTests_SA(){
    QListWidgetItem* item = 0;
    for (int i = 0; i < itemToTestMap.size(); i++) {
        auto it = itemToTestMap.begin();
        std::advance(it, i);
        item = it->first;
        if(!item->isHidden()){
            if(!item->checkState()){
                test_sa->item(0)->setCheckState(Qt::Unchecked);
                return;
            }
        }
	}
    test_sa->item(0)->setCheckState(Qt::Checked);
}

void VerificationValidationWidget::searchTests_TS(const QString &input)  {
    // Hide category when search
    if(input.isEmpty()){
        QListWidgetItem* item = 0;
        for (int i = 0; i < newTSList->count(); i++) {
            item = newTSList->item(i);
            item->setHidden(false);
        }
    } else {
        QList<QListWidgetItem *> tests = newTSList->findItems(input, Qt::MatchContains);
        QListWidgetItem* item = 0;
        for (int i = 0; i < newTSList->count(); i++) {
            item = newTSList->item(i);
            if(!tests.contains(item) || item->toolTip() == "Category")
                item->setHidden(true);
            else
                item->setHidden(false);
        }
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
    for (int i = 0; i < itemToTestMap.size(); i++) {
        auto it = itemToTestMap.begin();
        std::advance(it, i);
        item = it->first;
        if(!item->isHidden()){
            if(sa_option->checkState()){
                item->setCheckState(Qt::Checked);
            } else {
                item->setCheckState(Qt::Unchecked);
            }
            testListSelection(item);
        }
	}
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
    QListWidgetItem* item = 0;
    for (int i = 0; i < itemToTestMap.size(); i++) {
        auto it = itemToTestMap.begin();
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
        item = idToItemMap.at(id);

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

void VerificationValidationWidget::testListSelection(QListWidgetItem* test_clicked) {
    if(test_clicked->toolTip() == "Category"){
        return;
    }
    QSqlQuery* q1 = new QSqlQuery(getDatabase());
    QSqlQuery* q2 = new QSqlQuery(getDatabase());
    
    q1->prepare("Select testSuiteID from TestsInSuite Where testID = :id");
    q1->bindValue(":id", itemToTestMap.at(test_clicked).first);
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
                QListWidgetItem* test = idToItemMap.at(q2->value(0).toInt());

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

void VerificationValidationWidget::addItemFromTest(QListWidget* &listWidget){
    // Get test list from db
    QSqlDatabase db = getDatabase();
    QSqlQuery query(db);
    query.exec("Select id, testName, category from Tests ORDER BY category DESC");

    if(listWidget == testList){
        itemToTestMap.clear();
        idToItemMap.clear();
    }

    QStringList testIdList;
    QStringList testNameList;
    QStringList categoryList;

    while(query.next()){
        testIdList << query.value(0).toString();
    	testNameList << query.value(1).toString();
        categoryList << query.value(2).toString();
    }

    // Creat test widget item
    for (int i = 0; i < testNameList.size(); i++) {
        QListWidgetItem* item = new QListWidgetItem(testNameList[i]);
        int id = testIdList[i].toInt();

        std::vector<VerificationValidation::Arg> argList;
        query.prepare("Select arg, defaultVal, argType FROM TestArg Where testID = :id ORDER BY argIdx");
        query.bindValue(":id", id);
        query.exec();

        bool addedObject = false;
        while(query.next()){
            QString arg = query.value(0).toString();
            QString defaultVal = query.value(1).toString();
            Arg::Type type = (Arg::Type) query.value(2).toInt();
            if (type == Arg::Type::ObjectName || type == Arg::Type::ObjectPath) {
                if (addedObject) continue;
                argList.push_back(VerificationValidation::Arg(arg, defaultVal, type));
                addedObject = true;
            }
            else {
                argList.push_back(VerificationValidation::Arg(arg, defaultVal, type));
            }
        }
        Test t(testNameList[i], {}, argList);

        if(listWidget == testList){
            itemToTestMap.insert(make_pair(item, make_pair(id, t)));
            idToItemMap.insert(make_pair(id, item));
            item->setToolTip(itemToTestMap.at(item).second.getCMD());
        }
        
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
        if(t.hasVarArgs()) {
            item->setIcon(QIcon(QPixmap::fromImage(coloredIcon(":/icons/edit_default.png", "$Color-IconEditVVArg"))));
        }

        listWidget->addItem(item);
    }

    // Add test categories in test lists
    int offset = 0;
    for (int i = 0; i < categoryList.size(); i++) {
        QList<QListWidgetItem *> items = listWidget->findItems(categoryList[i], Qt::MatchExactly);
        if (items.size() == 0) {
            QListWidgetItem* item = new QListWidgetItem(categoryList[i]);
            item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
            item->setToolTip("Category");
            QFont itemFont = item->font();
            itemFont.setWeight(QFont::Bold);
            item->setFont(itemFont);
            listWidget->insertItem(i+offset, item);
            offset += 1;
        }
    }

    // Tests checklist add to dialog
   	listWidget->setMinimumWidth(listWidget->sizeHintForColumn(0)+40);
}

void VerificationValidationWidget::createTest() {
    if(testNameInput->text().simplified().isEmpty()){
        popup("[Verification & Validation]\nERROR: cannot create a test with empty name");
        showNewTestDialog();
        return;
    }
    QString testName = testNameInput->text();

    if(testCmdInput->text().simplified().isEmpty()){
        popup("[Verification & Validation]\nERROR: cannot create a test with empty command");
        showNewTestDialog();
        return;
    }
    QString testCmd = testCmdInput->text();

    QString testCategory = "no category";
    if(!testCategoryInput->text().simplified().isEmpty()){
        testCategory = testCategoryInput->text();
    }

    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("INSERT INTO Tests (testName, testCommand, category) VALUES (:testName, :testCommand, :category)");
    q->bindValue(":testName", testName);
    q->bindValue(":testCommand", testCmd);
    q->bindValue(":category", testCategory);
    dbExec(q);

    QString testID = q->lastInsertId().toString();
    for(int i = 0; i < addToSuiteList->count(); i++){
        QListWidgetItem* item = addToSuiteList->item(i);
        if(item->checkState()){
            q->prepare("SELECT id FROM TestSuites WHERE suiteName = ?");
            q->addBindValue(item->text());
            dbExec(q);
            int suiteID = -1;
            while(q->next()){
                suiteID = q->value(0).toInt();
            }
            q->prepare("INSERT INTO TestsInSuite (testSuiteID, testID) VALUES (:suiteID, :testID)");
            q->bindValue(":suiteID", suiteID);
            q->bindValue(":testID", testID);
            dbExec(q);
        }
    }

    // insert cmd into arglist
    q->prepare("INSERT INTO TestArg (testID, argIdx, arg, argType) VALUES (:testID, :argIdx, :arg, :argType)");
    q->bindValue(":testID", testID);
    q->bindValue(":argIdx", 0);
    q->bindValue(":arg", testCmd);
    q->bindValue(":argType", Arg::Type::Static);
    dbExec(q);

    int argIdx = 1;
    for(int i = 0; i < argInputList.size(); i++){
        if(argInputList[i]->text().simplified().isEmpty() && !isVarList[i]->checkState() && varInputList[i]->text().simplified().isEmpty()){
            continue;
        }
        q->prepare("INSERT INTO TestArg (testID, argIdx, arg, argType, defaultVal) VALUES (:testID, :argIdx, :arg, :argType, :defaultVal)");
        q->bindValue(":testID", testID);
        q->bindValue(":argIdx", argIdx);
        q->bindValue(":arg", argInputList[i]->text());
        q->bindValue(":argType", (isVarList[i]->checkState()) ? Arg::Type::Dynamic : Arg::Type::Static);
        q->bindValue(":defaultVal", varInputList[i]->text());
        dbExec(q);
        argIdx += 1;
    }

    // insert dummy object
    q->prepare("INSERT INTO TestArg (testID, argIdx, arg, argType) VALUES (:testID, :argIdx, :arg, :argType)");
    q->bindValue(":testID", testID);
    q->bindValue(":argIdx", argIdx);
    q->bindValue(":arg", "$OBJECT");
    q->bindValue(":argType", Arg::Type::ObjectName);
    dbExec(q);

    setupUI();
}

void VerificationValidationWidget::isArgTyped(const QString& text) {
    QObject* obj = sender();
    for(int i = 0; i < argInputList.size(); i++){
        if(obj == argInputList[i]){
            if(text.size() > 0){
                isVarList[i]->setDisabled(false);
            }
            else{
                isVarList[i]->setCheckState(Qt::Unchecked);
                isVarList[i]->setDisabled(true);
            }
        }
    }
}

void VerificationValidationWidget::isVarClicked(int state) {
    QObject* obj = sender();
    for(int i = 0; i < isVarList.size(); i++){
        if(obj == isVarList[i]){
            if(state == 2){
                varInputList[i]->setDisabled(false);
            }
            if(state == 0){
                varInputList[i]->setDisabled(true);
            }
        }
    }
}

void VerificationValidationWidget::addArgForm() {
    int n = argForms.size()+1;
    QString boxTitle = "Argument Input %1";
    QGroupBox* argField = new QGroupBox(boxTitle.arg(n));
    QFormLayout* argForm = new QFormLayout();
    QLineEdit* argInput = new QLineEdit();
    argForm->addRow("Argument: ", argInput);
    QCheckBox* isVar = new QCheckBox();
    argForm->addRow("Has variable: ", isVar);
    isVar->setDisabled(true);
    QLineEdit* varInput = new QLineEdit();
    argForm->addRow("Variable: ", varInput);
    varInput->setDisabled(true);
    argInputList.push_back(argInput);
    isVarList.push_back(isVar);
    varInputList.push_back(varInput);

    argField->setLayout(argForm);
    argField->setMinimumWidth(250);
    argLayout->addWidget(argField);

    argForms.push_back(argField);

    connect(argInput, SIGNAL(textChanged(const QString&)), this, SLOT(isArgTyped(const QString&)));
    connect(isVar, SIGNAL(stateChanged(int)), this, SLOT(isVarClicked(int)));
}

void VerificationValidationWidget::rmvArgForm() {
    int n = argForms.size();
    if (!n) return;

    QGroupBox* tmp = argForms[argForms.size()-1];
    argLayout->removeWidget(tmp);
    tmp->setVisible(false);
    content_widget->setLayout(argLayout);
    content_widget->setVisible(false);
    content_widget->setVisible(true);
    qApp->processEvents();

    argInputList.pop_back();
    isVarList.pop_back();
    varInputList.pop_back();
    argForms.pop_back();
}

void VerificationValidationWidget::showNewTestDialog() {
    argInputList.clear();
    isVarList.clear();
    varInputList.clear();

    QDialog* newTestDialog = new QDialog();
    QGridLayout* grid = new QGridLayout();
    
    QGroupBox* groupbox1 = new QGroupBox("Main Info");
    QVBoxLayout* v_layout = new QVBoxLayout();
    
    QFormLayout* mainForm = new QFormLayout();
    testNameInput = new QLineEdit();
    mainForm->addRow("Test Name: ", testNameInput);
    testCmdInput = new QLineEdit();
    mainForm->addRow("Test Command: ", testCmdInput);
    testCategoryInput = new QLineEdit();
    mainForm->addRow("Test Category: ", testCategoryInput);
    v_layout->addLayout(mainForm);

    v_layout->addSpacing(15);
    v_layout->addWidget(new QLabel("Select test suite to add test to"));
    addToSuiteList = new QListWidget();
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->exec("Select suiteName from TestSuites ORDER by id ASC");
    QStringList testSuites;
    while(q->next()){
    	testSuites << q->value(0).toString();
    }
    addToSuiteList->addItems(testSuites);
    QListWidgetItem* item = 0;
    for (int i = 0; i < addToSuiteList->count(); i++) {
        item = addToSuiteList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
    }
    v_layout->addWidget(addToSuiteList);
    groupbox1->setLayout(v_layout);

    QGroupBox* groupbox2 = new QGroupBox("Additional Info");
    QVBoxLayout* v_layout2 = new QVBoxLayout();
    QHBoxWidget* h_widget = new QHBoxWidget();
    QPushButton* addArgFormBtn = new QPushButton("Add Arg");
    QPushButton* rmvArgFormBtn = new QPushButton("Remove Arg");
    v_layout2->addSpacing(10);
    h_widget->addWidget(addArgFormBtn);
    h_widget->addWidget(rmvArgFormBtn);
    v_layout2->addWidget(h_widget);
    v_layout2->addSpacing(20);
    v_layout2->addSpacing(5);
    QScrollArea* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    content_widget = new QWidget();
    argLayout = new QHBoxLayout();
    content_widget->setLayout(argLayout);
    content_widget->setStyleSheet("QWidget { background: transparent; }");
    scroll->setWidget(content_widget);
    scroll->setMinimumWidth(275);
    v_layout2->addWidget(scroll);
    groupbox2->setLayout(v_layout2);
    
    QGroupBox* groupbox3 = new QGroupBox();
    QHBoxLayout* hbox = new QHBoxLayout();
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonOptions->button(QDialogButtonBox::Ok)->setText("Create");
    hbox->addWidget(buttonOptions);
    groupbox3->setLayout(hbox);

    grid->addWidget(groupbox1, 0, 0);
    grid->addWidget(groupbox2, 0, 1);
    grid->addWidget(groupbox3, 1, 0, 1, 2);
    newTestDialog->setLayout(grid);
    newTestDialog->setModal(true);
    newTestDialog->setWindowTitle("Create New Test");
    connect(addArgFormBtn, SIGNAL(clicked()), this, SLOT(addArgForm()));
    connect(rmvArgFormBtn, SIGNAL(clicked()), this, SLOT(rmvArgForm()));
    connect(buttonOptions, &QDialogButtonBox::accepted, newTestDialog, &QDialog::accept);
    connect(buttonOptions, SIGNAL(accepted()), this, SLOT(createTest()));
    connect(buttonOptions, &QDialogButtonBox::rejected, newTestDialog, &QDialog::reject);

    newTestDialog->exec();
    argForms.clear();
}

void VerificationValidationWidget::removeTests() {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    for(int i = 0; i < rmTestList->count(); i++){
        QListWidgetItem* item = rmTestList->item(i);
        if(item->checkState()){
            QString testName = item->text().replace(" (default)", "");
            q->prepare("SELECT id FROM Tests WHERE testName = ?");
            q->addBindValue(testName);
            q->exec();
            int testID = -1;
            while(q->next()){
                testID = q->value(0).toInt();
            }
            q->prepare("DELETE FROM Tests WHERE id = ?");
            q->addBindValue(testID);
            q->exec();
            q->prepare("DELETE FROM TestsInSuite WHERE testID = ?");
            q->addBindValue(testID);
            q->exec();
            q->prepare("DELETE FROM TestArg WHERE testID = ?");
            q->addBindValue(testID);
            q->exec();

            itemToTestMap.erase(item);
            idToItemMap.erase(testID);
        }
    }
    setupUI();
}

void VerificationValidationWidget::showRemoveTestDialog(){
    QDialog* rmTestDialog = new QDialog();
    QVBoxLayout* v_layout = new QVBoxLayout();
    QHBoxLayout* h_layout = new QHBoxLayout();
    QLineEdit* searchBox = new QLineEdit();

    h_layout->addWidget(new QLabel("Search: "));
    h_layout->addWidget(searchBox);
    v_layout->addLayout(h_layout);
    rmTestList = new QListWidget();
    addItemFromTest(rmTestList);

    v_layout->addWidget(rmTestList);
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonOptions->button(QDialogButtonBox::Ok)->setText("Remove");
    v_layout->addWidget(buttonOptions);
    rmTestDialog->setLayout(v_layout);
    rmTestDialog->setModal(true);
    rmTestDialog->setWindowTitle("Select Tests To Remove");
    connect(searchBox, SIGNAL(textEdited(const QString &)), this, SLOT(searchTests_rm(const QString &)));
    connect(buttonOptions, &QDialogButtonBox::accepted, rmTestDialog, &QDialog::accept);
    connect(buttonOptions, SIGNAL(accepted()), this, SLOT(removeTests()));
    connect(buttonOptions, &QDialogButtonBox::rejected, rmTestDialog, &QDialog::reject);

    rmTestDialog->exec();
}

void VerificationValidationWidget::createSuite() {
    if(suiteNameBox->text().simplified().isEmpty()){
        popup("[Verification & Validation]\nERROR: cannot create a test suite with empty name");
        showNewTestSuiteDialog();
        return;
    }

    QString suiteName = suiteNameBox->text();

    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT id FROM TestSuites WHERE suiteName = ?");
    q->addBindValue(suiteName);
    q->exec();
    if(q->next()){
        popup("[Verification & Validation]\nERROR: cannot create a test suite with duplicate name");
        showNewTestSuiteDialog();
        return;
    }

    q->prepare("INSERT OR IGNORE INTO TestSuites VALUES (NULL, ?)");
    q->addBindValue(suiteName);
    q->exec();
    QString suiteID = q->lastInsertId().toString();

    for(int i = 0; i < newTSList->count(); i++){
        QListWidgetItem* item = newTSList->item(i);
        if(item->checkState()){
            q->prepare("SELECT id FROM Tests WHERE testName = ?");
            q->addBindValue(item->text());
            q->exec();
            int testID = -1;
            while(q->next()){
                testID = q->value(0).toInt();
            }
            q->prepare("INSERT INTO TestsInSuite (testID, testSuiteID) VALUES (?, ?)");
            q->addBindValue(testID);
            q->addBindValue(suiteID);
            dbExec(q);
        }
    }

    q->exec("Select suiteName from TestSuites ORDER by id ASC");
    QStringList testSuites;
    while(q->next()){
    	testSuites << q->value(0).toString();
    }
    suiteList->clear();
    suiteList->addItems(testSuites);
    QListWidgetItem* item = 0;
    for (int i = 0; i < suiteList->count(); i++) {
        item = suiteList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
    }
}

void VerificationValidationWidget::showNewTestSuiteDialog() {
    QDialog* newTSDialog = new QDialog();
    QVBoxLayout* v_layout = new QVBoxLayout();

    QHBoxLayout* h_layout = new QHBoxLayout();
    suiteNameBox = new QLineEdit();
    h_layout->addWidget(new QLabel("Test Suite Name: "));
    h_layout->addWidget(suiteNameBox);
    v_layout->addLayout(h_layout);

    v_layout->addSpacing(10);

    QVBoxLayout* v_layout1 = new QVBoxLayout();
    QGroupBox* groupbox1 = new QGroupBox("Test List");
    QHBoxLayout* h_layout1 = new QHBoxLayout();
    QLineEdit* searchBox = new QLineEdit();
    h_layout1->addWidget(new QLabel("Search: "));
    h_layout1->addWidget(searchBox);
    v_layout1->addLayout(h_layout1);

    newTSList = new QListWidget();
    addItemFromTest(newTSList);

    v_layout1->addWidget(newTSList);
    groupbox1->setLayout(v_layout1);
    v_layout->addWidget(groupbox1);
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonOptions->button(QDialogButtonBox::Ok)->setText("Create");
    v_layout->addWidget(buttonOptions);
    newTSDialog->setLayout(v_layout);
    newTSDialog->setModal(true);
    newTSDialog->setWindowTitle("Create New Test Suite");
    connect(searchBox, SIGNAL(textEdited(const QString &)), this, SLOT(searchTests_TS(const QString &)));
    connect(buttonOptions, &QDialogButtonBox::accepted, newTSDialog, &QDialog::accept);
    connect(buttonOptions, SIGNAL(accepted()), this, SLOT(createSuite()));
    connect(buttonOptions, &QDialogButtonBox::rejected, newTSDialog, &QDialog::reject);

    newTSDialog->exec();
}

void VerificationValidationWidget::removeSuites() {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    for(int i = 0; i < rmTSList->count(); i++){
        QListWidgetItem* item = rmTSList->item(i);
        if(item->checkState()){
            QString suiteName = item->text();
            q->prepare("SELECT id FROM TestSuites WHERE suiteName = ?");
            q->addBindValue(suiteName);
            q->exec();
            int suiteID = -1;
            while(q->next()){
                suiteID = q->value(0).toInt();
            }
            q->prepare("DELETE FROM TestSuites WHERE id = ?");
            q->addBindValue(suiteID);
            q->exec();
            q->prepare("DELETE FROM TestsInSuite WHERE testSuiteID = ?");
            q->addBindValue(suiteID);
            q->exec();
        }
    }

    q->exec("Select suiteName from TestSuites ORDER by id ASC");
    QStringList testSuites;
    while(q->next()){
    	testSuites << q->value(0).toString();
    }
    suiteList->clear();
    suiteList->addItems(testSuites);
    QListWidgetItem* item = 0;
    for (int i = 0; i < suiteList->count(); i++) {
        item = suiteList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
    }
}

void VerificationValidationWidget::showRemoveTestSuiteDialog() {
    QDialog* rmTSDialog = new QDialog();
    QVBoxLayout* v_layout = new QVBoxLayout();

    rmTSList = new QListWidget();

    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->exec("Select suiteName from TestSuites ORDER by id ASC");
    QStringList testSuites;
    while(q->next()){
    	testSuites << q->value(0).toString();
    }
    rmTSList->addItems(testSuites);
    QListWidgetItem* item = 0;
    for (int i = 0; i < rmTSList->count(); i++) {
        item = rmTSList->item(i);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        item->setFlags(item->flags() &  ~Qt::ItemIsSelectable);
    }

    v_layout->addWidget(rmTSList);
    QDialogButtonBox* buttonOptions = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonOptions->button(QDialogButtonBox::Ok)->setText("Remove");
    v_layout->addWidget(buttonOptions);
    rmTSDialog->setLayout(v_layout);
    rmTSDialog->setModal(true);
    rmTSDialog->setWindowTitle("Select Test Suites To Remove");
    connect(buttonOptions, &QDialogButtonBox::accepted, rmTSDialog, &QDialog::accept);
    connect(buttonOptions, SIGNAL(accepted()), this, SLOT(removeSuites()));
    connect(buttonOptions, &QDialogButtonBox::rejected, rmTSDialog, &QDialog::reject);

    rmTSDialog->exec();
}

void VerificationValidationWidget::userInputDialogUI(QListWidgetItem* test) {
    if(test->toolTip() ==  "Category")
        return;
    if(!itemToTestMap.at(test).second.hasVarArgs())
        return;
    if(!test->checkState())
        return;
    
    userInputDialogUIDC(test);
}

void VerificationValidationWidget::userInputDialogUIDC(QListWidgetItem* test) {
    if(test->toolTip() ==  "Category")
        return;
    if(!itemToTestMap.at(test).second.hasVarArgs())
        return;

    QDialog* userInputDialog = new QDialog();
    userInputDialog->setModal(true);
    userInputDialog->setWindowTitle("Custom Argument Value");

    QVBoxLayout* vLayout = new QVBoxLayout();
    QFormLayout* formLayout = new QFormLayout();

    QString testName = itemToTestMap.at(test).second.testName;
    vLayout->addWidget(new QLabel("Test Name: "+ testName));
    vLayout->addSpacing(5);
    vLayout->addWidget(new QLabel("Test Command: "+ itemToTestMap.at(test).second.getCMD()));
    vLayout->addSpacing(15);

    std::vector<std::tuple<Arg*, QLineEdit*, QString>> inputTuples;
    std::vector<Arg>* argList = &(itemToTestMap.at(test).second.ArgList);
    for(int i = 0; i < argList->size(); i++){
        if(argList->at(i).type == Arg::Type::Dynamic){
            QLineEdit* lineEdit = new QLineEdit(argList->at(i).defaultValue);
            if(testName == DefaultTests::NO_OVERLAPS.testName || testName == DefaultTests::NO_NULL_REGIONS.testName)
                inputTuples.push_back(std::make_tuple(&argList->at(i), lineEdit, DefaultTests::nameToTestMap.at(testName).ArgList.at(i).defaultValue));
            else
                inputTuples.push_back(std::make_tuple(&argList->at(i), lineEdit, ""));
            formLayout->addRow(argList->at(i).argument, lineEdit);
            formLayout->setSpacing(10);
        }
    }
    
    vLayout->addLayout(formLayout);
    QPushButton* setBtn = new QPushButton("Set");
    vLayout->addWidget(setBtn);
    userInputDialog->setLayout(vLayout);

    connect(setBtn, &QPushButton::clicked, [this, test, inputTuples, testName](){
        bool isDefault = true;
        for(const auto& [currentArg, currentLineEdit, defaultVal] : inputTuples){
            if(currentArg->type == Arg::Type::Dynamic){
                currentArg->defaultValue = currentLineEdit->text();
                if (defaultVal != currentLineEdit->text())
                    isDefault = false;
            }
        }

        if(isDefault){
            test->setText(testName+" (default)");
            test->setIcon(QIcon(QPixmap::fromImage(coloredIcon(":/icons/edit_default.png", "$Color-IconEditVVArg"))));
        } else {
            test->setText(testName);
            test->setIcon(QIcon(QPixmap::fromImage(coloredIcon(":/icons/edit.png", "$Color-IconEditVVArg"))));
        }
        test->setToolTip(itemToTestMap.at(test).second.getCMD());
    });
    
    connect(setBtn, &QPushButton::clicked, userInputDialog, &QDialog::accept);
    userInputDialog->exec();
}

void VerificationValidationWidget::resizeEvent(QResizeEvent* event) {
    resultTable->setColumnWidth(RESULT_CODE_COLUMN, this->width() * 0.025);
    resultTable->setColumnWidth(TEST_NAME_COLUMN, this->width() * 0.175);
    resultTable->setColumnWidth(DESCRIPTION_COLUMN, this->width() * 0.35);
    resultTable->setColumnWidth(OBJECT_COLUMN, this->width() * 0.1);
    resultTable->setColumnWidth(OBJPATH_COLUMN, this->width() * 0.325);

    QHBoxWidget::resizeEvent(event);
}

void VerificationValidationWidget::pathDisplayOptimize(int idx, int oldSize, int newSize){
    if(idx != 4)
        return;
    
    if(!parentDockable->widget()->isVisible())
        return;
    
    QSqlQuery* q = new QSqlQuery(getDatabase());
    
    for(int i = 0; i < resultTable->rowCount(); i++){
        if(resultTable->item(i, idx)){
            q->prepare("SELECT objectName FROM ObjectIssue WHERE id = ?");
            q->addBindValue(resultTable->item(i, ISSUE_ID_COLUMN)->text());
            dbExec(q);
            QString path;
            while(q->next()){
                path = q->value(0).toString();
            }
            
            if(newSize/float(path.size()) < 6.5){
                QStringList dirTree = path.split("/");
                if(dirTree.length() <= 2){
                    continue;
                } else if (dirTree.length() == 3){
                    path = "/.../"+dirTree[2];
                } else {
                    path = "/"+dirTree[1] + "/.../" + dirTree[dirTree.size()-1];
                }
            }
            
            resultTable->item(i, idx)->setText(path);
        }
    }
}

void VerificationValidationWidget::setupUI() {    
    selectTestsDialog = new QDialog();
    testList = new QListWidget();
    suiteList = new QListWidget();
    test_sa = new QListWidget();
    suite_sa = new QListWidget();

    // setup result table's column headers
    QStringList columnLabels;
    columnLabels << "Type" << "Test Name" << "Description" << "Issue Object" << "Full Path";
    resultTable->setColumnCount(columnLabels.size() + 5); // add hidden columns
    resultTable->setHorizontalHeaderLabels(columnLabels);
    resultTable->verticalHeader()->setVisible(false);
    resultTable->horizontalHeader()->setStretchLastSection(true);
    resultTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft);
    
    QHeaderView* header = resultTable->horizontalHeader();
    
    resultTableSortIdx = RESULT_TABLE_IDX_COLUMN;
    connect(header, &QHeaderView::sectionClicked, [this](int idx){
        if(idx == resultTableSortIdx){
            resultTable->horizontalHeaderItem(idx)->setForeground(QBrush(QColor("#4b4b4b")));
            resultTable->sortItems(RESULT_TABLE_IDX_COLUMN, Qt::AscendingOrder);
            resultTableSortIdx = RESULT_TABLE_IDX_COLUMN;
        } else {
            if(resultTableSortIdx != RESULT_TABLE_IDX_COLUMN){
                resultTable->horizontalHeaderItem(resultTableSortIdx)->setForeground(QBrush(QColor("#4b4b4b")));
            }
            resultTable->horizontalHeaderItem(idx)->setForeground(QBrush(Qt::blue));
            if(idx == 0)
                resultTable->sortItems(ERROR_TYPE_COLUMN, Qt::AscendingOrder);
            else
                resultTable->sortItems(idx, Qt::AscendingOrder);
            resultTableSortIdx = idx;
        }
    });

    connect(header, SIGNAL(sectionResized(int,int,int)), this, SLOT(pathDisplayOptimize(int,int,int)));

    addWidget(resultTable);

    // setup terminal
    addWidget(btnCollapseTerminal);

    connect(btnCollapseTerminal, &QPushButton::clicked, this, [this]() {
        if (!terminal) {
            terminal = new MgedWidget(document);
            terminal->setStyleSheet("QTextEdit { background-color: black; color: #39ff14; font-weight: 600}");
            terminal->setVisible(false);
            this->addWidget(terminal);
        }

        terminal->setVisible(!terminal->isVisible());
        if(terminal->isVisible()){
            resultTable->setColumnWidth(RESULT_CODE_COLUMN, this->width() * 0.025);
            resultTable->setColumnWidth(TEST_NAME_COLUMN, this->width() * 0.075);
            resultTable->setColumnWidth(DESCRIPTION_COLUMN, this->width() * 0.225);
            resultTable->setColumnWidth(OBJECT_COLUMN, this->width() * 0.1);
            resultTable->setColumnWidth(OBJPATH_COLUMN, this->width() * 0.05);
        } else {
            resultTable->setColumnWidth(RESULT_CODE_COLUMN, this->width() * 0.025);
            resultTable->setColumnWidth(TEST_NAME_COLUMN, this->width() * 0.175);
            resultTable->setColumnWidth(DESCRIPTION_COLUMN, this->width() * 0.35);
            resultTable->setColumnWidth(OBJECT_COLUMN, this->width() * 0.1);
            resultTable->setColumnWidth(OBJPATH_COLUMN, this->width() * 0.325);
        }
    });

    QSqlQuery* query = new QSqlQuery(getDatabase());
    addItemFromTest(testList);

    // Get suite list from db
    query->exec("Select suiteName from TestSuites ORDER by id ASC");
    QStringList testSuites;
    while(query->next()){
    	testSuites << query->value(0).toString();
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
    QLineEdit* searchBox = new QLineEdit("");
    searchBar->addWidget(searchLabel);
    searchBar->addWidget(searchBox);
	
    // format and populate Select Tests dialog box
    selectTestsDialog->setModal(true);
    selectTestsDialog->setWindowTitle("Select Tests To Run");
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
    buttonOptions->button(QDialogButtonBox::Ok)->setText("Run");
    QHBoxLayout* hbox = new QHBoxLayout();
    hbox->addWidget(new QLabel("Warning: running tests will overwrite your current results."));
    hbox->addWidget(buttonOptions);
    groupbox3->setLayout(hbox);
    
    grid->addWidget(groupbox1, 0, 0);
    grid->addWidget(groupbox2, 0, 1);
    grid->addWidget(groupbox3, 1, 0, 1, 2);
    selectTestsDialog->setLayout(grid);

    resultTable->setShowGrid(false);
    resultTable->setStyleSheet("QTableWidget::item {border-bottom: 0.5px solid #3C3C3C;}");
    resultTable->setColumnHidden(RESULT_TABLE_IDX_COLUMN, true);
    resultTable->setColumnHidden(ERROR_TYPE_COLUMN, true);
    resultTable->setColumnHidden(ISSUE_ID_COLUMN, true);
    resultTable->setColumnHidden(TEST_RESULT_ID_COLUMN, true);
    resultTable->setColumnHidden(OBJECT_TESTED_COLUMN, true);
    resultTable->setContextMenuPolicy(Qt::CustomContextMenu);

    // *******************

    QSqlQuery* query2 = new QSqlQuery(getDatabase());

    QString str = "0";
    query->prepare("SELECT testID FROM RunningTests WHERE hasFinished = ?");
    query->addBindValue(str);
    query->exec();

    if (query->next()) {

        QMessageBox msgBox;
        msgBox.setText("This file was previously closed while running tests. Would you like to continue them?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::Yes);

        if (msgBox.exec() == QMessageBox::Yes) {
            hasUnfinishedTests = true;
            do {
                query2->prepare("SELECT testName FROM Tests WHERE id = ?");
                query2->addBindValue(query->value(0));
                query2->exec();

                if  (query2->next()) {
                    QList<QListWidgetItem *> item = testList->findItems(query2->value(0).toString(), Qt::MatchExactly);
                    item.at(0)->setCheckState(Qt::Checked);
                }

            } while (query->next());

            testStartAndThreadSetUp();
        }
    }

    //************
	
    // setup signal to allow updating of V&V Action's icons
    connect(this, &VerificationValidationWidget::updateVerifyValidateAct, mainWindow, &MainWindow::updateVerifyValidateAct);
    // Select all signal connect function
    connect(suite_sa, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateSuiteSelectAll(QListWidgetItem *)));
    connect(test_sa, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateTestSelectAll(QListWidgetItem *)));
    // Suite select signal connect function
    connect(suiteList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(updateTestListWidget(QListWidgetItem *)));
    // Test select signal connect function
    connect(testList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(testListSelection(QListWidgetItem*)));
    // Search button pressed signal select function
    connect(searchBox, SIGNAL(textEdited(const QString &)), this, SLOT(searchTests_run(const QString &)));
    // Test input for gqa
    connect(testList, SIGNAL(itemClicked(QListWidgetItem *)), this, SLOT(userInputDialogUI(QListWidgetItem *)));
    connect(testList, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(userInputDialogUIDC(QListWidgetItem *)));
    // Run test & exit
    connect(buttonOptions, &QDialogButtonBox::accepted, selectTestsDialog, &QDialog::accept);
    connect(buttonOptions, SIGNAL(accepted()), this, SLOT(testStartAndThreadSetUp()));
    connect(buttonOptions, &QDialogButtonBox::rejected, selectTestsDialog, &QDialog::reject);
    // Open details dialog
    connect(resultTable, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(setupResultMenu(const QPoint&)));
    connect(resultTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        QList<QTableWidgetItem*> list = resultTable->selectedItems();
        QList<QTableWidgetItem*> selectedItems;
        for (int i = 0; i < list.size(); i++) {
            if (list.at(i)->column() == TEST_NAME_COLUMN)
                selectedItems.append(list.at(i));
        }
        if (!selectedItems.size()) return;
        visualizeObjects(selectedItems);
    });
}

void VerificationValidationWidget::testStartAndThreadSetUp() {
    QSqlQuery* query = new QSqlQuery(getDatabase());
    QSqlQuery* query2 = new QSqlQuery(getDatabase());

    // preprocess stuff everytime running tests
    validateChecksum();
    dbUpdateModelUUID();
    minBtn_toggle = true;

    // get tests + do checks + UI changes
    QList<QListWidgetItem *> selected_tests = getSelectedTests();
    int totalTests = selected_tests.count();
    if (!totalTests)
    {
        popup("No tests were selected.");
        return;
    }
    resultTableChangeSize();

    // Do conditional logic for if the program was closed while running tests
    QStringList selectedObjects;
    if (hasUnfinishedTests == false) {
        dbClearResults();
        resultTable->setRowCount(0);
        selectedObjects = document->getObjectTreeWidget()->getSelectedObjects(ObjectTreeWidget::Name::PATHNAME, ObjectTreeWidget::Level::ALL);

        // POPULATE SELECTED OBJECTS TABLE
        for (QString object : selectedObjects) {
            query->prepare("INSERT INTO ObjectTree (object) VALUES (?)");
            query->addBindValue(object);
            query->exec();
        }

        // POPULATE RUNNING TEST TABLE 
        for (QListWidgetItem* item : selected_tests) {
            query->prepare("SELECT id FROM Tests WHERE testName = ?");
            query->addBindValue(item->text());
            query->exec();

            int str = 0;
            query->first();
            query2->prepare("INSERT INTO RunningTests (testID, hasFinished) VALUES (?, ?)");
            query2->addBindValue(query->value(0));
            query2->addBindValue(str);
            query2->exec();
        }
    }
    else {
        // POPULATE selectedObjects
        query->prepare("SELECT object FROM ObjectTree");
        query->exec();

        while (query->next())
            selectedObjects.push_back(query->value(0).toString());
    }

    // spin up new thread and get to work
    mgedWorkerThread = new MgedWorker(selected_tests, selectedObjects, totalTests, itemToTestMap, modelID, *(document->getFilePath()));

    // signal that allows for updating of MainWindow's status bar
    connect(mgedWorkerThread, QOverload<bool, int, int, int, int>::of(&MgedWorker::updateStatusBarRequest),
            mainWindow, QOverload<bool, int, int, int, int>::of(&MainWindow::setStatusBarMessage));

    // signal that allows for updating of progress bar from thread
    connect(mgedWorkerThread, &MgedWorker::updateProgressBarRequest, this, [this](const int &currTest, const int &totalTests)
            {
            if (!vvProgressBar) return;
            if (currTest < 0 || totalTests <= 0) vvProgressBar->setVisible(false);
            else vvProgressBar->setVisible(true);
            int newVal = (totalTests) ? ceil(currTest * 100 / (float)totalTests) : 0;
            vvProgressBar->setValue(newVal); });

    // signal that allows Verification Validation Widget's result table to be updated via thread
    connect(mgedWorkerThread, &MgedWorker::showResultRequest, this, &VerificationValidationWidget::showResult, Qt::BlockingQueuedConnection);

    // signals that allows V&V Widget's database to be updated from thread
    // note: must be blocking
    connect(mgedWorkerThread, QOverload<const QString &, const QStringList &, QList<QList<QVariant>> *, const int &>::of(&MgedWorker::queryRequest),
            this, QOverload<const QString &, const QStringList &, QList<QList<QVariant>> *, const int &>::of(&VerificationValidationWidget::performQueryRequest),
            Qt::BlockingQueuedConnection);

    connect(mgedWorkerThread, QOverload<const QString &, const QStringList &, QString &>::of(&MgedWorker::queryRequest),
            this, QOverload<const QString &, const QStringList &, QString &>::of(&VerificationValidationWidget::performQueryRequest),
            Qt::BlockingQueuedConnection);

    // thread finish -> cleanup
    connect(mgedWorkerThread, &MgedWorker::finished, this, [this]()
            {
            this->runningTests = false;
            emit updateVerifyValidateAct(this->document);

            mgedWorkerThread->deleteLater();
            mgedWorkerThread = nullptr; });

    this->runningTests = true;
    emit updateVerifyValidateAct(this->document);
    mgedWorkerThread->start();
    hasUnfinishedTests = false;
}

QSqlQuery* VerificationValidationWidget::dbExec(QString command, bool showErrorPopup) {
    QSqlDatabase db = getDatabase();
    QSqlQuery* query = new QSqlQuery(command, db);
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
    delete dbExec("DELETE FROM RunningTests");
    delete dbExec("DELETE FROM ObjectTree");
}

void VerificationValidationWidget::copyToClipboard(QTableWidgetItem* item) {
    clipboard = QApplication::clipboard();
    QTableWidgetItem *objPathItem = resultTable->item(item->row(), ISSUE_ID_COLUMN);
    QString objPath = "";
    if(objPathItem){
        QSqlQuery* q = new QSqlQuery(getDatabase());
        
        q->prepare("SELECT objectName FROM ObjectIssue WHERE id = ?");
        q->addBindValue(objPathItem->text());
        dbExec(q);
        while(q->next()){
            objPath = q->value(0).toString();
        }
    }

    clipboard->setText(objPath);
}

void VerificationValidationWidget::setupResultMenu(const QPoint& pos) {
    QList<QTableWidgetItem*> list = resultTable->selectedItems();
    QList<QTableWidgetItem*> selectedItems;
    for(int i = 0; i < list.size(); i++) {
        if (list.at(i)->column() == TEST_NAME_COLUMN) {
            selectedItems.append(list.at(i));
            break;
        }
    }
    QMenu *resultMenu = new QMenu();
    if(selectedItems.size() == 1) {
        resultMenu->addAction("Test Result Details", this, [this, selectedItems]{
            setupDetailedResult(selectedItems.at(0));
        });
        resultMenu->addAction("Copy Path", this, [this, selectedItems]{
            copyToClipboard(selectedItems.at(0));
        });
    }
    else {
        resultMenu->addAction("No known actions.", this, [] {});
    }

    resultMenu->exec(QCursor::pos());
}

void VerificationValidationWidget::setupDetailedResult(QTableWidgetItem* item) {
    QDialog* detail_dialog = new QDialog();
    detail_dialog->setModal(true);
    detail_dialog->setWindowTitle("Test Result Details");

    QVBoxLayout* detailLayout = new QVBoxLayout();

    QString resultCode;
    
    QTableWidgetItem* testNameItem = resultTable->item(item->row(), TEST_NAME_COLUMN);
    QTableWidgetItem* descriptionItem = resultTable->item(item->row(), DESCRIPTION_COLUMN);
    QTableWidgetItem* objPathItem = resultTable->item(item->row(), OBJPATH_COLUMN);
    QTableWidgetItem* testResultItem = resultTable->item(item->row(), TEST_RESULT_ID_COLUMN);
    QTableWidgetItem* objectTestedItem = resultTable->item(item->row(), OBJECT_TESTED_COLUMN);

    QString testName = (testNameItem) ? testNameItem->text() : "";
    QString description = (descriptionItem) ? descriptionItem->text() : "";
    QString objPath = (objPathItem) ? objPathItem->text() : "";
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT id FROM Tests WHERE testName = ?");
    q->addBindValue(testName);
    dbExec(q);
    if (!q->next()) { return; }
    int testID = q->value(0).toInt();
    Test currentTest = itemToTestMap.at(idToItemMap.at(testID)).second;
    
    
    QString objectTested = (objectTestedItem) ? objectTestedItem->text() : "";
    QString testCommand = currentTest.getCMD(objectTested);

    QString testResultID = (testResultItem) ? testResultItem->text() : "";

    q->prepare("SELECT terminalOutput, resultCode FROM TestResults WHERE id = ?");
    q->addBindValue(testResultID);

    dbExec(q);
    if (!q->next()) {
        popup("Failed to show testResultID: " + testResultID);
        return;
    }

    QString terminalOutput = q->value(0).toString();
    int code = q->value(1).toInt();
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
    detailLayout->addWidget(new QLabel(testName));
    detailLayout->addSpacing(10);
    detailLayout->addWidget(commandHeader);
    QLabel* testCmdLabel = new QLabel(testCommand);
    testCmdLabel->setFixedWidth(750);
    detailLayout->addWidget(testCmdLabel);
    detailLayout->addSpacing(10);
    detailLayout->addWidget(resultCodeHeader);
    detailLayout->addWidget(new QLabel(resultCode));
    detailLayout->addSpacing(10);
    detailLayout->addWidget(descriptionHeader);
    detailLayout->addWidget(new QLabel(description));
    detailLayout->addSpacing(10);
    detailLayout->addWidget(rawOutputHeader);

    QTextEdit* rawOutputBox = new QTextEdit();
    QPalette rawOutputBox_palette = rawOutputBox->palette();
    rawOutputBox_palette.setColor(QPalette::Base, Qt::black);
    rawOutputBox->setPalette(rawOutputBox_palette);
    rawOutputBox->setFontWeight(QFont::DemiBold);
    rawOutputBox->setTextColor(QColor("#39ff14"));
    rawOutputBox->append("mged> "+testCommand+"\n");
    rawOutputBox->setTextColor(Qt::white);
    rawOutputBox->append(terminalOutput);
    rawOutputBox->setReadOnly(true);
    rawOutputBox->moveCursor(QTextCursor::Start, QTextCursor::MoveAnchor);

    detailLayout->addWidget(rawOutputBox);
    detail_dialog->setLayout(detailLayout);
    detail_dialog->exec();
}

void VerificationValidationWidget::visualizeObjects(QList<QTableWidgetItem*> items) {
    QTableWidgetItem* testNameItem;
    QTableWidgetItem* descriptionItem;
    QTableWidgetItem* objPathItem;

    QString testName;
    QString description;
    QString objPath;
    QString objName, objName2;
    int testID;
    int code;
    int idxLastSlash;
    QStringList splitString;

    QList<QString> objNames;
    QList<int> codes;

    QSqlQuery* q = new QSqlQuery(getDatabase());
    ObjectTree *objTree = document->getObjectTree();
    QHash<int, QString> nameMap = document->getObjectTree()->getNameMap();
    QHashIterator<int, QString> iter1(nameMap);
    QHashIterator<int, QString> iter2(nameMap);

    for(int i = 0; i < items.size(); i++) {
        testNameItem = resultTable->item(items.at(i)->row(), TEST_NAME_COLUMN);
        descriptionItem = resultTable->item(items.at(i)->row(), DESCRIPTION_COLUMN);
        objPathItem = resultTable->item(items.at(i)->row(), OBJPATH_COLUMN);

        testName = (testNameItem) ? testNameItem->text() : "";
        description = (descriptionItem) ? descriptionItem->text() : "";
        objPath = (objPathItem) ? objPathItem->text() : "";

        q->prepare("SELECT id FROM Tests WHERE testName = ?");
        q->addBindValue(testName);
        dbExec(q);
        if (!q->next()) {
            popup("Failed to show testName: " + testName);
            return;
        }

        testID = q->value(0).toInt();

        q->prepare("SELECT resultCode FROM TestResults WHERE testID = ?");
        q->addBindValue(testID);
        dbExec(q);
        if (!q->next()) {
            popup("Failed to show testID: " + testID);
            return;
        }

        code = q->value(0).toInt();
        codes.append(code);

        if((code == Result::Code::WARNING || code == Result::Code::FAILED) && testName == DefaultTests::NO_OVERLAPS.testName) {
            splitString = description.split('\'');
            objName = splitString[1];
            objName2 = splitString[3];
            objNames.append(objName);
            objNames.append(objName2);
        }
        else {
            idxLastSlash = objPath.lastIndexOf('/');
            if(idxLastSlash == -1) continue;
            objName = objPath.mid(idxLastSlash + 1, objPath.size() - idxLastSlash - 1);
            objNames.append(objName);
        }
    }

    while(iter1.hasNext()) {
        iter1.next();
        objTree->changeVisibilityState(iter1.key(), false);
    }
    while(iter2.hasNext()) {
        iter2.next();
        if(objNames.contains(iter2.value())) {
            objTree->changeVisibilityState(iter2.key(), true);
        }
    }

    document->getDisplay()->getCamera()->autoview();
    document->getGeometryRenderer()->refreshForVisibilityAndSolidChanges();
    document->getDisplayGrid()->forceRerenderAllDisplays();
    document->getObjectTreeWidget()->refreshItemTextColors();
    document->getDisplay()->forceRerenderFrame();
}

void VerificationValidationWidget::showResult(const QString& testResultID) {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT Tests.testName, TestResults.resultCode, TestResults.terminalOutput, TestResults.objectArgID FROM Tests INNER JOIN TestResults ON Tests.id=TestResults.testID WHERE TestResults.id = ?");
    q->addBindValue(testResultID);
    dbExec(q);

    if (!q->next()) {
        popup("Failed to show Test Result #" + testResultID);
        return;
    }

    QString testName = q->value(0).toString();
    int resultCode = q->value(1).toInt();
    QString terminalOutput = q->value(2).toString();
    int objectArgID = q->value(3).toInt();
    QString testedObject = "";

    q->prepare("SELECT arg FROM TestArg WHERE id = ?");
    q->addBindValue(objectArgID);
    dbExec(q);

    if (!q->next()) popup("Failed get testedObject -- using default");
    else testedObject = q->value(0).toString();

    QString iconPath = "";
    QString objectName;
    QString issueDescription;

    if (resultCode == Result::Code::PASSED) {
        resultTable->insertRow(resultTable->rowCount());
        iconPath = ":/icons/passed.png";
        resultTable->setItem(resultTable->rowCount()-1, RESULT_CODE_COLUMN, new QTableWidgetItem(QIcon(iconPath), ""));
        resultTable->setItem(resultTable->rowCount()-1, TEST_NAME_COLUMN, new QTableWidgetItem(testName));
        resultTable->setItem(resultTable->rowCount()-1, DESCRIPTION_COLUMN, new QTableWidgetItem("Passed"));
        resultTable->setItem(resultTable->rowCount()-1, RESULT_TABLE_IDX_COLUMN, new QTableWidgetItem(QString::number(resultTable->rowCount()-1)));
        resultTable->setItem(resultTable->rowCount()-1, ERROR_TYPE_COLUMN, new QTableWidgetItem(QString::number(4)));
        resultTable->setItem(resultTable->rowCount()-1, TEST_RESULT_ID_COLUMN, new QTableWidgetItem(testResultID));
        resultTable->setItem(resultTable->rowCount()-1, OBJECT_TESTED_COLUMN, new QTableWidgetItem(testedObject));
    } 

    else if (resultCode == Result::Code::UNPARSEABLE) {
        resultTable->insertRow(resultTable->rowCount());
        iconPath = ":/icons/unparseable.png";
        resultTable->setItem(resultTable->rowCount()-1, RESULT_CODE_COLUMN, new QTableWidgetItem(QIcon(iconPath), ""));
        resultTable->setItem(resultTable->rowCount()-1, TEST_NAME_COLUMN, new QTableWidgetItem(testName));
        resultTable->setItem(resultTable->rowCount()-1, DESCRIPTION_COLUMN, new QTableWidgetItem("Check Test Result Details for terminal output"));
        resultTable->setItem(resultTable->rowCount()-1, RESULT_TABLE_IDX_COLUMN, new QTableWidgetItem(QString::number(resultTable->rowCount()-1)));
        resultTable->setItem(resultTable->rowCount()-1, ERROR_TYPE_COLUMN, new QTableWidgetItem(QString::number(3)));
        resultTable->setItem(resultTable->rowCount()-1, TEST_RESULT_ID_COLUMN, new QTableWidgetItem(testResultID));
        resultTable->setItem(resultTable->rowCount()-1, OBJECT_TESTED_COLUMN, new QTableWidgetItem(testedObject));
    }

    else {
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

            objectName = q3->value(0).toString();
            issueDescription = q3->value(1).toString().replace("\n", "");

            resultTable->insertRow(resultTable->rowCount());
            
            int error_type;
            if (resultCode == VerificationValidation::Result::Code::FAILED){
                iconPath = ":/icons/error.png";
                error_type = 1;
            }
            else if (resultCode == VerificationValidation::Result::Code::WARNING){
                iconPath = ":/icons/warning.png";
                error_type = 2;
            }

            QStringList objectTree = objectName.split("/");

            // Change to hide icon image path from showing
            resultTable->setItem(resultTable->rowCount()-1, RESULT_CODE_COLUMN, new QTableWidgetItem(QIcon(iconPath), ""));
            resultTable->setItem(resultTable->rowCount()-1, TEST_NAME_COLUMN, new QTableWidgetItem(testName));
            resultTable->setItem(resultTable->rowCount()-1, DESCRIPTION_COLUMN, new QTableWidgetItem(issueDescription));
            resultTable->setItem(resultTable->rowCount()-1, OBJECT_COLUMN, new QTableWidgetItem(objectTree.at(objectTree.size()-1)));
            resultTable->setItem(resultTable->rowCount()-1, OBJPATH_COLUMN, new QTableWidgetItem(objectName));
            resultTable->setItem(resultTable->rowCount()-1, RESULT_TABLE_IDX_COLUMN, new QTableWidgetItem(QString::number(resultTable->rowCount()-1)));
            resultTable->setItem(resultTable->rowCount()-1, ERROR_TYPE_COLUMN, new QTableWidgetItem(QString::number(error_type)));
            resultTable->setItem(resultTable->rowCount()-1, ISSUE_ID_COLUMN, new QTableWidgetItem(objectIssueID));
            resultTable->setItem(resultTable->rowCount()-1, TEST_RESULT_ID_COLUMN, new QTableWidgetItem(testResultID));
            resultTable->setItem(resultTable->rowCount()-1, OBJECT_TESTED_COLUMN, new QTableWidgetItem(testedObject));

            delete q3;
        }
        delete q2;
    }
    
    // Only select rows, disable edit
    resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    delete q;
    qApp->processEvents();
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

void VerificationValidationWidget::validateChecksum() {
    QSqlQuery* q = dbExec("SELECT uuid FROM Model");
    if (!q->next()) { popup("Failed to validate checksum (failed get UUID from Model)"); return; }
    QString uuid = q->value(0).toString();
    delete q;

    QString gFilePath = *document->getFilePath();
    QString* gFileUUID = generateUUID(gFilePath);
    if (!gFileUUID) { popup("Failed to validate checksum (failed generate UUID for " + gFilePath + ")"); return; }

    QMessageBox msgBox;
    if (uuid != *gFileUUID) {
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("The contents of " + gFilePath + " have changed.\n\nChecksums:\nold: " + uuid + "\nnew: " + *gFileUUID);
        msgBox.setStandardButtons(QMessageBox::Ok);
        msgBox.setDefaultButton(QMessageBox::Ok);
        msgBox.exec();
        dbUpdateModelUUID();
    }
}

void VerificationValidationWidget::dbUpdateModelUUID() {
    QString* uuid = generateUUID(*document->getFilePath());
    if (!uuid) return;
    QSqlQuery* updateQuery = new QSqlQuery(getDatabase());
    updateQuery->prepare("UPDATE Model SET uuid = ? WHERE id = ?");
    updateQuery->addBindValue(*uuid);
    updateQuery->addBindValue(modelID);
    updateQuery->exec();
    delete updateQuery;

    updateDockableHeader();
}

void VerificationValidationWidget::resultTableChangeSize() {
    if(minBtn_toggle){
        minBtn->setIcon(QIcon(":/icons/collapse.png"));
        parentDockable->widget()->setVisible(true);
        minBtn_toggle = false;
    } else {
        minBtn->setIcon(QIcon(":/icons/expand.png"));
        parentDockable->widget()->setVisible(false);
        minBtn_toggle = true;
    }
}

void VerificationValidationWidget::updateDockableHeader() {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare("SELECT uuid, filePath FROM Model WHERE id = ?");
    q->addBindValue(modelID);
    q->exec();
    if (q->next()) {
        // fetch from DB
        QString uuid = q->value(0).toString();
        QString filePath = q->value(1).toString();

        // craft dockable title
        QString dockableTitle = "Verification & Validation\tFile Path: "+filePath+" \tModel UUID: "+uuid;
        QLabel *title = new QLabel(dockableTitle);
        title->setStyleSheet("QLabel { background: transparent; }");
        title->setObjectName("dockableHeader");

        minBtn = new QToolButton();
        minBtn->setIcon(QIcon(":/icons/expand.png"));
        minBtn_toggle = true;

        // put together dockable title + minBtn for top row
        QHBoxWidget* topRow = new QHBoxWidget;
        topRow->addWidget(title);
        topRow->addWidget(minBtn);

        // progress bar when running tests
        vvProgressBar = new QProgressBar;
        vvProgressBar->setOrientation(Qt::Horizontal);
        vvProgressBar->setRange(0, 100);
        vvProgressBar->setVisible(false);
        vvProgressBar->setStyleSheet("QProgressBar::chunk {background: #00CA00;}");

        QVBoxWidget* titleWidget = new QVBoxWidget;
        titleWidget->addWidget(topRow);
        titleWidget->addWidget(vvProgressBar);

        parentDockable->setTitleBarWidget(titleWidget);
        parentDockable->widget()->setVisible(false);
        qApp->processEvents();

        connect(minBtn, SIGNAL(clicked()), this, SLOT(resultTableChangeSize()));
    }
    delete q;    
}

QList<QListWidgetItem*> VerificationValidationWidget::getSelectedTests() {
    // Get list of checked tests
    QList<QListWidgetItem*> selected_tests;
    QListWidgetItem* item = 0;
    for (int i = 0; i < testList->count(); i++) {
        item = testList->item(i);
        if (item->checkState()) {
            selected_tests.push_back(item);
        }
    }

    return selected_tests;
}

void MgedWorker::run() {
    QSet<QString> previouslyRunTests; // don't run duplicate tests (e.g.: "title" for each object)
    for (int objIdx = 0; objIdx < selectedObjects.size(); objIdx++) {
        QString object = selectedObjects[objIdx];
        for (int i = 0; i < totalTests; i++) {
            emit updateStatusBarRequest(false, i + 1, totalTests, objIdx + 1, selectedObjects.size());
            emit updateProgressBarRequest((totalTests * objIdx) + i, totalTests * selectedObjects.size());
            int testID = itemToTestMap.at(selected_tests[i]).first;
            Test currentTest = itemToTestMap.at(selected_tests[i]).second;

            if (isInterruptionRequested()) return;
            
            // for the current test, insert any Args that aren't in TestArg
            for (int j = 0; j < currentTest.ArgList.size(); j++) {
                Arg::Type type = currentTest.ArgList[j].type;
                QString arg = currentTest.ArgList[j].argument;
                if (type == Arg::Type::ObjectName) arg = object.split("/").last();
                else if (type == Arg::Type::ObjectPath) arg = object;

                int cnt = 0;
                QList<QList<QVariant>> answer;
                emit queryRequest("SELECT COUNT(*) FROM TestArg WHERE testID = ? AND argIdx = ? AND arg = ? AND argType = ?", 
                    { QString::number(testID), QString::number(currentTest.ArgList[j].argIdx), arg, QString::number((int)type) },
                    &answer, 1);

                if (answer.size() > 0 && answer[0].size() > 0)
                    cnt = answer[0][0].toInt();

                if (!cnt) {
                    emit queryRequest("INSERT INTO TestArg (testID, argIdx, arg, argType, defaultVal) VALUES (?,?,?,?,?)",
                        { QString::number(testID), QString::number(currentTest.ArgList[j].argIdx), arg, QString::number((int)type), currentTest.ArgList[j].defaultValue });
                }
            }

            // find objectArgID associated with this object
            QString objectPlaceholder = object;
            Arg::Type type = currentTest.getObjArgType();
            if (type == Arg::Type::ObjectName)
                objectPlaceholder = objectPlaceholder.split("/").last();
            else if (type == Arg::Type::ObjectNone)
                objectPlaceholder = "";

            QList<QList<QVariant>> answer;
            emit queryRequest("SELECT id FROM TestArg WHERE (argType = ? OR argType = ? or argType = ?) AND arg = ? AND testID = ?",
                { QString::number((int)Arg::Type::ObjectName), QString::number((int)Arg::Type::ObjectPath), QString::number((int)Arg::Type::ObjectNone), objectPlaceholder, QString::number(testID) },
                &answer, 1);

            if (!answer.size() || !answer[0].size()) continue;

            // run tests
            QString objectArgID = answer[0][0].toString();
            QString testCommand = currentTest.getCMD(objectPlaceholder);
            if (previouslyRunTests.contains(testCommand)) continue;
            previouslyRunTests.insert(testCommand);

            const QString terminalOutput = mgedRun(testCommand, gFilePath);

            // Update db with new arg value
            if (itemToTestMap.at(selected_tests[i]).second.hasVarArgs()) {
                std::vector<Arg> newArgs = itemToTestMap.at(selected_tests[i]).second.ArgList;
                for (int j = 0; j < newArgs.size(); j++) {
                    if (newArgs[j].type == Arg::Type::Dynamic) {
                        emit queryRequest("UPDATE TestArg SET defaultVal = ? WHERE testID = ? AND argIdx = ?",
                            { newArgs[j].defaultValue, QString::number(testID), QString::number(newArgs[j].argIdx) });
                    }
                }
            }

            QString executableName = selected_tests[i]->toolTip().split(' ', Qt::SkipEmptyParts).first();
            Result* result = nullptr;

            // find proper parser
            if (QString::compare(executableName, "search", Qt::CaseInsensitive) == 0)
                result = Parser::search(testCommand, terminalOutput, currentTest);
            else if (QString::compare(executableName, "lc", Qt::CaseInsensitive) == 0)
                result = Parser::lc(testCommand, terminalOutput, gFilePath);
            else if (QString::compare(executableName, "gqa", Qt::CaseInsensitive) == 0)
                result = Parser::gqa(testCommand, terminalOutput, currentTest);
            else if (QString::compare(executableName, "title", Qt::CaseInsensitive) == 0)
                result = Parser::title(testCommand, terminalOutput, currentTest);

            // if parser hasn't been implemented, default
            if (!result) {
                result = new Result;
                result->resultCode = Result::Code::UNPARSEABLE;
            }

            QString resultCode = QString::number(result->resultCode);

            // insert results into db
            QString testResultID;
            emit queryRequest("INSERT INTO TestResults (modelID, testID, objectArgID, resultCode, terminalOutput) VALUES (?,?,?,?,?)",
                { modelID, QString::number(testID), objectArgID, resultCode, terminalOutput },
                testResultID);

            // insert issues into db
            for (Result::ObjectIssue currentIssue : result->issues) {
                QString objectIssueID;
                emit queryRequest("INSERT INTO ObjectIssue (objectName, issueDescription) VALUES (?,?)",
                    { currentIssue.objectName, currentIssue.issueDescription },
                    objectIssueID);

                emit queryRequest("INSERT INTO Issues (testResultID, objectIssueID) VALUES (?,?)",
                    { testResultID, objectIssueID });
            }
            emit updateStatusBarRequest(true, i + 1, totalTests, objIdx + 1, selectedObjects.size());
            emit updateProgressBarRequest((totalTests * objIdx) + i + 1, totalTests * selectedObjects.size());
            emit showResultRequest(testResultID);

            // update running list with finished test
            QString str = "1";
            emit queryRequest("UPDATE RunningTests SET hasFinished = ? WHERE testID = ?", 
                { str, QString::number(testID) });
        }
    }

    QList<QList<QVariant>> answer;
    emit queryRequest("SELECT uuid, filePath FROM Model WHERE id = ?",
        { modelID },
        &answer, 2);
    if (!answer.size() || !answer[0].size()) {
        std::cout << "Failed to show modelID " << modelID.toStdString() << std::endl;
        return;
    }
}

void VerificationValidationWidget::performQueryRequest(const QString& query, const QStringList& args, QList<QList<QVariant>>* answer, const int& numAnswersExpected) {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare(query);
    for (const QString& arg : args)
        q->addBindValue(arg);
    dbExec(q);

    if (answer) {
        while (q->next()) {
            QList<QVariant> current;
            for (int i = 0; i < numAnswersExpected; i++) {
                current.append(q->value(i));
            }
            answer->append(current);
        }
    }
    delete q;
}

void VerificationValidationWidget::performQueryRequest(const QString& query, const QStringList& args, QString& lastInsertId) {
    QSqlQuery* q = new QSqlQuery(getDatabase());
    q->prepare(query);
    for (const QString& arg : args)
        q->addBindValue(arg);
    dbExec(q);
    lastInsertId = q->lastInsertId().toString();
    delete q;
}
