Added a :ref:`sockmap socket interface
<envoy_v3_api_msg_extensions.network.socket_interface.sockmap.v3.Sockmap>` that accelerates
same-host TCP hops with eBPF. A ``sock_ops`` program tracks established local sockets in a
``BPF_MAP_TYPE_SOCKHASH`` and an ``sk_msg`` program redirects their payloads with
``bpf_msg_redirect_hash``, bypassing the kernel TCP/IP stack. It is available on Linux only and
requires a kernel 4.18 or later. Connections whose peer is not on the same host transparently fall
back to TCP/IP.
