Fixed a race where removing or draining a reverse-connection listener could trigger a new
outbound handshake. Listener teardown now destroys the retry timer on its worker before closing
the socket.
