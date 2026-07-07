Extended the lock-free non-final reference-count release fast path (added for counters,
gauges and text readouts in a previous change) to histograms, which previously took the
store's histogram lock on every release, and switched stat reference-count operations to
standard reference-counting memory orderings instead of sequentially-consistent ones.
Processes with very large numbers of stats should see moderately lower flush times on top
of the earlier change (roughly a further 10-15% in the stats flush benchmark at millions of
stats). There are no visible behavioral changes.
