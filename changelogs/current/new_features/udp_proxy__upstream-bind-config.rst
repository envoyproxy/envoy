udp_proxy: native UDP upstream sockets now honor the cluster or bootstrap
``upstream_bind_config``, including the selected source address and pre-bind socket options.
Configure source port ``0`` to let the kernel allocate an ephemeral port for each UDP proxy
session.
