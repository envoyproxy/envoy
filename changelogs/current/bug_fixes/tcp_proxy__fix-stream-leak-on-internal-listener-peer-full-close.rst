Fixed a stream leak in :ref:`TCP proxy <envoy_v3_api_msg_extensions.filters.network.tcp_proxy.v3.TcpProxy>`
when the filter is configured with
:ref:`tunneling_config <envoy_v3_api_field_extensions.filters.network.tcp_proxy.v3.TcpProxy.tunneling_config>`
and routed through an
:ref:`internal listener <envoy_v3_api_field_config.listener.v3.Listener.internal_listener>`.
Previously, a full peer close on the downstream user-space ``IoHandle`` (peer called ``close()``,
not just ``shutdown(WR)``) was indistinguishable from a graceful read-side half-close because
``tcp_proxy`` enables half-close semantics on its downstream connection and the connection layer
does not subscribe to the ``Closed`` file event in that mode. As a result, the upstream tunneling
stream (e.g. HTTP CONNECT) was not reset and could remain pinned until the upstream idle timeout
fired (default one hour). With this fix, the transport read result flags an end-of-stream that
corresponds to a full peer close (via ``IoHandle::wasPeerFullyClosed()`` on the user-space handle),
and the connection layer translates it into a clean remote close when half-close is enabled,
releasing the upstream tunneled stream immediately.
This behavior is guarded by runtime feature
``envoy.reloadable_features.internal_listener_peer_destroyed_propagation`` and is enabled by default.
Set the runtime guard to ``false`` to restore the legacy behavior.
A peer ``shutdown(WR)`` (graceful half-close) is unchanged: the read side is half-closed and the
upstream tunnel remains open for the response. Real OS sockets are unaffected; the read result
never flags a full peer close for them and the new branch never fires.
