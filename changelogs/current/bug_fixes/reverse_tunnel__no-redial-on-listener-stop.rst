Fixed a race where removing or draining a reverse-connection listener (for example via LDS)
could still trigger a new outbound reverse-tunnel handshake. ``resetFileEvents()`` on the
worker now destroys the retry timer before the listen socket is closed on the main thread, so
drain-aware re-dial cannot arm a replacement against a dying listener.
