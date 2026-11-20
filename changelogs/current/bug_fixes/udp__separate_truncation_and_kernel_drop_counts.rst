UDP listeners now keep locally truncated datagrams separate from the kernel's receive-queue drop
counter, preventing ``downstream_rx_datagram_dropped`` from reporting a false jump of roughly
``2^32`` when both kinds of drops occur on the same socket.
