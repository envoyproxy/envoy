Fixed HTTP/3 upstream sockets ignoring the configured source address network namespace. Namespace
metadata is retained for port migration, and source address bind failures now close the socket.
