Fixed a race where removing or draining a reverse-connection listener (for example via LDS)
could still trigger a new outbound reverse-tunnel handshake. ``resetFileEvents()`` on the
worker now stops the retry timer and marks the initiator handle shutting down before the
listen socket is closed on the main thread, so drain-aware re-dial and connection maintenance
do not start replacements against a dying listener.
