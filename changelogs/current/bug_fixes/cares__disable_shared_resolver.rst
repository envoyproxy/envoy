Changes the default value of ``envoy.restart_features.shared_cares_dns_resolver`` to ``false``. This disables the shared DNS resolver
that can cause a race when ``createDnsResolver()`` is called from a worker thread in the ``DnsFilter``. Do not turn this back on until this
bug is fixed if ``DnsFilter`` is used.
