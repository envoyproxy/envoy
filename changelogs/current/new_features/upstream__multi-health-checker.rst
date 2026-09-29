Added support for configuring :ref:`multiple health checkers <config_health_checkers_multi>` on a
single cluster. When multiple health checks are specified, a host is considered healthy only when all
checkers report it as healthy. Each health check entry requires a unique ``name`` field for
per-checker stats tracking.
