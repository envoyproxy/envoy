Added a dynamic modules socket interface extension
(``envoy.extensions.network.socket_interface.dynamic_modules``) that lets a dynamic module create
the underlying socket for the addresses that select the interface and, when the module implements
the I/O hooks, delegate the socket's data path to it: Envoy calls
``envoy_dynamic_module_on_socket_interface_io_read``, ``..._io_write`` and ``..._io_shutdown`` on
the module's own file descriptor, and the module wakes the worker's event loop with
``envoy_dynamic_module_callback_socket_interface_io_ready``. The Rust SDK exposes this through the
``socket_interface:`` arm of ``declare_all_init_functions!``, the ``SocketInterfaceConfig``
trait and the per-socket ``SocketIo`` trait.
