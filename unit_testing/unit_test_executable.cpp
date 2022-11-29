#include "common.h"
#include <bu.h>
#include <ged.h>
#include "VerificationValidation.h"

using namespace std;

int count_lines(const char* str) {

	int count = 0; 
	while(*str != '\0') {
		if(*str == '\n') count++;
		str++;
	}

	if(*(str-1) != '\n') count++;

	return count;

}

int main(int ac, char* av[]) {

	/* must be called on app startup for the correct pwd to be aquired set */
    	bu_setprogname(av[0]);

	/* Checks if the executable format is correct; Or if .g file exists */
    	if (ac != 6) {
        	printf("Usage: %s file.g\n", av[0]);
        	return 1;
    	}
    	if (!bu_file_exists(av[1], NULL)) {
        	printf("ERROR: [%s] does not exist, expecting .g file\n", av[1]);
        	return 2;
    	}

	struct ged *dbp_lc, *dbp_duplicate, *dbp_mismatch, *dbp_gqa, *dbp_null, *dbp_overlaps  ,*dbp_search, *dbp_combo, *dbp_aircodes;
	/* Opens database with .g file */
	dbp_lc = ged_open("db", av[1], 1);
	dbp_duplicate = ged_open("db", av[1], 1);
	dbp_mismatch = ged_open("db", av[1], 1);
	dbp_gqa = ged_open("db", av[1], 1);
	dbp_null = ged_open("db", av[1], 1);
	dbp_overlaps = ged_open("db", av[1], 1);
	dbp_search = ged_open("db", av[1], 1);
	dbp_combo = ged_open("db", av[1], 1);
	dbp_aircodes = ged_open("db", av[1], 1);

	/* This assumes there is an all object!!! */
	const char *cmd_lc[2] = {"lc", NULL};
	const char *cmd_duplicate[4] = {"lc","-d", "all" , NULL};
	const char *cmd_mismatch[4] = {"lc", "-m", "all", NULL};
	const char *cmd_gqa[2] = {"gqa", NULL};
	const char *cmd_null[6] = {"gqa", "-Ao", "-g4mm4mm", "-t0.3mm", "all", NULL};	
	const char *cmd_search[2] = {"search", NULL};
	const char *cmd_combo[5] = {"search", "/all", "-nnodes", "0", NULL};
	const char *cmd_aircodes[7] = {"search", "/all", "-type", "region", "-attr", "aircode", NULL};

	ged_exec(dbp_lc, 1, cmd_lc);
	const char* output_lc = bu_vls_addr(dbp_lc->ged_result_str);

	ged_exec(dbp_duplicate, 3, cmd_duplicate);
	const char* output_duplicate = bu_vls_addr(dbp_duplicate->ged_result_str);

	ged_exec(dbp_mismatch, 3, cmd_mismatch);
	const char* output_mismatch = bu_vls_addr(dbp_mismatch->ged_result_str);

	ged_exec(dbp_gqa, 1, cmd_gqa);
	const char* output_gqa = bu_vls_addr(dbp_gqa->ged_result_str);

	//ged_exec(dbp_null, 5, cmd_null);_
	//char* output_null = bu_vls_addr(dbp_null->ged_result_str);
	
	ged_exec(dbp_search, 1, cmd_search);
       	const char* output_search = bu_vls_addr(dbp_search->ged_result_str);	

	ged_exec(dbp_combo, 4, cmd_combo);
	const char* output_combo = bu_vls_addr(dbp_combo->ged_result_str);

	ged_exec(dbp_aircodes, 6, cmd_aircodes);
	const char* output_aircodes = bu_vls_addr(dbp_aircodes->ged_result_str);
	
	/* If input from .g file is invalid --> Error Check (only needs to check one) */ 
	if(count_lines(output_lc) < 1) { 
		printf("%s\n", "ERROR: lc output unexpected\n");
		return 2;
	}

/*------------------------------------------------------------------------ lc Usage Test ----------------------------------------------------------------------------------------------------------------*/
	QString qCommand = cmd_lc[0];
	QString qOutput = output_lc;

	/* Retrieve r from parser */
	VerificationValidation::Result* r = VerificationValidation::Parser::lc(qCommand, qOutput, av[1]);

	/* Check if parser reads Usage */
	if(r->terminalOutput.indexOf("Usage") != -1) {
		printf("%s\n", "lc (Usage): Test Passed");
	}
	else {
		printf("%s\n", "lc (Usage): Test Failed");
	}


/*------------------------------------------------------------------------ lc Duplicate Test ------------------------------------------------------------------------------------------------------------*/
	qCommand = cmd_duplicate[0]; // lc
	qCommand = qCommand + " " + cmd_duplicate[1] + " " + cmd_duplicate[2]; // -d all
	qOutput = output_duplicate;
	/* Retrieve the r from parser */
	r = VerificationValidation::Parser::lc(qCommand, qOutput, av[1]);

	/* Checks if parser reads in the correct number of length */
	if(r->issues.size() == atoi(av[2])) {
		printf("%s\n", "lc (Duplicate): Test Passed");
	}
	else {
		printf("%s\n", "lc (Duplicate): Test Failed");
	}



/*------------------------------------------------------------------------ lc Mismatch Test -------------------------------------------------------------------------------------------------------------*/
	qCommand = cmd_mismatch[0];
	qCommand = qCommand + " " + cmd_mismatch[1] + " " +  cmd_mismatch[2];
	qOutput = output_mismatch;

	/* Retrieve r from parser */
	r = VerificationValidation::Parser::lc(qCommand, qOutput, av[2]);

	/*Checks if parser reads in the correct number of length */
	if(r->issues.size() == atoi(av[3])) {
		printf("%s\n", "lc (Mismatch): Test Passed");
	}
	else {
		printf("%s\n", "lc (Mismatch): Test Failed");
	}


/*------------------------------------------------------------------------- gqa Usage Test  -------------------------------------------------------------------------------------------------------------*/

	qCommand = cmd_gqa[0];
	qOutput = output_gqa;

	/* Retrieve r from parser */
	r = VerificationValidation::Parser::gqa(qCommand, qOutput, VerificationValidation::DefaultTests::NO_OVERLAPS);

	/* Check if parser reads Usage */
	if(r->terminalOutput.indexOf("Usage") != -1) {
		printf("%s\n", "gqa (Usage): Test Passed");
	}
	else {
		printf("%s\n", "gqa (Usage): Test Failed");
	}	

/*-------------------------------------------------------------------------- gqa Null Test  -------------------------------------------------------------------------------------------------------------*/
	qCommand = cmd_null[0];
	qCommand = qCommand + " " + cmd_null[1] + " " + cmd_null[2] + " " + cmd_null[3] + " " + cmd_null[4] + " " + cmd_null[5];

	qOutput = mgedRun(qCommand, av[1]);

	/* Retrieve r from parser */
	r = VerificationValidation::Parser::gqa(qCommand, qOutput, VerificationValidation::DefaultTests::NO_NULL_REGIONS);

	/* Check if parser has a list of issues */
	if(r->issues.size() != 0) {
		printf("%s\n", "gqa (Null): Test Passed");
	}
	else {
		printf("%s\n", "gqa (Null): Test Failed");
	}



/*-------------------------------------------------------------------------- gqa Overlaps Test  ---------------------------------------------------------------------------------------------------------*/
	qCommand = "gqa -Ao -g32mm,4mm -t0.3mm all";
	qOutput = mgedRun(qCommand, av[1]);

	/* Retrieve r from parser */
	r = VerificationValidation::Parser::gqa(qCommand, qOutput, VerificationValidation::DefaultTests::NO_OVERLAPS);

	/* Check if parser has a list of issues */
	if(r->issues.size() != 0) {
		printf("%s\n", "gqa (Overlaps): Test Passed");
	}
	else {
		printf("%s\n", "gqa (Overlaps): Test Failed");
	}

/*-------------------------------------------------------------------------- search Usage Test  ---------------------------------------------------------------------------------------------------------*/
	qCommand = cmd_search[0];
	qOutput = output_search;
	
	/* Retrieve r from parser */
	r = VerificationValidation::Parser::search(qCommand, qOutput, VerificationValidation::DefaultTests::NO_OVERLAPS);

	/* Check if parser reads Usage */
	if(r->terminalOutput.indexOf("Usage") != -1) {
		printf("%s\n", "search (Usage): Test Passed");
	}
	else {
		printf("%s\n", "search (Usage): Test Failed");
	}
	


/*-------------------------------------------------------------------------- search Combos Test  --------------------------------------------------------------------------------------------------------*/

	qCommand = cmd_combo[0];
	qCommand = qCommand + " "  + cmd_combo[1] + " " + cmd_combo[2] + " " + cmd_combo[3];
	qOutput = output_combo;

	/* Retrieve r from parser */
	r = VerificationValidation::Parser::search(qCommand, qOutput, VerificationValidation::DefaultTests::NO_EMPTY_COMBOS);

	/* Check if parser read a line */
	if(r->issues.size() == atoi(av[4])) {
		printf("%s\n", "search (No Empty Combos): Test Passed");
	}
	else {
		printf("%s\n", "search (No Empty Combos): Test Failed");
	}

/*-------------------------------------------------------------------------- search Aircodes Test  ------------------------------------------------------------------------------------------------------*/
	
	qCommand = cmd_aircodes[0];
	qCommand = qCommand + " " + cmd_aircodes[1] + " " + cmd_aircodes[2] + " " + cmd_aircodes[3] + " " + cmd_aircodes[4] + " " + cmd_aircodes[5];
	qOutput = output_aircodes;

	/* Retrieve r from parser */
	r = VerificationValidation::Parser::search(qCommand, qOutput, VerificationValidation::DefaultTests::NO_INVALID_AIRCODE_REGIONS);

	/* Check if parser read correct number of issues */
       	if(r->issues.size() == atoi(av[5])) {
		printf("%s\n", "search (No Regions Have Aircodes): Test Passed" );
       	}
	else {
		printf("%s\n", "search (No Regions Have Aircodes): Test Failed");
	}	

	ged_close(dbp_lc);
	ged_close(dbp_duplicate);
	ged_close(dbp_mismatch);
	ged_close(dbp_gqa);
	ged_close(dbp_search);
	ged_close(dbp_combo);
	ged_close(dbp_aircodes);
	
	return 0;

}
