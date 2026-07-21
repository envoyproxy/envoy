Fixed upstream TLS session resumption for clusters that connect to multiple hostnames through a
single TLS context. Cached sessions are now only offered when their SNI matches the requested
hostname, preventing cross-host certificate validation failures.
