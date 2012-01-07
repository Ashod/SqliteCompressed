/*
** 2011 Sep 03 - Ashod Nakashian (ashodnakashian.com)
** Released: 2011 Sep 18 - EXPERIMENTAL.
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
******************************************************************************
**
** This file contains code implements a VFS shim that read/writes compressed database.
** Based on the trace VFS shim.
**
** USAGE:
**
** This source file exports a single symbol which is the name of a function:
**
**   int sqlite3_compress(
**       int trace,                  // True to trace operations to stderr
**       int compressionLevel,       // The compression level: -1 for default, 1 fastest, 9 best
**       int chunkSizeBytes          // The size of the compression chunk in bytes: -1 for default
**   );
**
** BUILD:
**
** Compile this file and link with Zlib library to Sqlite3. 
**
*/
#include "sqliteInt.h"
#if SQLITE_OS_WIN               /* This file is used for windows only */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "zlib.h"
#include <winbase.h>
#include <WinIoCtl.h>

#ifdef __CYGWIN__
# include <sys/cygwin.h>
#endif

extern void *convertUtf8Filename(const char *zFilename);

/*
** The chunk size is the compression unit.
** It must be in multiple of max-page-size.
** Defaults to 4 max-pages (64k * 4).
** Memory consumption per chunk is DEF_CHUNK_SIZE_BYTES * 2.
*/
#define COMPRESION_UNIT_SIZE_BYTES  (64 * 1024)
#define DEF_CHUNK_SIZE_BYTES        (4 * COMPRESION_UNIT_SIZE_BYTES)

/*
** The number of chunks to cache.
** MUST be at least 2 slots.
** Memory consumption is CACHE_SIZE_IN_CHUNKS * DEF_CHUNK_SIZE_BYTES * 2.
*/
#define CACHE_SIZE_IN_CHUNKS        (2)

/*
** The default compression level.
** Must be 1-9 or -1 for library default.
*/
#define DEFAULT_COMPRESSION_LEVEL   (6)

enum State
{
    Empty,          //< no data at all.
    Uncompressed,   //< new data not compressed.
    Unwritten,      //< compressed data in memory.
    Cached          //< compressed data flushed.
};

typedef enum TraceLevel
{
    Error = -1,
    None = 0,
    Registeration,
    OpenClose,
    NonIoOps,
    Compression,
    IoOps,
    Trace,
    Maximum

} TraceLevel;

/*
** The default trace level.
*/
#define DEFAULT_TRACE_LEVEL		Registeration

typedef struct vfsc_chunk vfsc_chunk;
struct vfsc_chunk {
    sqlite_int64 offset;
    int origSize;
    int compSize;
    char* pOrigData;
    char* pCompData;
    char state;
};

/*
** An instance of this structure is attached to the each trace VFS to
** provide auxiliary information.
*/
typedef struct vfsc_info vfsc_info;
struct vfsc_info {
  sqlite3_vfs *pRootVfs;              /* The underlying real VFS */
  int (*xOut)(const char*, void*);    /* Send output here */
  void *pOutArg;                      /* First argument to xOut */
  const char *zVfsName;               /* Name of this trace-VFS */
  sqlite3_vfs *pTraceVfs;             /* Pointer back to the trace VFS */
  vfsc_chunk* pCache[CACHE_SIZE_IN_CHUNKS];  /* The chunk cache. */
  int trace;
};

/*
** The sqlite3_file object for the trace VFS
*/
typedef struct vfsc_file vfsc_file;
struct vfsc_file {
  sqlite3_file base;        /* Base class.  Must be first */
  vfsc_info *pInfo;     /* The trace-VFS to which this file belongs */
  const char *zFName;       /* Base name of the file */
  sqlite3_file *pReal;      /* The real underlying file */
  HANDLE hFile;             /* The underlying file handle */
};

/*
** Method declarations for vfsc_file.
*/
static int vfscClose(sqlite3_file*);
static int vfscRead(sqlite3_file*, void*, int iAmt, sqlite3_int64 iOfst);
static int vfscWrite(sqlite3_file*,const void*,int iAmt, sqlite3_int64);
static int vfscTruncate(sqlite3_file*, sqlite3_int64 size);
static int vfscSync(sqlite3_file*, int flags);
static int vfscFileSize(sqlite3_file*, sqlite3_int64 *pSize);
static int vfscLock(sqlite3_file*, int);
static int vfscUnlock(sqlite3_file*, int);
static int vfscCheckReservedLock(sqlite3_file*, int *);
static int vfscFileControl(sqlite3_file*, int op, void *pArg);
static int vfscSectorSize(sqlite3_file*);
static int vfscDeviceCharacteristics(sqlite3_file*);
static int vfscShmLock(sqlite3_file*,int,int,int);
static int vfscShmMap(sqlite3_file*,int,int,int, void volatile **);
static void vfscShmBarrier(sqlite3_file*);
static int vfscShmUnmap(sqlite3_file*,int);

/*
** Method declarations for vfsc_vfs.
*/
static int vfscOpen(sqlite3_vfs*, const char *, sqlite3_file*, int , int *);
static int vfscDelete(sqlite3_vfs*, const char *zName, int syncDir);
static int vfscAccess(sqlite3_vfs*, const char *zName, int flags, int *);
static int vfscFullPathname(sqlite3_vfs*, const char *zName, int, char *);
static void *vfscDlOpen(sqlite3_vfs*, const char *zFilename);
static void vfscDlError(sqlite3_vfs*, int nByte, char *zErrMsg);
static void (*vfscDlSym(sqlite3_vfs*,void*, const char *zSymbol))(void);
static void vfscDlClose(sqlite3_vfs*, void*);
static int vfscRandomness(sqlite3_vfs*, int nByte, char *zOut);
static int vfscSleep(sqlite3_vfs*, int microseconds);
static int vfscCurrentTime(sqlite3_vfs*, double*);
static int vfscGetLastError(sqlite3_vfs*, int, char*);
static int vfscCurrentTimeInt64(sqlite3_vfs*, sqlite3_int64*);
static int vfscSetSystemCall(sqlite3_vfs*,const char*, sqlite3_syscall_ptr);
static sqlite3_syscall_ptr vfscGetSystemCall(sqlite3_vfs*, const char *);
static const char *vfscNextSystemCall(sqlite3_vfs*, const char *zName);

