Reduced the cost of releasing references to stats: non-final reference-count decrements no
longer take the allocator's lock (for counters, gauges and text readouts) or the store's
histogram lock (for histograms), and stat reference-count operations use standard
reference-counting memory orderings instead of sequentially-consistent ones. Processes with
very large numbers of stats should see substantially lower and more consistent flush times
(roughly 30-40% in the stats flush benchmark at millions of stats). There are no visible
behavioral changes.
