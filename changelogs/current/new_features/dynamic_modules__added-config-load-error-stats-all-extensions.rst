Dynamic module extensions now emit counters when loading their configuration fails, so that
failures (which are otherwise either only surfaced to the control plane or, for the HTTP filter's
remote fetch path, silently fail open) are observable. The counters are emitted on the server-wide
stats scope under the ``dynamic_modules.`` prefix (the server scope is used so the counters survive
a rejected listener configuration update): ``module_load_error`` (the module could not be loaded
— missing/invalid source, ``dlopen`` failure, by-name lookup miss, or a required ABI symbol could
not be resolved), ``config_init_error`` (the module loaded but initializing the in-module
configuration failed, e.g. ``on_*_config_new`` returned null), ``remote_fetch_error`` (a remote
module source failed to fetch or load, including NACK-mode cache misses; HTTP filter only) and
``per_route_config_error`` (a per-route configuration failed to load; HTTP filter only). Each
counter carries a ``config_name`` tag set to the configured name of the extension instance —
for example ``filter_name``, ``transport_socket_name``, ``lb_policy_name``, ``tracer_name`` or
``cluster_name`` (falling back to ``default`` when the extension has no per-instance name, as
for the UDP listener filter) — so failures can be attributed to a specific extension instance.
The ``upstreams/http`` bridge is not yet instrumented because its module is loaded lazily on
the data path where no server-wide stats scope is available.
