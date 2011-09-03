
  set l [open log w]
  set script ""
  while {![eof stdin]} {
    flush stdout
    set line [gets stdin]
    puts $l "READ $line"
    if { $line == "OVER" } {
      catch {eval $script} result
      puts $result
      puts $l "WRITE $result"
      puts OVER
      puts $l "WRITE OVER"
      flush stdout
      set script ""
    } else {
      append script $line
      append script " ; "
    }
  }
  close $l

