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

If the kernel does not support io_uring, Envoy will fall back to the traditional socket API.
