Fixed the c-ares resolver reporting a successful empty result when one address family returned no records
and the other lookup failed in ``AUTO`` or ``V4_PREFERRED`` mode. Such failures no longer remove existing
hosts from strict DNS clusters. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.cares_dual_resolution_preserve_failure`` to ``false``.
