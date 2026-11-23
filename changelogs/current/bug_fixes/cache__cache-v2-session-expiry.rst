Fixed the ``cache_v2`` filter so that idle cache sessions expire. Each lookup checked only the first
session in the table, so idle sessions and the ``cache_sessions_entries`` gauge could grow while
that session stayed in use.
