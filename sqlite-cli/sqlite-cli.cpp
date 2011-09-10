// sqlite.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <Windows.h>
#include <time.h>

extern "C" int vfscompress_register(
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
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyz 123456789,.!?+-";
    const int ALPHA_COUNT = sizeof(alpha) - 1;

    char* text = new char[length + 1];
    for (int i = 0; i < length; ++i)
    {
        text[i] = alpha[rand() % ALPHA_COUNT];
    }

    text[length] = '\0';
    return text;
}

const char* expected_value;
static int callback_check(void *NotUsed, int argc, char **argv, char **azColName)
{
    int i;
    NotUsed=0;

    for(i=0; i<argc; i++)
    {
        //printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
        if (strcmp(expected_value, argv[i]) != 0)
        {
            printf("Error: value mismatch.\nExpected: %s\n Got: %s\n", expected_value, argv[i]);
            exit(1);
        }
        else
        {
            printf("++PASS++\n");
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

    //srand(time(NULL));

    DeleteFile(dbFilename);
    vfscompress_register(0, -1);
    rc = sqlite3_open(dbFilename, &db);
    if( rc ){
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(1);
    }

    const char const* CREATE_TABLE_COMMAND = "create table t1 (t1key INTEGER PRIMARY KEY, data TEXT, num INT, timeEnter DATE);";
    rc = sqlite3_exec(db, CREATE_TABLE_COMMAND, callback, 0, &zErrMsg);

    DWORD start = GetTickCount();

    const int ROW_COUNT = 100;
    const int MAX_DATA_SIZE = 300 * 1024;
    const char* test_data[ROW_COUNT];
    char* buffer = new char[(MAX_DATA_SIZE * 2) + 128];
    const char const* INSERT_COMMAND = "INSERT INTO t1 (data, num) values ('%s', %d);";

    for (int c = 0; (c < ROW_COUNT) && (rc == SQLITE_OK); ++c)
    {
        int data_size = ((int)rand() * (int)rand()) % MAX_DATA_SIZE;
        test_data[c] = GenerateText(data_size);
        sprintf(buffer, INSERT_COMMAND, test_data[c], c);
        printf("%d) Inserting %d bytes...\n", c, data_size);
        rc = sqlite3_exec(db, buffer, callback, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            printf("%d) Insertion failed! Error code: %d.\n", c, rc);
        }
    }

    printf(">>>> Reading\n");
    const char const* SELECT_COMMAND = "SELECT data FROM t1 WHERE num = %d;";

    for (int c = ROW_COUNT - 1; (c >= 0) && (rc == SQLITE_OK); --c)
    {
        printf("%d) Selecting...\n", c);
        sprintf(buffer, SELECT_COMMAND, c);
        expected_value = test_data[c];
        rc = sqlite3_exec(db, buffer, callback_check, 0, &zErrMsg);
    }

    printf(">>>> Updating\n");
    const char const* UPDATE_COMMAND = "UPDATE t1 set data = '%s' WHERE num = %d;";

    for (int c = ROW_COUNT - 1; (c >= 0) && (rc == SQLITE_OK); --c)
    {
        int data_size = ((int)rand() * (int)rand()) % (MAX_DATA_SIZE * 2);
        delete [] test_data[c];
        test_data[c] = GenerateText(data_size);
        sprintf(buffer, UPDATE_COMMAND, test_data[c], c);
        printf("%d) Updating %d bytes...\n", c, data_size);
        rc = sqlite3_exec(db, buffer, callback, 0, &zErrMsg);
        if (rc != SQLITE_OK)
        {
            printf("%d) Update failed! Error code: %d.\n", c, rc);
        }
    }

    printf(">>>> Reading\n");
    for (int c = ROW_COUNT - 1; (c >= 0) && (rc == SQLITE_OK); --c)
    {
        printf("%d) Selecting...\n", c);
        sprintf(buffer, SELECT_COMMAND, c);
        expected_value = test_data[c];
        rc = sqlite3_exec(db, buffer, callback_check, 0, &zErrMsg);
        delete [] test_data[c];
    }

    if( rc!=SQLITE_OK ){
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
    }

    printf("Finished in %dms\n", GetTickCount() - start);

    sqlite3_close(db);

    GetSparseFileSize(dbFilename);
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

BOOL SparseFileSuppored(LPCTSTR lpVolRootPath)
{
    DWORD dwFlags;

    GetVolumeInformation(
        lpVolRootPath,
        NULL,
        MAX_PATH,
        NULL,
        NULL,
        &dwFlags,
        NULL,
        MAX_PATH);

    return (dwFlags & FILE_SUPPORTS_SPARSE_FILES);

}

HANDLE CreateSparseFile(LPCTSTR lpSparseFileName)
{
    // Use CreateFile as you would normally - Create file with whatever flags
    //and File Share attributes that works for you
    DWORD dwTemp;

    HANDLE hSparseFile = CreateFile(lpSparseFileName,
        GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hSparseFile == INVALID_HANDLE_VALUE)
        return hSparseFile;

    BOOL res = DeviceIoControl(hSparseFile,
        FSCTL_SET_SPARSE,
        NULL,
        0,
        NULL,
        0,
        &dwTemp,
        NULL);
    return hSparseFile;
}

DWORD SetSparseRange(HANDLE hSparseFile, LONGLONG start, LONGLONG size)
{
    // Specify the starting and the ending address (not the size) of the
    // sparse zero block
    FILE_ZERO_DATA_INFORMATION fzdi;
    fzdi.FileOffset.QuadPart = start;
    fzdi.BeyondFinalZero.QuadPart = start + size;
    // Mark the range as sparse zero block
    DWORD dwTemp;
    SetLastError(0);
    BOOL bStatus = DeviceIoControl(hSparseFile,
        FSCTL_SET_ZERO_DATA,
        &fzdi,
        sizeof(fzdi),
        NULL,
        0,
        &dwTemp,
        NULL);
    if (bStatus) return 0; //Sucess
    else
    {
        DWORD e = GetLastError();
        return(e); //return the error value
    }
}

void CreateSparse()
{
    HANDLE hFile = CreateSparseFile("SparseFile");

    //::SetFilePointer(hFile, 0x1000000, NULL, FILE_END);
    //::SetEndOfFile(hFile);

    const int SIZE = 640000;
    char* buffer = new char[SIZE];
    memset(buffer, 'a', SIZE);

    DWORD wrote = 0;
    DWORD written = 0;
    WriteFile(hFile, buffer, SIZE, &written, NULL);
    wrote += written;

    for (int i = 0; i < SIZE; ++i)
    {
        int start = i * 64 * 1024;
        SetSparseRange(hFile, start + 1, (64 * 1024) - 1);
    }

    printf("Wrote %d bytes.\n", wrote);
    CloseHandle(hFile);
}

void ReadSparse()
{
    HANDLE hFile = CreateSparseFile("SparseFile");

    const int SIZE = 6 * 6 * 512;
    char* buffer = new char[SIZE];
    memset(buffer, 0, SIZE);

    DWORD read = 0;
    ReadFile(hFile, buffer, SIZE, &read, NULL);
    printf("Read %d bytes.\n", read);

    printf(buffer);

    CloseHandle(hFile);
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
    _tprintf(_T("\nFile total size: %I64uKB\nActual size on disk: %I64uKB\n"),
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

    vfscompress_register(1,1);

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

int _tmain(int argc, _TCHAR* argv[])
{
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc,i;

    char **result;
    int nrow;
    int ncol;

    //QueryWikideskDB("Z:\\wikidesk.db");
    CreateLargeDB("Z:\\test.db");
    return 0;


    printf("Supported: %s\n", SparseFileSuppored("C:\\") ? "Yes" : "No");

    CreateSparse();
    GetSparseFileSize("SparseFile");
    ReadSparse();
    return 0;

    if( argc!=3 ){
        fprintf(stderr, "Usage: %s DATABASE SQL-STATEMENT\n", argv[0]);
        exit(1);
    }

    UpdateLarge(argv[1]);

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


