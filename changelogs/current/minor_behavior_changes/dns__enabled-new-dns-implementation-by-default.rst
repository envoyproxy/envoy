Runtime guard ``envoy.reloadable_features.enable_new_dns_implementation`` is now enabled by
default. This activates the new DNS implementation, a merged implementation of strict and
logical DNS clusters. It can be temporarily reverted by setting the runtime guard to ``false``.
