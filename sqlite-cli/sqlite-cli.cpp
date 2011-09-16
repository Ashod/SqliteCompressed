// sqlite.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <Windows.h>
#include <time.h>

extern "C" int sqlite3_compress(
    int trace,
    int compressionLevel
    );

BOOL GetSparseFileSize(LPCTSTR lpFileName);

static int callback(void *NotUsed, int argc, char **argv, char **azColName)
{
    int i;
    NotUsed=0;

    for(i=0; i<argc; i++){
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

const char* GenerateText(int length)
{
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyz 123456789,.!?+-ABCDEFGHIJKLMNOPQRSTUVWXYZ~!@#$%^&*()_[];/`";
    const int ALPHA_COUNT = sizeof(alpha) - 1;

    char* text = new char[length + 1];
    for (int i = 0; i < length; ++i)
    {
        text[i] = alpha[rand() % ALPHA_COUNT];
    }

    text[length] = '\0';
    return text;
}

static int callback_check(void *expected_value, int argc, char **argv, char **azColName)
{
    for (int i = 0; i < argc; i++)
    {
        if (strcmp((char*)expected_value, argv[i]) != 0)
        {
            printf("ERROR: value mismatch.\nExpected length: %d, Got: %d\n", strlen((char*)expected_value), strlen(argv[i]));
            exit(1);
        }
        else
        {
            // Got it, break the query.
            printf("++PASS++\n");
            return 1;
        }
    }

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

    srand(0);

    DeleteFile(dbFilename);
    sqlite3_compress(1, -1);
    rc = sqlite3_open(dbFilename, &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char const* CREATE_TABLE_COMMAND = "create table t1 (t1key INTEGER PRIMARY KEY, data TEXT, num INT, timeEnter DATE);";
    rc = sqlite3_exec(db, CREATE_TABLE_COMMAND, callback, 0, &zErrMsg);

    DWORD start = GetTickCount();

    const int ROW_COUNT = 2;
    const int MAX_DATA_SIZE = 4400 * 1024;
    const char* test_data[ROW_COUNT];
    memset(test_data, 0, sizeof(test_data));
    char* buffer = new char[(MAX_DATA_SIZE * 2) + 128];
    const char const* INSERT_COMMAND = "INSERT INTO t1 (data, num) values ('%s', %d);";

    printf("\n>>>> Inserting\n");
    for (int c = 0; (c < ROW_COUNT) && (rc == SQLITE_OK); ++c)
    {
        int data_size = ((int)rand() * (int)rand()) % MAX_DATA_SIZE;
        test_data[c] = GenerateText(data_size);
        sprintf(buffer, INSERT_COMMAND, test_data[c], c);
        printf("%d) Inserting %d bytes...\n", c, data_size);
        rc = sqlite3_exec(db, buffer, callback, 0, &zErrMsg);

        if ((rc != SQLITE_OK) && (rc != SQLITE_ABORT))
        {
            fprintf(stderr, "Error: %s\n", zErrMsg);
            break;
        }
    }

    printf("\n>>>> Reading\n");
    const char const* SELECT_COMMAND = "SELECT data FROM t1 WHERE num = %d;";

    for (int c = ROW_COUNT - 1; (c >= 0) && ((rc == SQLITE_OK) || (rc == SQLITE_ABORT)); --c)
    {
        printf("%d) Selecting... ", c);
        sprintf(buffer, SELECT_COMMAND, c);
        rc = sqlite3_exec(db, buffer, callback_check, (void*)test_data[c], &zErrMsg);
        delete [] test_data[c];
        test_data[c] = NULL;

        if ((rc != SQLITE_OK) && (rc != SQLITE_ABORT))
        {
            fprintf(stderr, "Error: %s\n", zErrMsg);
            break;
        }
    }

    rc = SQLITE_OK;
    printf("\n>>>> Updating\n");
    const char const* UPDATE_COMMAND = "UPDATE t1 set data = '%s' WHERE num = %d;";

    for (int c = ROW_COUNT - 1; (c >= 0) && (rc == SQLITE_OK); --c)
    {
        int data_size = ((int)rand() * (int)rand()) % (MAX_DATA_SIZE * 2);
        delete [] test_data[c];
        test_data[c] = NULL;
        test_data[c] = GenerateText(data_size);
        sprintf(buffer, UPDATE_COMMAND, test_data[c], c);
        printf("%d) Updating %d bytes...\n", c, data_size);
        rc = sqlite3_exec(db, buffer, callback, 0, &zErrMsg);

        if ((rc != SQLITE_OK) && (rc != SQLITE_ABORT))
        {
            fprintf(stderr, "Error: %s\n", zErrMsg);
            break;
        }
    }

    printf("\n>>>> Reading\n");
    for (int c = ROW_COUNT - 1; c >= 0; --c)
    {
        printf("%d) Selecting... ", c);
        sprintf(buffer, SELECT_COMMAND, c);
        rc = sqlite3_exec(db, buffer, callback_check, (void*)test_data[c], &zErrMsg);
        delete [] test_data[c];
        test_data[c] = NULL;

        if ((rc != SQLITE_OK) && (rc != SQLITE_ABORT))
        {
            fprintf(stderr, "Error: %s\n", zErrMsg);
            break;
        }
    }

    printf("\nFinished in %dms\n", GetTickCount() - start);

    sqlite3_close(db);

    GetSparseFileSize(dbFilename);
}

BOOL GetSparseFileSize(LPCTSTR lpFileName)
{
    // Retrieves the size of the specified file, in bytes. The size includes
    // both allocated ranges and sparse ranges.
    HANDLE hFile = CreateFile(lpFileName,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;
    LARGE_INTEGER liSparseFileSize;
    GetFileSizeEx(hFile, &liSparseFileSize);

    // Retrieves the file's actual size on disk, in bytes. The size does not
    // include the sparse ranges.

    LARGE_INTEGER liSparseFileCompressedSize;
    liSparseFileCompressedSize.LowPart = GetCompressedFileSize(lpFileName,
        (LPDWORD)&liSparseFileCompressedSize.HighPart);
    // Print the result
    _tprintf(_T("\nFile total size: %I64u KB\nActual size on disk: %I64u KB\n"),
        liSparseFileSize.QuadPart / 1024,
        liSparseFileCompressedSize.QuadPart / 1024);

    CloseHandle(hFile);
    return TRUE;
}

void QueryWikideskDB(_TCHAR* dbFilename)
{
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc,i;

    char **result;
    int nrow;
    int ncol;

    sqlite3_compress(1,1);

    rc = sqlite3_open(dbFilename, &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char const* COMMAND = "SELECT Title FROM Page WHERE Title LIKE '%zimb%';";
    rc = sqlite3_exec(db, COMMAND, callback, 0, &zErrMsg);

    if( rc!=SQLITE_OK ){
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
    }
    sqlite3_close(db);
}

int QuickTest(LPCTSTR lpFilename, char* query)
{
    sqlite3 *db;
    DeleteFile(lpFilename);

    sqlite3_compress(-1, -1);
    int rc = sqlite3_open(lpFilename, &db);

    if (rc == SQLITE_OK)
    {
        char *zErrMsg = 0;
        rc = sqlite3_exec(db, query, callback, 0, &zErrMsg);
    }

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Sqlite Error: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_close(db);
    return rc;
}

int _tmain(int argc, _TCHAR* argv[])
{
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc,i;

    char **result;
    int nrow;
    int ncol;

    CreateLargeDB("Z:\\test.db");
    return 0;

    QuickTest("Z:\\test.db", 
        "CREATE TABLE abc(a PRIMARY KEY, b, c);\
        INSERT INTO abc VALUES(1, 2, 3);\
        INSERT INTO abc VALUES(2, 3, 4);\
        INSERT INTO abc SELECT a+2, b, c FROM abc;\
        SELECT * FROM abc;");

    QuickTest("Z:\\test.db", 
        "CREATE TABLE abc(a PRIMARY KEY, b, c);\
        INSERT OR REPLACE INTO abc VALUES(1, 2, 3);\
        INSERT OR REPLACE INTO abc VALUES(1, 2, 4);\
        INSERT OR REPLACE INTO abc SELECT a+2, b, c FROM abc;\
        SELECT * FROM abc;");

    //QueryWikideskDB("Z:\\wikidesk.db");
    return 0;

    if( argc!=3 ){
        fprintf(stderr, "Usage: %s DATABASE SQL-STATEMENT\n", argv[0]);
        exit(1);
    }

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


