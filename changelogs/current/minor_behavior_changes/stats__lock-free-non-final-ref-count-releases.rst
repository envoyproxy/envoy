Reduced the cost of the periodic stats flush: releasing a non-final reference to a stat no
longer takes the allocator's lock (the store's histogram lock for histograms), and stat
reference-count operations now use standard reference-counting memory orderings instead of
sequentially-consistent ones. Flushing metrics to sinks briefly references every stat, so
processes with very large numbers of stats should see substantially lower and more
consistent flush times (roughly 35-40% lower in the stats flush benchmark at millions of
stats). There are no visible behavioral changes.
