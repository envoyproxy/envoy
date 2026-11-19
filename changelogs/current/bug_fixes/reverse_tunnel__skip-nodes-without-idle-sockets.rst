Fixed the reverse connection cluster picking, for a cluster ID, a node whose sockets are all in use, such
as a draining initiator still serving long requests. Such a node cannot take a new connection, so every
request routed to it failed with ``fallback_no_reverse_socket`` until its last request ended. Round-robin
now skips to the next node with an idle socket, and falls back to the old behavior when no node has one.
