
  proc explain_query_plan {db sql} {
    set stmt [sqlite3_prepare_v2 db $sql -1 DUMMY]
    print_explain_query_plan $stmt
    sqlite3_finalize $stmt
  }
  sqlite3 db test.db
  explain_query_plan db {
  SELECT a FROM t1 EXCEPT SELECT d FROM t2 ORDER BY 1
}
  db close
  exit

