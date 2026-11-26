Delta RDS updates that remove a route configuration now unload it for ``on_demand`` scoped routes.
When a scope's ``route_configuration_name`` appears in ``removed_resources``, Envoy drops the
scope's RDS provider (unsubscribing once no other scope shares it), clears its cached route
configuration, and resumes any requests paused on that scope so they re-resolve and start a new
on-demand fetch. Eager scopes and non-scoped RDS subscriptions continue to ignore removals.
