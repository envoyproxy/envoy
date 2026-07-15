Fixed missing attempted upstream host tracking in the
:ref:`UDP proxy <config_udp_listener_filters_udp_proxy>` tunneling path.
``TunnelingConnectionPoolImpl`` now records the upstream host via
``StreamInfo::UpstreamInfo::addUpstreamHostAttempted`` on both pool ready and pool
failure, so the ``%UPSTREAM_HOSTS_ATTEMPTED%``,
``%UPSTREAM_HOST_NAMES_ATTEMPTED_WITHOUT_PORT%`` and related access log formatters are
populated for tunneled (UDP-over-HTTP) sessions, matching the behavior of the
non-tunneling UDP proxy and TCP proxy.