static int CompressionLevel = DEFAULT_COMPRESSION_LEVEL;
static int ChunkSizeBytes = DEF_CHUNK_SIZE_BYTES;
static int CacheHits = 0;
static int TotalHits = 0;
static int CompressCount = 0;
static int DecompressCount = 0;

/*
** Return a pointer to the tail of the pathname.  Examples:
**
**     /home/drh/xyzzy.txt -> xyzzy.txt
**     xyzzy.txt           -> xyzzy.txt
*/
static const char *fileTail(const char *z){
  int i;
  if( z==0 ) return 0;
  i = strlen(z)-1;
  while( i>0 && z[i-1]!='/' ){ i--; }
  return &z[i];
}

/*
** Send trace output defined by zFormat and subsequent arguments.
*/
static void vfsc_printf(
  vfsc_info *pInfo,
  TraceLevel level,
  const char *zFormat,
  ...
){
  va_list ap;
  char *zMsg;
  if (pInfo->trace == None || pInfo->trace < level)
  {
      return;
  }

  va_start(ap, zFormat);
  zMsg = sqlite3_vmprintf(zFormat, ap);
  va_end(ap);
  pInfo->xOut(zMsg, pInfo->pOutArg);
  sqlite3_free(zMsg);
}

/*
** Convert value rc into a string and print it using zFormat.  zFormat
** should have exactly one %s
*/
static void vfsc_print_errcode(
  vfsc_info *pInfo,
  TraceLevel level,
  const char *zFormat,
  int rc
){
  char zBuf[50];
  char *zVal;

  if (pInfo->trace == None || pInfo->trace < level)
  {
      return;
  }

  switch( rc ){
    case SQLITE_OK:         zVal = "SQLITE_OK";          break;
    case SQLITE_ERROR:      zVal = "SQLITE_ERROR";       break;
    case SQLITE_PERM:       zVal = "SQLITE_PERM";        break;
    case SQLITE_ABORT:      zVal = "SQLITE_ABORT";       break;
    case SQLITE_BUSY:       zVal = "SQLITE_BUSY";        break;
    case SQLITE_NOMEM:      zVal = "SQLITE_NOMEM";       break;
    case SQLITE_READONLY:   zVal = "SQLITE_READONLY";    break;
    case SQLITE_INTERRUPT:  zVal = "SQLITE_INTERRUPT";   break;
    case SQLITE_IOERR:      zVal = "SQLITE_IOERR";       break;
    case SQLITE_CORRUPT:    zVal = "SQLITE_CORRUPT";     break;
    case SQLITE_FULL:       zVal = "SQLITE_FULL";        break;
    case SQLITE_CANTOPEN:   zVal = "SQLITE_CANTOPEN";    break;
    case SQLITE_PROTOCOL:   zVal = "SQLITE_PROTOCOL";    break;
    case SQLITE_EMPTY:      zVal = "SQLITE_EMPTY";       break;
    case SQLITE_SCHEMA:     zVal = "SQLITE_SCHEMA";      break;
    case SQLITE_CONSTRAINT: zVal = "SQLITE_CONSTRAINT";  break;
    case SQLITE_MISMATCH:   zVal = "SQLITE_MISMATCH";    break;
    case SQLITE_MISUSE:     zVal = "SQLITE_MISUSE";      break;
    case SQLITE_NOLFS:      zVal = "SQLITE_NOLFS";       break;
    case SQLITE_IOERR_READ:         zVal = "SQLITE_IOERR_READ";         break;
    case SQLITE_IOERR_SHORT_READ:   zVal = "SQLITE_IOERR_SHORT_READ";   break;
    case SQLITE_IOERR_WRITE:        zVal = "SQLITE_IOERR_WRITE";        break;
    case SQLITE_IOERR_FSYNC:        zVal = "SQLITE_IOERR_FSYNC";        break;
    case SQLITE_IOERR_DIR_FSYNC:    zVal = "SQLITE_IOERR_DIR_FSYNC";    break;
    case SQLITE_IOERR_TRUNCATE:     zVal = "SQLITE_IOERR_TRUNCATE";     break;
    case SQLITE_IOERR_FSTAT:        zVal = "SQLITE_IOERR_FSTAT";        break;
    case SQLITE_IOERR_UNLOCK:       zVal = "SQLITE_IOERR_UNLOCK";       break;
    case SQLITE_IOERR_RDLOCK:       zVal = "SQLITE_IOERR_RDLOCK";       break;
    case SQLITE_IOERR_DELETE:       zVal = "SQLITE_IOERR_DELETE";       break;
    case SQLITE_IOERR_BLOCKED:      zVal = "SQLITE_IOERR_BLOCKED";      break;
    case SQLITE_IOERR_NOMEM:        zVal = "SQLITE_IOERR_NOMEM";        break;
    case SQLITE_IOERR_ACCESS:       zVal = "SQLITE_IOERR_ACCESS";       break;
    case SQLITE_IOERR_CHECKRESERVEDLOCK:
                               zVal = "SQLITE_IOERR_CHECKRESERVEDLOCK"; break;
    case SQLITE_IOERR_LOCK:         zVal = "SQLITE_IOERR_LOCK";         break;
    case SQLITE_IOERR_CLOSE:        zVal = "SQLITE_IOERR_CLOSE";        break;
    case SQLITE_IOERR_DIR_CLOSE:    zVal = "SQLITE_IOERR_DIR_CLOSE";    break;
    case SQLITE_IOERR_SHMOPEN:      zVal = "SQLITE_IOERR_SHMOPEN";      break;
    case SQLITE_IOERR_SHMSIZE:      zVal = "SQLITE_IOERR_SHMSIZE";      break;
    case SQLITE_IOERR_SHMLOCK:      zVal = "SQLITE_IOERR_SHMLOCK";      break;
    case SQLITE_LOCKED_SHAREDCACHE: zVal = "SQLITE_LOCKED_SHAREDCACHE"; break;
    case SQLITE_BUSY_RECOVERY:      zVal = "SQLITE_BUSY_RECOVERY";      break;
    case SQLITE_CANTOPEN_NOTEMPDIR: zVal = "SQLITE_CANTOPEN_NOTEMPDIR"; break;
    default: {
       sqlite3_snprintf(sizeof(zBuf), zBuf, "%d", rc);
       zVal = zBuf;
       break;
    }
  }
  vfsc_printf(pInfo, level, zFormat, zVal);
}

