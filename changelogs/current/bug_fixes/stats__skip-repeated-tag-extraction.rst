CDS, RDS, and LDS updates that re-declare existing counters, gauges, histograms, and text readouts use less CPU,
especially with custom ``stats_tags``. Stat names and tags are unchanged. Skipping repeated tag
extraction also makes ``server.stats_recent_lookups`` grow more slowly.
Workers also use less CPU the first time they record each histogram.
