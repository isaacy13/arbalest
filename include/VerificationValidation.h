//
// Created by isaacy13 on 10/21/2022.
//

#ifndef VV_H
#define VV_H
#include <vector>
#include <list>
#include <QString>

namespace VerificationValidation {
    class Arg {
    public:
        enum Type {
            Static, // argument is not variable (e.g.: "-t")
            Dynamic, // argument is variable (e.g.: "-t3mm,3mm" or "-t5mm,5mm")
            ObjectName, // argument is the objectName
            ObjectPath // argument is the objectPath
        };

        QString argument;
        QString defaultValue;
        Type type;

        Arg(QString argument, QString defaultValue = NULL, Type type = Static){
            this->argument = argument;
            this->defaultValue = defaultValue;

            if (defaultValue != NULL) this->type = Dynamic;
            else this->type = type;
        }

        void updateValue (QString input){
            defaultValue = input;
        }
    };

    class Test {
    public:
        QString testName;
        QString testCommand;
        QString suiteName;
        QString category;
        bool hasVariable;
        std::vector<Arg> ArgList;

        QString getCmdWithArgs() const {
            QString cmd = testCommand;
            for(int i = 0; i < ArgList.size(); i++){
                cmd = cmd + " " + ArgList[i].argument;
                if(ArgList[i].type == Arg::Type::Dynamic){
                    cmd  += ArgList[i].defaultValue;
                }
            }
            return cmd;
        }
    };

    class Result {
    public:
        enum Code {
            PASSED,
            WARNING,
            FAILED,
            UNPARSEABLE
        };

        struct ObjectIssue {
            QString objectName;
            QString issueDescription;
        };

        QString terminalOutput;
        Code resultCode;
        std::list<ObjectIssue> issues; // used list for O(1) push_back, O(N) access; since need to display all issues in GUI anyways
    };

    class DefaultTests {
    public:
        static VerificationValidation::Test MISMATCHED_DUP_IDS;
        static VerificationValidation::Test NO_DUPLICATE_ID;
        static VerificationValidation::Test NO_NULL_REGIONS;
        static VerificationValidation::Test NO_OVERLAPS;
        static VerificationValidation::Test NO_NESTED_REGIONS;
        static VerificationValidation::Test NO_EMPTY_COMBOS;
        static VerificationValidation::Test NO_SOLIDS_OUTSIDE_REGIONS;
        static VerificationValidation::Test ALL_BOTS_VOLUME_MODE;
        static VerificationValidation::Test NO_BOTS_LH_ORIENT; // TODO: this command can run faster if use unix
        static VerificationValidation::Test ALL_REGIONS_MAT;
        static VerificationValidation::Test ALL_REGIONS_LOS;
        static VerificationValidation::Test NO_MATRICES;
        static VerificationValidation::Test NO_INVALID_AIRCODE_REGIONS;
        static VerificationValidation::Test VALID_TITLE;
        const static std::vector<VerificationValidation::Test*> allTests;

        // TODO: missing "No errors when top level drawn"
        // TODO: missing "BoTs are valid"
        // TODO: missing "Air does not stick out"
        // TODO: missing "Ground plane at z=0"
    };

    class Parser {
    public:
        static Result* search(const QString& cmd, const QString* terminalOutput);
        static void searchSpecificTest(Result* r, const QString& currentLine, const Test* type);
        static bool searchCatchUsageErrors(Result* r, const QString& currentLine);
        static bool searchDBNotFoundErrors(Result* r);
        static void searchFinalDefense(Result* r);

        static Result* title(const QString& cmd, const QString* terminalOutput);

        static Result* lc(const QString* terminalOutput);
        static Result* gqa(const QString* terminalOutput);
    };
}

#endif