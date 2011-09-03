sqlite3_test_control_pending_byte 65536

     sqlite3 db2 test2.db
     db2 eval {
        BEGIN;
        INSERT INTO t2 VALUES(2);
     }
     sqlite3 db test.db
     db timeout 1000000
     db eval {
        INSERT INTO t1 VALUES(2);
     }
     db close
     db2 eval COMMIT
     exit
  
