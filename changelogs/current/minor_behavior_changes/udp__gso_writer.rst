Updated the GSO UDP packet writer, used by QUIC and UDP listeners, to send datagrams larger than
1452 bytes through ordinary UDP sends instead of failing with ``EMSGSIZE``. Multi-slice datagrams
also use ordinary sends, after flushing any older batch. Blocked writers now return ``EAGAIN``
without accepting new datagrams. The ``internal_buffer_size`` gauge now sums buffered bytes across
writers sharing a statistics scope instead of reporting the last writer's value.
