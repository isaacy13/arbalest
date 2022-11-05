//
// Created by isaacy13 on 10/14/2022.
//

#include "Utils.h"
#include "VerificationValidation.h"

using Result = VerificationValidation::Result;
using Test = VerificationValidation::Test;
using DefaultTests = VerificationValidation::DefaultTests;
using Parser = VerificationValidation::Parser;

bool Parser::catchUsageErrors(Result* r, const QString& currentLine) {
    int msgStart = currentLine.indexOf(QRegExp("usage:", Qt::CaseInsensitive));
    if (msgStart != -1) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({"SYNTAX ERROR", currentLine.mid(msgStart)});
        return true;
    }
    return false;
}

bool Parser::searchDBNotFoundErrors(Result* r) {
    int msgStart = r->terminalOutput.indexOf(QRegExp("Search path error:\n input: '.*' normalized: '.* not found in database!'", Qt::CaseInsensitive));
    if (msgStart != -1) {
        int objNameStartIdx = msgStart + 28; // skip over "Search path error:\n input: '"
        int objNameEndIdx = r->terminalOutput.indexOf("'", objNameStartIdx);
        
        int objNameSz = objNameEndIdx - objNameStartIdx;
        QString objName = r->terminalOutput.mid(objNameStartIdx, objNameSz);
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objName, r->terminalOutput.mid(msgStart)});

        return true;
    }
    return false; 
}

void Parser::finalDefense(Result* r) {
    int msgStart = r->terminalOutput.indexOf(QRegExp("error[: ]", Qt::CaseInsensitive));
    if (msgStart != -1) {
        r->resultCode = Result::Code::UNPARSEABLE;
        r->issues.push_back({"UNEXPECTED ERROR", r->terminalOutput.mid(msgStart)});
    }

    msgStart = r->terminalOutput.indexOf(QRegExp("warning[: ]", Qt::CaseInsensitive));
    if (msgStart != -1) {
        r->resultCode = Result::Code::UNPARSEABLE;
        r->issues.push_back({"UNEXPECTED WARNING", r->terminalOutput.mid(msgStart)});
    }
}

Result* Parser::search(const QString& cmd, const QString* terminalOutput) {
    Result* r = new Result;
    r->terminalOutput = terminalOutput->trimmed();
    r->resultCode = Result::Code::PASSED;
    Test* type = nullptr;

    // default checks
    if (QString::compare(DefaultTests::NO_NESTED_REGIONS.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_NESTED_REGIONS);
    
    else if (QString::compare(DefaultTests::NO_EMPTY_COMBOS.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_EMPTY_COMBOS);

    else if (QString::compare(DefaultTests::NO_SOLIDS_OUTSIDE_REGIONS.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_SOLIDS_OUTSIDE_REGIONS);

    else if (QString::compare(DefaultTests::ALL_BOTS_VOLUME_MODE.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::ALL_BOTS_VOLUME_MODE);

    else if (QString::compare(DefaultTests::NO_BOTS_LH_ORIENT.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_BOTS_LH_ORIENT);

    else if (QString::compare(DefaultTests::ALL_REGIONS_MAT.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::ALL_REGIONS_MAT);

    else if (QString::compare(DefaultTests::ALL_REGIONS_LOS.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::ALL_REGIONS_LOS);

    else if (QString::compare(DefaultTests::NO_MATRICES.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_MATRICES);

    else if (QString::compare(DefaultTests::NO_INVALID_AIRCODE_REGIONS.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_INVALID_AIRCODE_REGIONS);

    // search for DB errors (if found, return)
    if (Parser::searchDBNotFoundErrors(r)) return r;
    
    QStringList lines = r->terminalOutput.split('\n');
    for (size_t i = 0; i < lines.size(); i++) {
        // if no usage errors, run specific test
        if (!Parser::catchUsageErrors(r, lines[i]) && type)
            Parser::searchSpecificTest(r, lines[i], type);
    }

    // final defense: find any errors / warnings
    if (r->resultCode == Result::Code::PASSED)
        Parser::finalDefense(r);

    return r;
}

void Parser::searchSpecificTest(Result* r, const QString& currentLine, const Test* type) {
    if (currentLine.trimmed().isEmpty()) return;
    QString objectPath = currentLine;
    QString objectName = currentLine.split('/').last();

    if (type == &DefaultTests::NO_NESTED_REGIONS) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objectPath, "Nested region at '" + objectName + "'"});
    } 
    
    else if (type == &DefaultTests::NO_EMPTY_COMBOS) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objectPath, "Empty combo at '" + objectName + "'"});
    }

    else if (type == &DefaultTests::NO_SOLIDS_OUTSIDE_REGIONS) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objectPath, "Solid outside of region at '" + objectName + "'"});
    }

    else if (type == &DefaultTests::ALL_BOTS_VOLUME_MODE) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objectPath, "BoT not volume mode at '" + objectName + "'"});
    }

    else if (type == &DefaultTests::NO_BOTS_LH_ORIENT) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objectPath, "Left-hand oriented BoT at '" + objectName + "'"});
    }

    else if (type == &DefaultTests::ALL_REGIONS_MAT) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objectPath, "Obj/region doesn't have material at '" + objectName + "'"});
    }

    else if (type == &DefaultTests::ALL_REGIONS_LOS) {
        r->resultCode = Result::Code::FAILED;
        r->issues.push_back({objectPath, "Obj/region doesn't have LOS at '" + objectName + "'"});
    }

    else if (type == &DefaultTests::NO_MATRICES) {
        r->resultCode = Result::Code::WARNING;
        r->issues.push_back({objectPath, "Matrix at '" + objectName + "'"});
    }

    else if (type == &DefaultTests::NO_INVALID_AIRCODE_REGIONS) {
        r->resultCode = Result::Code::WARNING;
        r->issues.push_back({objectPath, "Obj/region has aircode at '" + objectName + "'"});
    }
}

