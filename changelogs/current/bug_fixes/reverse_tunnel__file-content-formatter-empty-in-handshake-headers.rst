Fixed a bug in the reverse tunnel downstream socket interface
(``envoy.bootstrap.reverse_tunnel.downstream_socket_interface``) where handshake
``additional_headers`` values that use a ``ThreadLocal``-backed substitution formatter (such as
``%FILE_CONTENT%``, or secret/SDS-backed formatters) resolved to an empty string on the worker
thread that assembles the handshake request. The handshake formatters were built in the bootstrap
extension constructor, which runs before the worker threads register with the ``ThreadLocal``
system, so the formatter providers' thread-local slots were never populated on the workers. The
formatters are now built in ``onServerInitialized()``, after the workers are registered, so their
values propagate to every worker thread.
