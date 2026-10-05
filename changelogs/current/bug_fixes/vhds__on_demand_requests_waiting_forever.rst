Fixed on-demand VHDS requests that could wait forever and leave the downstream request hanging:
a repeated request for an already-answered alias re-subscribed and waited for a response the
server may never re-send, and requests queued against an update superseded while warming were
never resolved. Envoy now tracks which names and aliases the server has answered and resolves
such requests locally from the published route configuration. This behavior can be temporarily
reverted by setting the runtime guard ``envoy.reloadable_features.vhds_answered_alias_cache``
to ``false``.
