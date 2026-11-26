Added support for connection-level RBAC rules in the Postgres proxy filter. Rules can use startup
attributes when Envoy can inspect the StartupMessage, and can use connection properties and downstream
TLS peer identity when available. Added ``authorization_allowed``, ``authorization_denied``, and
``protocol_rejected`` counters.
Policies can distinguish cancellations from connection startup using filter-generated packet type metadata.
Enabling RBAC preserves empty startup values and normalizes effective user and database names, which
can change the database suffix in SQL metadata keys. Even when RBAC is disabled, the filter closes
connections that send extra data with an SSL request that Envoy terminates, and recognized cancel
requests no longer count as encrypted sessions.
