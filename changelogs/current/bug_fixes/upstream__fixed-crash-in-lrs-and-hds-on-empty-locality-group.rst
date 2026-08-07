Fixed a crash (SIGSEGV) in ``LoadStatsReporter`` when ``HostsPerLocalityImpl::filter()`` produced
an empty locality group after the last host in a locality was removed by EDS and confirmed
unreachable by active health checking. The reporter now skips empty locality groups instead of
dereferencing ``hosts[0]``.
