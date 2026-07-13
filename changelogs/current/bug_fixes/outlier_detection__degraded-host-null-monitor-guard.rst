Fixed a crash (null-pointer dereference) in outlier detection where the cross-thread degrade path
(``setHostDegradedMainThread``) could dereference a null host monitor when a host was removed from
the cluster after a worker thread posted a degrade event but before the main thread ran it. The
degrade path now skips a removed host, mirroring the existing guard in the eject path.
