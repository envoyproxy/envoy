sockets: fixed several connection lifecycle defects in the experimental ``io_uring`` socket
interface. Resetting file events now detaches the handler callback so a later completion cannot
reach a released owner. The close write timeout now restarts while a slow peer keeps reading so
the response tail is not dropped. Injected completions are now keyed by the owning socket rather
than the file descriptor so a reused descriptor is not wedged. The socket handle no longer closes
a file descriptor that its ``io_uring`` socket already closes. This only affected deployments that
opted into ``io_uring``.
