
  sqlite3_test_control_pending_byte 0x0010000
  sqlite3 db test.db
  db eval {
    PRAGMA journal_mode=persist;
    PRAGMA default_cache_size=20;
    BEGIN;
    CREATE TABLE t3 AS SELECT * FROM t2;
    DELETE FROM t2;
  }
  sqlite_abort

