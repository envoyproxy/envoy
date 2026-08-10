Fixed a bug where Envoy silently dropped zero-length UDP datagrams before issuing a socket
operation. Empty datagrams are now sent through both connected and unconnected UDP sockets while
preserving their packet boundaries.