/*
** Append to a buffer.
*/
static void strappend(char *z, int *pI, const char *zAppend){
  int i = *pI;
  while( zAppend[0] ){ z[i++] = *(zAppend++); }
  z[i] = 0;
  *pI = i;
}

/*
** Decompression interface.
** Returns the output size in bytes.
*/
static int Decompress(const void* input, int* input_length, void* output, int max_output_length)
{
    int ret;
    unsigned output_length;
    z_stream strm;

    ++DecompressCount;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
    {
        return -1;
    }

    strm.avail_in = *input_length;
    strm.next_in = (Bytef*)input;
    strm.avail_out = max_output_length;
    strm.next_out = (Bytef*)output;
    ret = inflate(&strm, Z_NO_FLUSH);

    *input_length = strm.total_in;
    output_length = max_output_length - strm.avail_out;
    (void)inflateEnd(&strm);

    return output_length;
}

/*
** Compression interface.
** Returns the output size in bytes.
*/
static int Compress(const void* input, int input_length, void* output, int max_output_length)
{
    int ret;
    int output_length;
    z_stream strm;

    ++CompressCount;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, CompressionLevel);
    if (ret != Z_OK)
    {
        return -1;
    }

    strm.avail_in = input_length;
    strm.next_in = (Bytef*)input;
    strm.avail_out = max_output_length;
    strm.next_out = (Bytef*)output;
    ret = deflate(&strm, Z_FINISH);    /* no bad return value */

    output_length = max_output_length - strm.avail_out;
    (void)deflateEnd(&strm);

    {
#if 0
        char dout[ChunkSizeBytes];
        int size = ChunkSizeBytes;
        int dec = Decompress(output, &size, dout, ChunkSizeBytes);
        if (dec != input_length || memcmp(dout, input, input_length) != 0)
        {
            printf("ERROR: Decompression failure!\n");
            exit(1);
        }
#endif
    }

    return output_length;
}

static DWORD SetSparseRange(HANDLE hSparseFile, LONGLONG start, LONGLONG size)
{
    typedef struct _FILE_ZERO_DATA_INFORMATION {

        LARGE_INTEGER FileOffset;
        LARGE_INTEGER BeyondFinalZero;

    } FILE_ZERO_DATA_INFORMATION, *PFILE_ZERO_DATA_INFORMATION;

    FILE_ZERO_DATA_INFORMATION fzdi;
    DWORD dwTemp;
    BOOL res;

    if (size <= 0)
    {
        return 0;
    }

    // Specify the starting and the ending address (not the size) of the
    // sparse zero block
    fzdi.FileOffset.QuadPart = start;
    fzdi.BeyondFinalZero.QuadPart = start + size;

#define FSCTL_SET_ZERO_DATA             CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 50, METHOD_BUFFERED, FILE_WRITE_DATA) // FILE_ZERO_DATA_INFORMATION,

    // Mark the range as sparse zero block
    SetLastError(0);
    res = DeviceIoControl(hSparseFile,
        FSCTL_SET_ZERO_DATA,
        &fzdi,
        sizeof(fzdi),
        NULL,
        0,
        &dwTemp,
        NULL);

    if (res)
    {
        return 0; // Success
    }

    // return the error value
    return GetLastError();
}

static int FlushChunk(vfsc_file *pFile, vfsc_chunk *pChunk)
{
    vfsc_info *pInfo = pFile->pInfo;
    int rc = SQLITE_OK;
    if (pChunk != NULL && pChunk->origSize > 0 && pChunk->state != Empty && pChunk->state != Cached)
    {
        if (pChunk->state == Uncompressed)
        {
            // Compress...
            pChunk->compSize = Compress(pChunk->pOrigData, pChunk->origSize, pChunk->pCompData, ChunkSizeBytes);

            vfsc_printf(pInfo, Compression, "Compressed %d into %d bytes from offset %lld.\n", pChunk->origSize, pChunk->compSize, pChunk->offset);
        }

        // Write the chunk.
        vfsc_printf(pInfo, Compression, "> %s.Flush(%s,n=%d,ofst=%lld)",
            pInfo->zVfsName, pFile->zFName, pChunk->compSize, pChunk->offset);
        vfsc_printf(pInfo, Compression, "  Chunk=%lld, Data=%d bytes", pChunk->offset, pChunk->compSize);
        rc = pFile->pReal->pMethods->xWrite(pFile->pReal, pChunk->pCompData, pChunk->compSize, pChunk->offset);
        vfsc_print_errcode(pInfo, Compression, " -> %s\n", rc);

        SetSparseRange(pFile->hFile, pChunk->offset + pChunk->compSize, ChunkSizeBytes - pChunk->compSize);
        pChunk->state = Cached;

		vfsc_printf(pInfo, Trace, "> Sparse Range(%s, ofst=%lld, sz=%lld)\n",
					pFile->zFName, pChunk->offset + pChunk->compSize, ChunkSizeBytes - pChunk->compSize);
    }
    else
    {
        vfsc_printf(pInfo, Trace, "> SKIPPED Flush(%s,n=%d,ofst=%lld)\n",
            pInfo->zVfsName, pFile->zFName, pChunk->compSize, pChunk->offset);
    }

    return rc;
}

static int FlushCache(vfsc_file *pFile)
{
    int i;
    vfsc_info *pInfo = pFile->pInfo;
    if (pFile->hFile != INVALID_HANDLE_VALUE)
    {
        // Iterate over the complete cache and flush each chunk.
        for (i = 0; i < CACHE_SIZE_IN_CHUNKS; ++i)
        {
            int rc = FlushChunk(pFile, pInfo->pCache[i]);
            if (rc != SQLITE_OK)
            {
                return rc;
            }
        }
    }
   
    return SQLITE_OK;
}