Result* Parser::title(const QString& cmd, const QString* terminalOutput) {
    Result* r = new Result;
    r->terminalOutput = terminalOutput->trimmed();
    r->resultCode = Result::Code::PASSED;

    QStringList cmdList = cmd.split(" ");
    if (cmdList.size() > 1) {
        r->resultCode = Result::Code::UNPARSEABLE;
        r->issues.push_back({"SYNTAX ERROR", "title cannot have any arguments for testing (implies setting database name)"});
    }

    bool suspiciousTitle = terminalOutput->contains("tmp", Qt::CaseInsensitive) || 
    terminalOutput->contains("temporary", Qt::CaseInsensitive) || 
    terminalOutput->contains("untitled", Qt::CaseInsensitive);
    
    if (suspiciousTitle) {
        r->resultCode = Result::Code::WARNING;
        r->issues.push_back({*terminalOutput, "title contains a keyword indicating it is not finalized"});
    }

    return r;
}

Result* Parser::lc(const QString* terminalOutput) {
    return nullptr; // TODO: implement
}

Result* Parser::gqa(const QString& cmd, const QString* terminalOutput) {
    Result* r = new Result;
    r->terminalOutput = terminalOutput->trimmed();
    r->resultCode = Result::Code::PASSED;
    Test* type = nullptr;

    if (QString::compare(DefaultTests::NO_NULL_REGIONS.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_NULL_REGIONS);
    
    else if (QString::compare(DefaultTests::NO_OVERLAPS.getCmdWithArgs(), cmd, Qt::CaseInsensitive) == 0)
        type = (Test*) &(DefaultTests::NO_OVERLAPS);
    
    QStringList lines = r->terminalOutput.split('\n');
    bool startParsing = false;
    for (size_t i = 0; i < lines.size(); i++) {
        // if no usage errors, run specific test
        if(lines[i].startsWith("list Overlaps"))
        {
            startParsing = true;
            continue;
        }
        if (!Parser::catchUsageErrors(r, lines[i]) && type && startParsing)
            Parser::gqaSpecificTest(r, lines[i], type);
    }

    // final defense: find any errors / warnings
    if (r->resultCode == Result::Code::PASSED)
        Parser::finalDefense(r);

    return r;
}

void Parser::gqaSpecificTest(Result* r, const QString& currentLine, const Test* type) {
    if (currentLine.trimmed().isEmpty()) return;
    
    if(type == &DefaultTests::NO_NULL_REGIONS)
    {
        QString objectPath1 = currentLine.split(' ')[0];
        QString objectName1 = objectPath1;
        int slashIdx1 = objectPath1.lastIndexOf('/');
        if(slashIdx1 != -1)
            objectName1 = objectPath1.mid(slashIdx1 + 1, objectPath1.size() - slashIdx1 - 1);

        if(currentLine.contains("was not hit"))
        {
            r->resultCode = Result::Code::FAILED;
            r->issues.push_back({objectPath1, "'"+objectName1+"' was not hit"});
        }
    }

    else if(type == &DefaultTests::NO_OVERLAPS)
    {
        if(currentLine.contains("was not hit")) return;

        r->resultCode = Result::Code::WARNING;
        QStringList splitLine = currentLine.split(' ');

        QString objectPath1 = splitLine[0];
        QString objectName1 = objectPath1;
        int slashIdx1 = objectPath1.lastIndexOf('/');
        if(slashIdx1 != -1)
            objectName1 = objectPath1.mid(slashIdx1 + 1, objectPath1.size() - slashIdx1 - 1);

        QString objectPath2 = splitLine[1];
        QString objectName2 = objectPath2;
        int slashIdx2 = objectPath2.lastIndexOf('/');
        if(slashIdx2 != -1)
            objectName2 = objectPath2.mid(slashIdx2 + 1, objectPath2.size() - slashIdx2 - 1);

        QString countString = splitLine[2].mid(6, splitLine[2].size() - 6);
        QString distanceString = splitLine[3].mid(5, splitLine[3].size() - 5);
        int parenthIdx = currentLine.indexOf('(');
        QString locationString = currentLine.mid(parenthIdx + 1, currentLine.size() - parenthIdx - 1);

        r->issues.push_back({objectPath1, "'"+objectName1+"' overlaps with '" + objectName2 + "' -- Count: " + countString + ", Distance: "+distanceString});
    }
    
}
