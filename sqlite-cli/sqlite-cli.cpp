// sqlite.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <stdio.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <Windows.h>

extern "C" int vfstrace_register(
    const char *zTraceName,
    const char *zOldVfsName,
    int (*xOut)(const char*,void*),
    void *pOutArg,
    int makeDefault
    );


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

    vfstrace_register("trace",0,(int(*)(const char*,void*))fputs,stderr,1);

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

    vfstrace_register("trace",0,(int(*)(const char*,void*))fputs,stderr,1);

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

    QueryWikideskDB("Z:\\wikidesk.db");
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


