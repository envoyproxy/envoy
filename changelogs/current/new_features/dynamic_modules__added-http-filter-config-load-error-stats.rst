The dynamic modules HTTP filter now emits counters when loading a filter configuration fails, so
that failures (which are otherwise either only surfaced to the control plane or, for the remote
fetch path, silently fail open) are observable. The counters are emitted on the server-wide stats
scope under the ``dynamic_modules.`` prefix (the server scope is used so the counters survive a
rejected listener configuration update): ``module_load_error`` (the module could not be loaded
— missing/invalid source, ``dlopen`` failure, by-name lookup miss), ``config_init_error`` (the
module loaded but the HTTP filter ABI symbols could not be resolved or
``on_http_filter_config_new`` returned null), ``remote_fetch_error`` (a remote module source failed
to fetch or load, including NACK-mode cache misses), and ``per_route_config_error`` (a per-route
configuration failed to load). Each counter carries a ``config_name`` tag set to the configured
filter name (``filter_name``), so failures can be attributed to a specific filter.
