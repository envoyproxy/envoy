.. _config_sock_interface_io_uring:

io_uring Socket Interface
=========================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.socket_interface.v3.IoUringSocketInterface>`

.. attention::

  The io_uring socket interface extension is experimental and is currently under active development.

io_uring is an asynchronous I/O API implemented in the Linux kernel.
This socket interface uses [liburing](https://github.com/axboe/liburing) to integrate io_uring
with Envoy.

Example configuration
---------------------

.. code-block:: yaml

  bootstrap_extensions:
    - name: envoy.extensions.io_socket.io_uring
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.socket_interface.v3.IoUringSocketInterface
  default_socket_interface: "envoy.extensions.network.socket_interface.io_uring"

