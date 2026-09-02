Fixed a use-after-free where reverse-tunnel listener stop left in-flight handshake
``RCConnectionWrapper`` objects alive until main-thread destruction. Worker
``resetFileEvents()`` now shuts down those wrappers and deferred-deletes them.
