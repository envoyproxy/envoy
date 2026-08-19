Fixed a bug in the reverse tunnel downstream socket interface where the per-host and per-cluster
``connected`` and ``connecting`` gauges were not decremented on some connection teardown paths.
