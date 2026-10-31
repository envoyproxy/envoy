Fixed an issue in outlier detection where interval bucket rotation could split total and success
request counters across adjacent intervals, causing reported success rates >100% and spurious host
ejections. Also bounds per-interval request counters to 31 bits to prevent modular integer wrap.
