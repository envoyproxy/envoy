CDS, RDS, and LDS updates that re-declare existing counters, gauges, and text readouts use less CPU,
especially with custom ``stats_tags``. Stat names and tags are unchanged. Skipping repeated tag
extraction also makes ``server.stats_recent_lookups`` grow more slowly.
