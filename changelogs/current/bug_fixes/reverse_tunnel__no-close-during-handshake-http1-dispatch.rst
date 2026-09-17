Fixed a segfault in the reverse-tunnel initiator HTTP/1 handshake when the
upstream rejected the handshake with a bodied response (for example ``403`` or
``429``). ``decodeHeaders()`` previously closed the connection while
``Http1::ConnectionImpl::dispatch()`` was still parsing the response body. The
wrapper is now deferred-deleted and ``shutdown()`` closes the connection after
dispatch returns.
