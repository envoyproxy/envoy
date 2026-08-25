Changes the default value of ``envoy.restart_features.shared_cares_dns_resolver`` to ``false``. This disabled the shared dns resolver
that can cause a race when createDnsResolver() is called from a workerthread in the DnsFilter. Do not turn this back on until this bug is
fixed if DnsFilter is used.
