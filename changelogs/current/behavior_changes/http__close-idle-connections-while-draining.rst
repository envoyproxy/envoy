With :option:`--drain-strategy` set to ``immediate``, downstream HTTP connections now start draining
on drain notification if idle, or when their last stream ends, without waiting for the idle timeout
or proactive drain window. Connections awaiting their first request close after the configured
:ref:`drain timeout
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`.
These timeout closures use the ``on_drain_deadline`` close reason.
HTTP/1 requests that race with connection close can still fail.
Existing ``gradual`` idle drain timing and proactive scheduling for both strategies are preserved.
For HTTP/3, ``immediate`` draining waits for the first stream; under either strategy, the proactive
timeout closes connections with no streams received without sending GOAWAY. Revert by setting
``envoy.reloadable_features.use_connection_event_drain`` to ``false``.
