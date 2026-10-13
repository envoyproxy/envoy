Fixed a bug where shadowed (mirrored) requests did not honor dynamically-set subset load balancer
metadata match criteria. Previously only the static route-level ``metadata_match`` was forwarded to
the shadow cluster, so subset selectors set at runtime (for example via the header-to-metadata
filter writing ``envoy.lb`` dynamic metadata, or connection-level ``envoy.lb`` metadata) were
ignored and the shadow request could be routed to hosts outside the intended subset. The shadow
stream now inherits the downstream request's ``envoy.lb`` dynamic metadata (request-level merged
over connection-level), matching the main request's subset selection. This behavior can be
temporarily reverted by setting the runtime guard
``envoy.reloadable_features.shadow_policy_inherit_dynamic_metadata`` to ``false``.
