Fixed a bug in :ref:`tcp_proxy <config_network_filters_tcp_proxy>` where the idle timer was
disabled when the upstream connection closed, leaving a downstream connection in ``FlushWrite``
unbounded by ``idle_timeout`` if the client stopped reading. The idle timer now remains armed
until the downstream connection closes or an upstream RST is propagated via ``AbortReset``. This
behavior can be reverted by setting
``envoy.reloadable_features.tcp_proxy_keep_idle_timer_on_upstream_close`` to ``false``.