static int ReadCache(vfsc_file *pFile, int chunkOffset, vfsc_chunk* pChunk)
{
    int rc = pFile->pReal->pMethods->xRead(pFile->pReal, pChunk->pCompData, ChunkSizeBytes, chunkOffset);
    if (rc == SQLITE_IOERR_READ || rc == SQLITE_FULL)
    {
        return rc;
    }

    if (pChunk->pCompData[0] == 0)
    {
        // The first byte should contain the length, hence can't be zero for compressed streams.
        pChunk->compSize = 0;
        pChunk->origSize = 0;
        pChunk->state = Empty;
        //memset(pChunk->pCompData, 0, ChunkSizeBytes);
    }
    else
    {
        pChunk->compSize = ChunkSizeBytes; //TODO: Check if we read less.
        pChunk->origSize = Decompress(pChunk->pCompData, &pChunk->compSize, pChunk->pOrigData, ChunkSizeBytes);
        pChunk->state = Cached;
        vfsc_printf(pFile->pInfo, Compression, "> Decompressed %d bytes from offset %d.\n", pChunk->origSize, chunkOffset);
    }

    pChunk->offset = chunkOffset;
    memset(pChunk->pOrigData + pChunk->origSize, 0, ChunkSizeBytes - pChunk->origSize);

    return rc;
}

/*
** Moves a cached chunk at index ahead by one position.
*/
static void MtfCachedChunk(vfsc_info *pInfo, int index)
{
    if (index > 0)
    {
        // Swap the target with the one ahead of it.
        vfsc_chunk *temp = pInfo->pCache[index - 1];
        pInfo->pCache[index - 1] = pInfo->pCache[index];
        pInfo->pCache[index] = temp;
    }
}

/*
** Finds the chunk in cache or reads from disk.
*/
static int GetCache(vfsc_file *pFile, sqlite_int64 chunkOffset, vfsc_chunk** pChunk)
{
    int i;
    int index = -1;
    vfsc_info *pInfo = pFile->pInfo;
    ++TotalHits;

    for (i = 0; i < CACHE_SIZE_IN_CHUNKS; ++i)
    {
        if (index < 0 && pInfo->pCache[i]->state == Empty)
        {
            index = i;
        }

        if (pInfo->pCache[i]->offset == chunkOffset)
        {
            // Found.
            ++CacheHits;
            *pChunk = pInfo->pCache[i];
            MtfCachedChunk(pInfo, i);
            return SQLITE_OK;
        }
    }

    // Not cached.
    if (index < 0)
    {
        // Flush the last entry since we'll remove it to make room.
        FlushChunk(pFile, pInfo->pCache[CACHE_SIZE_IN_CHUNKS - 1]);

        // Move the next-to-last to the last as if we moved the target ahead.
        MtfCachedChunk(pInfo, CACHE_SIZE_IN_CHUNKS - 1);

        // New target is next-to-last.
        index = CACHE_SIZE_IN_CHUNKS - 2;
    }

    // Cache the target chunk.
    *pChunk = pInfo->pCache[index];
    return ReadCache(pFile, chunkOffset, pInfo->pCache[index]);
}

/*
** Close an vfsc-file.
*/
static int vfscClose(sqlite3_file *pFile){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;

  FlushCache(p);

  if (p->hFile != INVALID_HANDLE_VALUE)
  {
      vfsc_printf(pInfo, Registeration, "Cache Hits: %d, Cache Misses: %d, Total: %d, Ratio: %.2f%%\n", CacheHits, TotalHits - CacheHits, TotalHits, 100.0 * CacheHits / (double)TotalHits);
      vfsc_printf(pInfo, Registeration, "Compressed: %d Chunks, Decompressed: %d Chunks\n", CompressCount, DecompressCount);
      CloseHandle(p->hFile);
      p->hFile = INVALID_HANDLE_VALUE;
  }

  vfsc_printf(pInfo, OpenClose, "%s.xClose(%s)", pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xClose(p->pReal);
  vfsc_print_errcode(pInfo, OpenClose, " -> %s\n", rc);
  if( rc==SQLITE_OK ){
    sqlite3_free((void*)p->base.pMethods);
    p->base.pMethods = 0;
  }
  return rc;
}

/*
** Read data from an vfsc-file.
*/
static int vfscRead(
  sqlite3_file *pFile,
  void *zBuf,
  int iAmt,
  sqlite_int64 iOfst
){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc = 0;
  sqlite_int64 chunkOffset;

  if (p->hFile != INVALID_HANDLE_VALUE)
  {
      vfsc_chunk *pChunk;
      chunkOffset = iOfst - (iOfst % ChunkSizeBytes);
      rc = GetCache(p, chunkOffset, &pChunk);

      // Copy the data from the cache.
      //TODO: Check if the required data crosses chunk boundaries.
      memcpy(zBuf, pChunk->pOrigData + (iOfst % ChunkSizeBytes), iAmt);

      vfsc_printf(pInfo, IoOps, "> %s.xRead(%s,n=%d,ofst=%lld)",
          pInfo->zVfsName, p->zFName, iAmt, iOfst);
      vfsc_printf(pInfo, IoOps, "  Chunk=%lld", chunkOffset);
      vfsc_print_errcode(pInfo, IoOps, " -> %s\n", rc);
  }
  else
  {
      vfsc_printf(pInfo, IoOps, "%s.xRead(%s,n=%d,ofst=%lld)",
          pInfo->zVfsName, p->zFName, iAmt, iOfst);
      rc = p->pReal->pMethods->xRead(p->pReal, zBuf, iAmt, iOfst);
      vfsc_print_errcode(pInfo, IoOps, " -> %s\n", rc);
  }

  return rc;
}

/*
** Write data to an vfsc-file.
*/
static int vfscWrite(
  sqlite3_file *pFile,
  const void *zBuf,
  int iAmt,
  sqlite_int64 iOfst
){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc = SQLITE_OK;
  sqlite_int64 chunkOffset;

  if (p->hFile != INVALID_HANDLE_VALUE)
  {
      // Get the cache chunk.
      vfsc_chunk *pChunk;
      int offsetInChunk = iOfst % ChunkSizeBytes;
      chunkOffset = iOfst - offsetInChunk;
      GetCache(p, chunkOffset, &pChunk);

      // Write the new data.
      memcpy(pChunk->pOrigData + offsetInChunk, zBuf, iAmt);
      pChunk->state = Uncompressed;
      pChunk->origSize = max(pChunk->origSize, offsetInChunk + iAmt);
      if (pChunk->origSize > ChunkSizeBytes)
      {
          printf("ERROR: CHUNK OVERRUN!!!!\n");
          exit(1);
      }

      vfsc_printf(pInfo, IoOps, "> %s.xWrite(%s,n=%d,ofst=%lld)",
          pInfo->zVfsName, p->zFName, iAmt, iOfst);
      vfsc_printf(pInfo, IoOps, "  Chunk=%lld, Data=%d bytes", chunkOffset, pChunk->compSize);
      vfsc_print_errcode(pInfo, IoOps, " -> %s\n", rc);
  }
  else
  {
      vfsc_printf(pInfo, IoOps, "%s.xWrite(%s,n=%d,ofst=%lld)",
          pInfo->zVfsName, p->zFName, iAmt, iOfst);
      rc = p->pReal->pMethods->xWrite(p->pReal, zBuf, iAmt, iOfst);
      vfsc_print_errcode(pInfo, IoOps, " -> %s\n", rc);
  }

  return rc;
}

/*
** Truncate an vfsc-file.
*/
static int vfscTruncate(sqlite3_file *pFile, sqlite_int64 size){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xTruncate(%s,%lld)", pInfo->zVfsName, p->zFName,
                  size);
  rc = p->pReal->pMethods->xTruncate(p->pReal, size);
  vfsc_printf(pInfo, NonIoOps, " -> %d\n", rc);
  return rc;
}

