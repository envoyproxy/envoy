Fixed a hot restart child reading packets from an inherited UDP socket while its listener was still paused. The
QUIC listener injects a read event on its listener to process the handshakes it buffered from packets the parent
forwarded, and the listener then read the socket the parent was still serving, so the packets of the parent's
connections were dequeued by the child and answered with stateless resets until the clients had reconnected. A
paused listener now runs its read-ready callback on such an event without reading the socket.
