
    
    set script ""
    while {![eof stdin]} {
      flush stdout
      set line [gets stdin]
      if { $line == "OVER" } {
        set rc [catch {eval $script} result]
        puts [list $rc $result]
        puts OVER
        flush stdout
        set script ""
      } else {
        append script $line
        append script "\n"
      }
    }
  
