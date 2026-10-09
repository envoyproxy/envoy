Fixed a use-after-free where removing a reverse-tunnel remote host (for example after a
cluster host-map update) immediately destroyed in-flight handshake ``RCConnectionWrapper``
objects. Wrappers are now shut down (clearing the handshake read-filter back-pointer and HTTP/1
codec) and deferred-deleted on the worker dispatcher, matching the normal connection-done path,
so a late handshake read cannot call into ``Http1::ConnectionImpl::dispatch()`` on a freed
object.
