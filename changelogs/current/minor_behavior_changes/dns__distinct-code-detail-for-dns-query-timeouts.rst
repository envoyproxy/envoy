When the runtime guard ``envoy.reloadable_features.dns_cache_distinguish_resolution_timeout`` is
enabled, the dynamic forward proxy DNS cache emits the ``dns_resolution_timeout`` response code
detail for DNS query timeouts instead of ``dns_resolution_failure``. This allows timeouts, which
are transient, to be distinguished from other resolution failures such as NXDOMAIN. The guard is
disabled by default.