/*
** Sync an vfsc-file.
*/
static int vfscSync(sqlite3_file *pFile, int flags){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  int i;
  char zBuf[100];

  FlushCache(p);

  memcpy(zBuf, "|0", 3);
  i = 0;
  if( flags & SQLITE_SYNC_FULL )        strappend(zBuf, &i, "|FULL");
  else if( flags & SQLITE_SYNC_NORMAL ) strappend(zBuf, &i, "|NORMAL");
  if( flags & SQLITE_SYNC_DATAONLY )    strappend(zBuf, &i, "|DATAONLY");
  if( flags & ~(SQLITE_SYNC_FULL|SQLITE_SYNC_DATAONLY) ){
    sqlite3_snprintf(sizeof(zBuf)-i, &zBuf[i], "|0x%x", flags);
  }
  vfsc_printf(pInfo, NonIoOps, "%s.xSync(%s,%s)", pInfo->zVfsName, p->zFName,
                  &zBuf[1]);
  rc = p->pReal->pMethods->xSync(p->pReal, flags);
  vfsc_printf(pInfo, NonIoOps, " -> %d\n", rc);
  return rc;
}

/*
** Return the current file-size of an vfsc-file.
*/
static int vfscFileSize(sqlite3_file *pFile, sqlite_int64 *pSize){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xFileSize(%s)", pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xFileSize(p->pReal, pSize);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s,", rc);
  vfsc_printf(pInfo, NonIoOps, " size=%lld\n", *pSize);
  return rc;
}

/*
** Return the name of a lock.
*/
static const char *lockName(int eLock){
  const char *azLockNames[] = {
     "NONE", "SHARED", "RESERVED", "PENDING", "EXCLUSIVE"
  };
  if( eLock<0 || eLock>=sizeof(azLockNames)/sizeof(azLockNames[0]) ){
    return "???";
  }else{
    return azLockNames[eLock];
  }
}

/*
** Lock an vfsc-file.
*/
static int vfscLock(sqlite3_file *pFile, int eLock){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xLock(%s,%s)", pInfo->zVfsName, p->zFName,
                  lockName(eLock));
  rc = p->pReal->pMethods->xLock(p->pReal, eLock);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s\n", rc);
  return rc;
}

/*
** Unlock an vfsc-file.
*/
static int vfscUnlock(sqlite3_file *pFile, int eLock){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xUnlock(%s,%s)", pInfo->zVfsName, p->zFName,
                  lockName(eLock));
  rc = p->pReal->pMethods->xUnlock(p->pReal, eLock);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s\n", rc);
  return rc;
}

/*
** Check if another file-handle holds a RESERVED lock on an vfsc-file.
*/
static int vfscCheckReservedLock(sqlite3_file *pFile, int *pResOut){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xCheckReservedLock(%s,%d)",
                  pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xCheckReservedLock(p->pReal, pResOut);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s", rc);
  vfsc_printf(pInfo, NonIoOps, ", out=%d\n", *pResOut);
  return rc;
}

/*
** File control method. For custom operations on an vfsc-file.
*/
static int vfscFileControl(sqlite3_file *pFile, int op, void *pArg){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  char zBuf[100];
  char *zOp;
  switch( op ){
    case SQLITE_FCNTL_LOCKSTATE:    zOp = "LOCKSTATE";          break;
    case SQLITE_GET_LOCKPROXYFILE:  zOp = "GET_LOCKPROXYFILE";  break;
    case SQLITE_SET_LOCKPROXYFILE:  zOp = "SET_LOCKPROXYFILE";  break;
    case SQLITE_LAST_ERRNO:         zOp = "LAST_ERRNO";         break;
    case SQLITE_FCNTL_SIZE_HINT: {
      sqlite3_snprintf(sizeof(zBuf), zBuf, "SIZE_HINT,%lld",
                       *(sqlite3_int64*)pArg);
      zOp = zBuf;
      break;
    }
    case SQLITE_FCNTL_CHUNK_SIZE: {
      sqlite3_snprintf(sizeof(zBuf), zBuf, "CHUNK_SIZE,%d", *(int*)pArg);
      zOp = zBuf;
      break;
    }
    case SQLITE_FCNTL_FILE_POINTER: zOp = "FILE_POINTER";       break;
    case SQLITE_FCNTL_SYNC_OMITTED: {
        FlushCache(p);
        zOp = "SYNC_OMITTED";
        break;
    }
    case 0xca093fa0:                zOp = "DB_UNCHANGED";       break;
    default: {
      sqlite3_snprintf(sizeof zBuf, zBuf, "%d", op);
      zOp = zBuf;
      break;
    }
  }
  vfsc_printf(pInfo, NonIoOps, "%s.xFileControl(%s,%s)",
                  pInfo->zVfsName, p->zFName, zOp);
  rc = p->pReal->pMethods->xFileControl(p->pReal, op, pArg);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s\n", rc);
  return rc;
}

