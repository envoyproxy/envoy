sockets: fixed a crash in the experimental ``io_uring`` socket interface where a TLS upstream that
closed the connection without a close_notify dereferenced an empty write parameter while probing
``SO_ERROR``. The socket now returns the kernel error outside of a write completion, matching the
epoll socket handle. This only affected deployments that opted into ``io_uring``.
