Fixed on-demand VHDS requests that could wait forever and leave the downstream request hanging:
a repeated request for an already-answered alias waited for a response that never came, because
the alias is already part of the delta subscription, so the xDS layer deduplicated the repeated
interest and sent no new ``DeltaDiscoveryRequest`` at all (and even when a duplicate
``resource_names_subscribe`` entry was still sent on the wire, major control planes don't
implement the mandated re-send of an already-known resource). Requests queued against an update
superseded while warming were also never resolved. Envoy now tracks which explicitly requested
aliases the server has answered and resolves repeated requests for them locally from the
published route configuration. Ids the server volunteered without a request are not answered
locally, because no subscription guarantees further pushes for them. This behavior can be
temporarily reverted by setting the runtime guard
``envoy.reloadable_features.vhds_answered_alias_cache`` to ``false``.