/*
** Return the sector-size in bytes for an vfsc-file.
*/
static int vfscSectorSize(sqlite3_file *pFile){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xSectorSize(%s)", pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xSectorSize(p->pReal);
  vfsc_printf(pInfo, NonIoOps, " -> %d\n", rc);
  return rc;
}

/*
** Return the device characteristic flags supported by an vfsc-file.
*/
static int vfscDeviceCharacteristics(sqlite3_file *pFile){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xDeviceCharacteristics(%s)",
                  pInfo->zVfsName, p->zFName);
  rc = p->pReal->pMethods->xDeviceCharacteristics(p->pReal);
  vfsc_printf(pInfo, NonIoOps, " -> 0x%08x\n", rc);
  return rc;
}

/*
** Shared-memory operations.
*/
static int vfscShmLock(sqlite3_file *pFile, int ofst, int n, int flags){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  char zLck[100];
  int i = 0;
  memcpy(zLck, "|0", 3);
  if( flags & SQLITE_SHM_UNLOCK )    strappend(zLck, &i, "|UNLOCK");
  if( flags & SQLITE_SHM_LOCK )      strappend(zLck, &i, "|LOCK");
  if( flags & SQLITE_SHM_SHARED )    strappend(zLck, &i, "|SHARED");
  if( flags & SQLITE_SHM_EXCLUSIVE ) strappend(zLck, &i, "|EXCLUSIVE");
  if( flags & ~(0xf) ){
     sqlite3_snprintf(sizeof(zLck)-i, &zLck[i], "|0x%x", flags);
  }
  vfsc_printf(pInfo, NonIoOps, "%s.xShmLock(%s,ofst=%d,n=%d,%s)",
                  pInfo->zVfsName, p->zFName, ofst, n, &zLck[1]);
  rc = p->pReal->pMethods->xShmLock(p->pReal, ofst, n, flags);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s\n", rc);
  return rc;
}
static int vfscShmMap(
  sqlite3_file *pFile,
  int iRegion,
  int szRegion,
  int isWrite,
  void volatile **pp
){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xShmMap(%s,iRegion=%d,szRegion=%d,isWrite=%d,*)",
                  pInfo->zVfsName, p->zFName, iRegion, szRegion, isWrite);
  rc = p->pReal->pMethods->xShmMap(p->pReal, iRegion, szRegion, isWrite, pp);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s\n", rc);
  return rc;
}
static void vfscShmBarrier(sqlite3_file *pFile){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  vfsc_printf(pInfo, NonIoOps, "%s.xShmBarrier(%s)\n", pInfo->zVfsName, p->zFName);
  p->pReal->pMethods->xShmBarrier(p->pReal);
}
static int vfscShmUnmap(sqlite3_file *pFile, int delFlag){
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = p->pInfo;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xShmUnmap(%s,delFlag=%d)",
                  pInfo->zVfsName, p->zFName, delFlag);
  rc = p->pReal->pMethods->xShmUnmap(p->pReal, delFlag);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s\n", rc);
  return rc;
}

/*
** Opens a file in sparse-mode.
*/
static HANDLE OpenSparseFile(const char *zName)
{
    DWORD dwTemp;
    DWORD res;
    void *zConverted;              /* Filename in OS encoding */
    HANDLE hSparseFile;

    /* Convert the filename to the system encoding. */
    zConverted = convertUtf8Filename(zName);
    if (zConverted == NULL)
	{
        return INVALID_HANDLE_VALUE;
    }

    hSparseFile = CreateFileW((WCHAR*)zConverted,
        GENERIC_READ|GENERIC_WRITE,
		FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING, // We expect the default VFS to have opened the file already.
		FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (hSparseFile == INVALID_HANDLE_VALUE)
    {
        return INVALID_HANDLE_VALUE;
    }

    SetLastError(0);
    res = DeviceIoControl(hSparseFile,
                            FSCTL_SET_SPARSE,
                            NULL,
                            0,
                            NULL,
                            0,
                            &dwTemp,
                            NULL);
    if (res == 0)
    {
        CloseHandle(hSparseFile);
        hSparseFile = INVALID_HANDLE_VALUE;
    }

    return hSparseFile;
}

/*
** Checks whether or not a database file is compressed by us.
*/
static int IsCompressed(HANDLE hFile)
{
    char buffer[100];
    DWORD read = 0;
    LONG upper = 0;

    ReadFile(hFile, buffer, 14, &read, NULL);
    SetFilePointer(hFile, 0, &upper, FILE_BEGIN);
    if (read == 0)
    {
        // Empty file, just start supporting compression.
        return 1;
    }

    buffer[read] = 0;
    return (strcmp(buffer, "SQLite format ") != 0);
}

/*
** Open an vfsc file handle.
*/
static int vfscOpen(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_file *pFile,
  int flags,
  int *pOutFlags
){
  int rc;
  vfsc_file *p = (vfsc_file *)pFile;
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  p->pInfo = pInfo;
  p->zFName = zName ? fileTail(zName) : "<temp>";
  p->pReal = (sqlite3_file *)&p[1];
  p->hFile = INVALID_HANDLE_VALUE;
  rc = pRoot->xOpen(pRoot, zName, p->pReal, flags, pOutFlags);

  vfsc_printf(pInfo, OpenClose, "%s.xOpen(%s,flags=0x%x)",
      pInfo->zVfsName, p->zFName, flags);

  if( p->pReal->pMethods ){
    sqlite3_io_methods *pNew = (sqlite3_io_methods*)sqlite3_malloc( sizeof(*pNew) );
    const sqlite3_io_methods *pSub = p->pReal->pMethods;
    memset(pNew, 0, sizeof(*pNew));
    pNew->iVersion = pSub->iVersion;
    pNew->xClose = vfscClose;
    pNew->xRead = vfscRead;
    pNew->xWrite = vfscWrite;
    pNew->xTruncate = vfscTruncate;
    pNew->xSync = vfscSync;
    pNew->xFileSize = vfscFileSize;
    pNew->xLock = vfscLock;
    pNew->xUnlock = vfscUnlock;
    pNew->xCheckReservedLock = vfscCheckReservedLock;
    pNew->xFileControl = vfscFileControl;
    pNew->xSectorSize = vfscSectorSize;
    pNew->xDeviceCharacteristics = vfscDeviceCharacteristics;
    if( pNew->iVersion>=2 ){
      pNew->xShmMap = pSub->xShmMap ? vfscShmMap : 0;
      pNew->xShmLock = pSub->xShmLock ? vfscShmLock : 0;
      pNew->xShmBarrier = pSub->xShmBarrier ? vfscShmBarrier : 0;
      pNew->xShmUnmap = pSub->xShmUnmap ? vfscShmUnmap : 0;
    }
    pFile->pMethods = pNew;
  }
  vfsc_print_errcode(pInfo, OpenClose, " -> %s", rc);
  if( pOutFlags ){
    vfsc_printf(pInfo, OpenClose, ", outFlags=0x%x\n", *pOutFlags);
  }else{
    vfsc_printf(pInfo, OpenClose, "\n");
  }

  if (rc == SQLITE_OK &&
      ((flags & 0xFFFFFF00) == SQLITE_OPEN_MAIN_DB))
  {
      // Now reopen the file and mark it sparse.
      p->hFile = OpenSparseFile(zName);
      if (p->hFile != INVALID_HANDLE_VALUE)
      {
          int compressed = IsCompressed(p->hFile);
	      vfsc_printf(pInfo, OpenClose, "> %s.xOpen(%s) -> %x", pInfo->zVfsName, p->zFName, GetLastError());
          vfsc_printf(pInfo, OpenClose, " -> %s\n", compressed ? "Compressed" : "Plain");
          if (!compressed)
          {
              CloseHandle(p->hFile);
              p->hFile = INVALID_HANDLE_VALUE;
          }
      }

      if (p->hFile == INVALID_HANDLE_VALUE)
	  {
			vfsc_printf(pInfo, OpenClose, "> %s.xOpen(%s) -> Failed to open/create sparse file! Last Error: 0x%x.\n", pInfo->zVfsName, p->zFName, GetLastError());
	  }
  }

  return rc;
}

/*
** Delete the file located at zPath. If the dirSync argument is true,
** ensure the file-system modifications are synced to disk before
** returning.
*/
static int vfscDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xDelete(\"%s\",%d)",
                  pInfo->zVfsName, zPath, dirSync);
  rc = pRoot->xDelete(pRoot, zPath, dirSync);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s\n", rc);
  return rc;
}

