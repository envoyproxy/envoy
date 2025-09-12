.. _config_io_uring:

io_uring
========

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.socket_interface.v3.IoUringOptions>`

.. attention::

  io_uring is experimental and is currently under active development.

.. note::

  This feature is not supported on Windows.

io_uring is an API for asynchronous I/O available in modern Linux kernels, designed to reduce system
calls in network I/O and enhance performance. Envoy can be configured to use io_uring for all TCP
listeners and connections.

To enable io_uring in Envoy, the Linux kernel must be at least version 5.11.

Performance Modes
------------------

io_uring supports three different operation modes that determine which underlying syscalls are used:

* **READ_WRITEV** (default): Uses readv/writev operations for backward compatibility. Provides vectored I/O with good performance for most use cases.
* **SEND_RECV**: Uses send/recv operations optimized for TCP sockets. Could provide performance improvement over readv/writev for simple streaming data.
* **SENDMSG_RECVMSG**: Uses sendmsg/recvmsg operations for advanced use cases. Supports scatter-gather I/O with control messages and could provide performance improvement for complex I/O patterns.

Example Configuration
---------------------

.. literalinclude:: _include/io_uring.yaml
    :language: yaml
    :linenos:
    :lines: 44-49
    :caption: :download:`io_uring.yaml <_include/io_uring.yaml>`

In this configuration, io_uring is enabled in the bootstrap extension, and the default socket
interface is explicitly defined. As a result, Envoy initializes a socket interface with io_uring
support, replacing the default socket interface that uses the traditional socket API.

Performance Mode Configuration
------------------------------

To use high-performance modes, specify the ``mode`` field in ``io_uring_options``:

.. code-block:: yaml

  bootstrap_extensions:
  - name: envoy.extensions.network.socket_interface.default_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.socket_interface.v3.DefaultSocketInterface
      io_uring_options:
        mode: SEND_RECV
        io_uring_size: 1000
        read_buffer_size: 8192

For maximum performance with complex I/O patterns:

.. code-block:: yaml

  bootstrap_extensions:
  - name: envoy.extensions.network.socket_interface.default_socket_interface
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.network.socket_interface.v3.DefaultSocketInterface
      io_uring_options:
        mode: SENDMSG_RECVMSG
        io_uring_size: 2000
        read_buffer_size: 16384

If the kernel does not support io_uring, Envoy will fall back to the traditional socket API.
