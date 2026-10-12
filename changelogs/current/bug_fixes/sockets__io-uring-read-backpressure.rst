sockets: fixed missing read backpressure in the experimental ``io_uring`` socket interface where a
read-disabled connection kept draining the kernel into an unbounded read buffer. Reads now stop once
the buffered data reaches the read buffer size, so the kernel receive window closes and backpressure
reaches the peer, matching the epoll socket handle. This only affected deployments that opted into
``io_uring``.
