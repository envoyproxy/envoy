Fixed idle downstream HTTP connections to follow the configured :option:`--drain-strategy`
during server draining. With the ``immediate`` drain strategy, idle connections begin draining
as soon as the drain sequence begins, rather than waiting for the normal connection idle timeout.
Connections that have not received a request close after the HTTP connection manager's configured
:ref:`drain timeout
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`.
With the ``gradual`` drain strategy, idle connections continue to drain proactively in the last
third of the listener drain time. Proactive draining remains enabled for both drain strategies.
HTTP/3 connections wait for their first stream before starting the HTTP drain sequence, allowing
new connections to complete their handshake during draining. This change can be reverted by setting
``envoy.reloadable_features.use_connection_event_drain`` to ``false``.
