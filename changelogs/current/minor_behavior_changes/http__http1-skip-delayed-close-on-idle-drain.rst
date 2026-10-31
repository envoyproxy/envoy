An HTTP/1 downstream connection that carries no request between the start of a drain sequence and
its :ref:`drain timeout <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.drain_timeout>`,
such as one closed by the connection idle timeout, is now closed without waiting for the
:ref:`delayed close timeout <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.delayed_close_timeout>`.
There is no response on such a connection for the client to read, and while the delayed close was
pending Envoy ignored any request a pooling client sent on it, so that request was never answered.
This change can be reverted by setting the runtime guard
``envoy.reloadable_features.http1_skip_delayed_close_on_idle_drain`` to ``false``.
