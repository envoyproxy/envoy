Fixed a use-after-free during worker shutdown. When a connection handler was destroyed with
connections still open, closing those connections could re-enter the handler, e.g. an upstream
connection pool reconnecting to an internal listener, and look up the already destroyed internal
listener map. This could crash Envoy or make the worker thread spin forever and hang shutdown.
