Fixed a use-after-free in HTTP/3 upstream connections after their cluster was removed. The per-cluster QUIC state
(including the clock and alarm factory the connections use) was destroyed with the cluster, while the cluster's
connection pools were only drained, so a connection with active streams read freed memory on its next write or timer
and crashed the process. The state now lives as long as any connection pool using it.
