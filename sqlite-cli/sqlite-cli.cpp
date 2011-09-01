// sqlite.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>

static int callback(void *NotUsed, int argc, char **argv, char **azColName){
    int i;
    NotUsed=0;

    for(i=0; i<argc; i++){
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

void CreateLargeDB(_TCHAR* dbFilename)
{
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc,i;

    char **result;
    int nrow;
    int ncol;

    rc = sqlite3_open(dbFilename, &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char const* CREATE_TABLE_COMMAND = "create table t1 (t1key INTEGER PRIMARY KEY, data TEXT, num double, timeEnter DATE);";
    rc = sqlite3_exec(db, CREATE_TABLE_COMMAND, callback, 0, &zErrMsg);
    
    int c = 100000;
    while (c-- && (rc == SQLITE_OK))
    {
        const char const* INSERT_COMMAND = "insert into t1 (data, num) values ('This is sample data', %d);";
        char buffer[1024];
        sprintf(buffer, INSERT_COMMAND, rand()*rand()+rand());
        rc = sqlite3_exec(db, buffer, callback, 0, &zErrMsg);
    }

    //     rc = sqlite3_get_table(
    //         db,              /* An open database */
    //         "select * from stuff",       /* SQL to be executed */
    //         &result,       /* Result written to a char *[]  that this points to */
    //         &nrow,             /* Number of result rows written here */
    //         &ncol,          /* Number of result columns written here */
    //         &zErrMsg          /* Error msg written here */
    //         );
    // 
    //     printf("nrow=%d ncol=%d\n",nrow,ncol);
    //     for(i=0 ; i < nrow+ncol; ++i)
    //         printf("%s ",result[i]);
    // 
    // 
    // 
    //    sqlite3_free_table(result);

    if( rc!=SQLITE_OK ){
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
    }
    sqlite3_close(db);
}

void UpdateLarge(_TCHAR* dbFilename)
{
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc,i;

    char **result;
    int nrow;
    int ncol;

    rc = sqlite3_open(dbFilename, &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char const* CREATE_TABLE_COMMAND = "create table t1 (key INTEGER PRIMARY KEY, data TEXT);";
    rc = sqlite3_exec(db, CREATE_TABLE_COMMAND, callback, 0, &zErrMsg);

    printf(">> Insert\n");
    rc = sqlite3_exec(db, "insert into t1 (key, data) values (1, 'This is sample data');", callback, 0, &zErrMsg);
    rc = sqlite3_exec(db, "insert into t1 (key, data) values (2, 'Another sample data');", callback, 0, &zErrMsg);
    rc = sqlite3_exec(db, "insert into t1 (key, data) values (3, 'Third sample data');", callback, 0, &zErrMsg);

    printf(">> Update\n");
    const char const* UPDATE_COMMAND = "update t1 set data = 'asdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadkasdfghjklkajksfkjsdkadk' where key=1;";
    rc = sqlite3_exec(db, UPDATE_COMMAND, callback, 0, &zErrMsg);

    if( rc!=SQLITE_OK ){
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
    }
    sqlite3_close(db);
}

int _tmain(int argc, _TCHAR* argv[])
{
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc,i;

    char **result;
    int nrow;
    int ncol;

    if( argc!=3 ){
        fprintf(stderr, "Usage: %s DATABASE SQL-STATEMENT\n", argv[0]);
        exit(1);
    }

    UpdateLarge(argv[1]);

//     CreateLargeDB(argv[1]);

    rc = sqlite3_open(argv[1], &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }
//     rc = sqlite3_exec(db, argv[2], callback, 0, &zErrMsg);

    rc = sqlite3_get_table(
        db,              /* An open database */
        "select * from t1",       /* SQL to be executed */
        &result,       /* Result written to a char *[]  that this points to */
        &nrow,             /* Number of result rows written here */
        &ncol,          /* Number of result columns written here */
        &zErrMsg          /* Error msg written here */
        );

    printf("nrow=%d ncol=%d\n",nrow,ncol);
    for(i=0 ; i < nrow+ncol; ++i)
        printf("%s ",result[i]);



   sqlite3_free_table(result);

    if( rc!=SQLITE_OK ){
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
    }
    sqlite3_close(db);
    return 0;
}