/*
** Test for access permissions. Return true if the requested permission
** is available, or false otherwise.
*/
static int vfscAccess(
  sqlite3_vfs *pVfs,
  const char *zPath,
  int flags,
  int *pResOut
){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xAccess(\"%s\",%d)",
                  pInfo->zVfsName, zPath, flags);
  rc = pRoot->xAccess(pRoot, zPath, flags, pResOut);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s", rc);
  vfsc_printf(pInfo, NonIoOps, ", out=%d\n", *pResOut);
  return rc;
}

/*
** Populate buffer zOut with the full canonical pathname corresponding
** to the pathname in zPath. zOut is guaranteed to point to a buffer
** of at least (DEVSYM_MAX_PATHNAME+1) bytes.
*/
static int vfscFullPathname(
  sqlite3_vfs *pVfs,
  const char *zPath,
  int nOut,
  char *zOut
){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  int rc;
  vfsc_printf(pInfo, NonIoOps, "%s.xFullPathname(\"%s\")",
                  pInfo->zVfsName, zPath);
  rc = pRoot->xFullPathname(pRoot, zPath, nOut, zOut);
  vfsc_print_errcode(pInfo, NonIoOps, " -> %s", rc);
  vfsc_printf(pInfo, NonIoOps, ", out=\"%.*s\"\n", nOut, zOut);
  return rc;
}

/*
** Open the dynamic library located at zPath and return a handle.
*/
static void *vfscDlOpen(sqlite3_vfs *pVfs, const char *zPath){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfsc_printf(pInfo, NonIoOps, "%s.xDlOpen(\"%s\")\n", pInfo->zVfsName, zPath);
  return pRoot->xDlOpen(pRoot, zPath);
}

/*
** Populate the buffer zErrMsg (size nByte bytes) with a human readable
** utf-8 string describing the most recent error encountered associated
** with dynamic libraries.
*/
static void vfscDlError(sqlite3_vfs *pVfs, int nByte, char *zErrMsg){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfsc_printf(pInfo, NonIoOps, "%s.xDlError(%d)", pInfo->zVfsName, nByte);
  pRoot->xDlError(pRoot, nByte, zErrMsg);
  vfsc_printf(pInfo, NonIoOps, " -> \"%s\"", zErrMsg);
}

/*
** Return a pointer to the symbol zSymbol in the dynamic library pHandle.
*/
static void (*vfscDlSym(sqlite3_vfs *pVfs,void *p,const char *zSym))(void){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfsc_printf(pInfo, NonIoOps, "%s.xDlSym(\"%s\")\n", pInfo->zVfsName, zSym);
  return pRoot->xDlSym(pRoot, p, zSym);
}

/*
** Close the dynamic library handle pHandle.
*/
static void vfscDlClose(sqlite3_vfs *pVfs, void *pHandle){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfsc_printf(pInfo, NonIoOps, "%s.xDlOpen()\n", pInfo->zVfsName);
  pRoot->xDlClose(pRoot, pHandle);
}

/*
** Populate the buffer pointed to by zBufOut with nByte bytes of
** random data.
*/
static int vfscRandomness(sqlite3_vfs *pVfs, int nByte, char *zBufOut){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  vfsc_printf(pInfo, NonIoOps, "%s.xRandomness(%d)\n", pInfo->zVfsName, nByte);
  return pRoot->xRandomness(pRoot, nByte, zBufOut);
}

/*
** Sleep for nMicro microseconds. Return the number of microseconds
** actually slept.
*/
static int vfscSleep(sqlite3_vfs *pVfs, int nMicro){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xSleep(pRoot, nMicro);
}

/*
** Return the current time as a Julian Day number in *pTimeOut.
*/
static int vfscCurrentTime(sqlite3_vfs *pVfs, double *pTimeOut){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xCurrentTime(pRoot, pTimeOut);
}
static int vfscCurrentTimeInt64(sqlite3_vfs *pVfs, sqlite3_int64 *pTimeOut){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xCurrentTimeInt64(pRoot, pTimeOut);
}

/*
** Return th3 emost recent error code and message
*/
static int vfscGetLastError(sqlite3_vfs *pVfs, int iErr, char *zErr){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xGetLastError(pRoot, iErr, zErr);
}

