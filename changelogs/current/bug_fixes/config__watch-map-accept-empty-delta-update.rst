Fixed a bug in ``WatchMap`` where delta xDS watches using ``accept()`` prefix filtering (such as
VHDS) did not receive an update when the initial wildcard ``DeltaDiscoveryResponse`` was empty or
contained only resources outside the accepted prefix. Those watches now receive an empty initial
update so VHDS warming completes instead of stalling until ``initial_fetch_timeout``.
