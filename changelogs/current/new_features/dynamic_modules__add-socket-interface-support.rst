Added a dynamic modules socket interface extension
(``envoy.extensions.network.socket_interface.dynamic_modules``), selected as the bootstrap's
``default_socket_interface``, that asks a dynamic module for every socket of every listener and
cluster except the admin listener's. For each socket the module either leaves it to the standard
socket interface or creates it and hands Envoy its file descriptor. For a socket it creates, the
module can also perform the socket's reads, writes and shutdown itself, waking the worker with
``envoy_dynamic_module_callback_socket_interface_io_ready`` for data the kernel does not signal,
or, for a listen socket, hand Envoy the listener's connections with
``envoy_dynamic_module_on_socket_interface_accept`` so they run through its filter chains. The
Rust SDK exposes this through the ``socket_interface:`` arm of ``declare_all_init_functions!``,
the ``SocketInterfaceConfig`` trait and the per-socket ``SocketIo`` and ``SocketAcceptor`` traits.
