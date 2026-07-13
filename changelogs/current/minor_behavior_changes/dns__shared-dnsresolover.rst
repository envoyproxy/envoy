Share DNSResolver if c-ares config is the same. This is so that qcache can be enabled and shared
across clusters. This behavior can be reverted by setting the runtime guard
``envoy.restart_features.shared_cares_dns_resolver`` to ``false``.
