Fixed a heap buffer overflow in the hot restart IPC receive path: a datagram whose
length prefix is within 8 bytes of ``UINT64_MAX`` wrapped the receive buffer size
computation to a near-zero value, causing the next ``recvmsg()`` to overflow the
buffer. Oversized datagrams are now dropped and the stream state reset. Also fixed
the hot restart domain socket mode: ``fchmod()`` was previously called on the socket
descriptor before ``bind()`` created the filesystem node, so the intended mode was
never applied; the mode is now set on the socket path after binding.
