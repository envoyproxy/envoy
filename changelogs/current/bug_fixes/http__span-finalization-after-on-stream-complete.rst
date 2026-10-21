Fixed downstream span finalization to run after ``onStreamComplete`` so filter-set span tags
are exported instead of silently dropped.
