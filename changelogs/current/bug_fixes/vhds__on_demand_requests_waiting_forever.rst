Fixed on-demand VHDS requests that could wait forever and leave the downstream request hanging:
a repeated request for an already-answered alias re-subscribed and waited for a response the
server may never re-send, and requests queued against an update superseded while warming were
never resolved. Envoy now tracks which explicitly requested aliases the server has answered and
resolves repeated requests for them locally from the published route configuration. Ids the
server volunteered without a request are not answered locally, because no subscription
guarantees further pushes for them. This behavior can be temporarily
reverted by setting the runtime guard ``envoy.reloadable_features.vhds_answered_alias_cache``
to ``false``.
