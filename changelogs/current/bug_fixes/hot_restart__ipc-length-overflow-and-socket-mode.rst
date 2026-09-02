Fixed a heap buffer overflow in the hot restart IPC receive path: a datagram whose
length prefix is within 8 bytes of ``UINT64_MAX`` wrapped the receive buffer size
computation to a near-zero value, causing the next ``recvmsg()`` to overflow the
buffer. Oversized datagrams are now dropped and the stream state reset. Also made
the hot restart domain socket mode handling portable by retaining the race-free
pre-bind ``fchmod()`` and applying the mode to the socket path after binding as a
fallback for platforms that do not propagate the socket descriptor mode.