/*
** Override system calls.
*/
static int vfscSetSystemCall(
  sqlite3_vfs *pVfs,
  const char *zName,
  sqlite3_syscall_ptr pFunc
){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xSetSystemCall(pRoot, zName, pFunc);
}
static sqlite3_syscall_ptr vfscGetSystemCall(
  sqlite3_vfs *pVfs,
  const char *zName
){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xGetSystemCall(pRoot, zName);
}
static const char *vfscNextSystemCall(sqlite3_vfs *pVfs, const char *zName){
  vfsc_info *pInfo = (vfsc_info*)pVfs->pAppData;
  sqlite3_vfs *pRoot = pInfo->pRootVfs;
  return pRoot->xNextSystemCall(pRoot, zName);
}


/*
** Clients invoke this routine to construct a new vfs-compress shim.
**
** Return SQLITE_OK on success.
**
** SQLITE_NOMEM is returned in the case of a memory allocation error.
** SQLITE_NOTFOUND is returned if zOldVfsName does not exist.
*/
SQLITE_API int sqlite3_compress(
   int trace,                  /* See TraceLevel. 0 to disable. */
   int compressionLevel,       /* The compression level: -1 for default, 1 fastest, 9 best */
   int chunkSizeBytes          /* The size of the compression chunk in bytes: -1 for default */
){
  sqlite3_vfs *pNew;
  sqlite3_vfs *pRoot;
  vfsc_info *pInfo;
  int nName;
  int nByte;
  int i;

  int chunkSize = chunkSizeBytes / COMPRESION_UNIT_SIZE_BYTES;
  ChunkSizeBytes = chunkSize <= 0 ? DEF_CHUNK_SIZE_BYTES : (chunkSize * COMPRESION_UNIT_SIZE_BYTES);

  CompressionLevel = compressionLevel;

  // Find the windows VFS.
  pRoot = sqlite3_vfs_find("win32");
  if( pRoot==0 ) return SQLITE_NOTFOUND;
  nName = strlen("vfscompress");
  nByte = sizeof(*pNew) + sizeof(*pInfo) + nName + 1;
  pNew = (sqlite3_vfs*)sqlite3_malloc( nByte );
  if( pNew==0 ) return SQLITE_NOMEM;
  memset(pNew, 0, nByte);
  pInfo = (vfsc_info*)&pNew[1];
  pNew->iVersion = pRoot->iVersion;
  pNew->szOsFile = pRoot->szOsFile + sizeof(vfsc_file);
  pNew->mxPathname = pRoot->mxPathname;
  pNew->zName = (char*)&pInfo[1];
  memcpy((char*)&pInfo[1], "vfscompress", nName+1);
  pNew->pAppData = pInfo;
  pNew->xOpen = vfscOpen;
  pNew->xDelete = vfscDelete;
  pNew->xAccess = vfscAccess;
  pNew->xFullPathname = vfscFullPathname;
  pNew->xDlOpen = pRoot->xDlOpen==0 ? 0 : vfscDlOpen;
  pNew->xDlError = pRoot->xDlError==0 ? 0 : vfscDlError;
  pNew->xDlSym = pRoot->xDlSym==0 ? 0 : vfscDlSym;
  pNew->xDlClose = pRoot->xDlClose==0 ? 0 : vfscDlClose;
  pNew->xRandomness = vfscRandomness;
  pNew->xSleep = vfscSleep;
  pNew->xCurrentTime = vfscCurrentTime;
  pNew->xGetLastError = pRoot->xGetLastError==0 ? 0 : vfscGetLastError;
  if( pNew->iVersion>=2 ){
    pNew->xCurrentTimeInt64 = pRoot->xCurrentTimeInt64==0 ? 0 :
                                   vfscCurrentTimeInt64;
    if( pNew->iVersion>=3 ){
      pNew->xSetSystemCall = pRoot->xSetSystemCall==0 ? 0 :
                                   vfscSetSystemCall;
      pNew->xGetSystemCall = pRoot->xGetSystemCall==0 ? 0 :
                                   vfscGetSystemCall;
      pNew->xNextSystemCall = pRoot->xNextSystemCall==0 ? 0 :
                                   vfscNextSystemCall;
    }
  }
  pInfo->pRootVfs = pRoot;
  pInfo->xOut = (int(*)(const char*,void*))fputs;
  pInfo->pOutArg = stderr;
  pInfo->zVfsName = pNew->zName;
  pInfo->pTraceVfs = pNew;
  pInfo->trace = trace >= Maximum ? Maximum : (trace < None ? DEFAULT_TRACE_LEVEL : trace);

  memset(pInfo->pCache, 0, sizeof(pInfo->pCache));
  for (i = 0; i < CACHE_SIZE_IN_CHUNKS; ++i)
  {
      pInfo->pCache[i] = (vfsc_chunk*)sqlite3_malloc(sizeof(vfsc_chunk));
      if(pInfo->pCache[i] == NULL)
      {
         return SQLITE_NOMEM;
      }

      memset(pInfo->pCache[i], 0, sizeof(vfsc_chunk));
      pInfo->pCache[i]->state = Empty;
      pInfo->pCache[i]->offset = -1;
      pInfo->pCache[i]->origSize = -1;
      pInfo->pCache[i]->pCompData = (char*)sqlite3_malloc( ChunkSizeBytes );
      pInfo->pCache[i]->pOrigData = (char*)sqlite3_malloc( ChunkSizeBytes );
  }
  
  vfsc_printf(pInfo, Registeration, "%s.enabled_for(\"%s\") - Compression Chunk Size: %d KBytes, Level: %d, Cache: %d Chunks.\n",
      pInfo->zVfsName, pRoot->zName, ChunkSizeBytes / 1024, CompressionLevel, CACHE_SIZE_IN_CHUNKS);

  return sqlite3_vfs_register(pNew, 1);
}

#else

SQLITE_API int sqlite3_compress(
   int trace,                  /* See TraceLevel. 0 to disable. */
   int compressionLevel,       /* The compression level: -1 for default, 1 fastest, 9 best */
   int chunkSizeBytes          /* The size of the compression chunk in bytes: -1 for default */
){
  return SQLITE_OK;
}

#endif /* SQLITE_OS_WIN */
