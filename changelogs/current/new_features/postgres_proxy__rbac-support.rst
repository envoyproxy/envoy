Added support for connection-level RBAC rules in the Postgres proxy filter. Rules can use startup
attributes when Envoy can inspect the StartupMessage, and can use connection properties and downstream
TLS peer identity when available. Added ``authorization_allowed`` and ``authorization_denied`` counters.
