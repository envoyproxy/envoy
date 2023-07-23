.. _config_sock_interface_vcl:

VCL Socket Interface
====================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.vcl.v3alpha.VclSocketInterface>`

.. attention::

  The VCL socket interface extension is experimental and is currently under active development.

.. note::

 These features are not supported on Windows, for details please refer to `VPP Supported Architectures <https://s3-docs.fd.io/vpp/22.10/aboutvpp/supported.html>`_.


This socket interface extension provides Envoy with high speed L2-L7 user space networking by integrating with `fd.io VPP <https://fd.io>`_ through VPP's ``Comms`` Library (VCL).

The VCL socket interface is only included in :ref:`contrib images <install_contrib>`

Example configuration
---------------------

.. code-block:: yaml

  bootstrap_extensions:
    - name: envoy.extensions.vcl.vcl_socket_interface
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.vcl.v3alpha.VclSocketInterface
  default_socket_interface: "envoy.extensions.vcl.vcl_socket_interface"

How it works
------------

If enabled, the extension attaches through a VCL interface (``vcl_interface.h``) to VCL, and consequently to the external VPP process, when it is initialized during Envoy bootstrap. This registers a main VCL worker, while subsequent Envoy workers are registered whenever the socket interface extension detects that its code is being executed by a pthread that has not yet been registered with VCL.

Because both libevent and VCL want to handle the async polling and the dispatching of ``IoHandles``, the VCL interface delegates control to libevent by registering with it, for each Envoy worker, the eventfd associated to the VCL worker's VPP message queue.
These shared memory message queues are used by VPP to convey io/ctrl events to VCL and the eventfds are used to signal message queue transitions from empty to non-empty state.
This ultimately means that VPP generated events force libevent to hand over control to the VCL interface which, for each Envoy worker, uses an internally maintained epoll fd to poll/pull events from VCL and subsequently dispatch them.
To support all of these indirect interactions, the socket interface makes use of custom ``IoHandle`` and ``FileEvent`` implementations that convert between Envoy and VCL API calls.

Installing and running VPP/VCL
------------------------------

For information on how to build and/or install VPP see the getting started guide `here <https://fd.io/docs/vpp/master/>`_. Assuming the use of DPDK interfaces, a minimal ``startup.conf`` file that also configures the host stack would consist of:

.. code-block:: text

  unix {
    # Run in interactive mode and not as a daemon
    nodaemon
    interactive

    # Cli socket to be used by vppctl
    cli-listen /run/vpp/cli.sock

    # Group id is an example
    gid vpp
  }

  cpu {
    # Avoid using core 0 and run vpp's main thread on core 1
    skip-cores 0
    main-core 1

    # Set logical CPU core(s) where worker threads are running. For performance testing make
    # sure the cores are on the same numa as the NIC(s). Use lscpu to determine the numa of
    # a cpu and "sh hardware" in vpp cli to determine the numa of a NIC. To configure multiple
    # workers lists are also possible, e.g., corelist-workers 2-4,6
    corelist-workers 2
  }

  buffers {
    # Default is 16384 (8192 if running unpriviledged)
    buffers-per-numa 16384
  }

  dpdk {
    # Notes:
    # - Assuming only one NIC is used
    # - The PCI address is an example, the actual one should be found using something like dpdk_devbind
    #    https://github.com/DPDK/dpdk/blob/main/usertools/dpdk-devbind.py
    # - Number of rx queues (num-rx-queus) should be number of workers
    dev 0000:18:00.0 {
      num-tx-desc 256
      num-rx-desc 256
      num-rx-queues 1
    }
  }

  session {
    # Use session layer socket api for VCL attachments
    use-app-socket-api

    # VPP worker's message queues lengths
    event-queue-length 100000
  }

Manually start VPP, once a binary is obtained: ``./vpp -c startup.conf``

VCL can be configured by either adding a configuration file to ``/etc/vpp/vcl.conf`` or by pointing the ``VCL_CONFIG`` environment variable to a configuration file. A minimal example that can be used for RPS load testing can be found lower:

.. code-block:: text

  vcl {
    # Max rx/tx session buffers sizes in bytes. Increase for high throughput traffic.
    rx-fifo-size 400000
    tx-fifo-size 400000

    # Size of shared memory segments between VPP and VCL in bytes
    segment-size 1000000000

    # App has access to global routing table
    app-scope-global

    # Allow inter-app shared-memory cut-through sessions
    app-scope-local

    # Pointer to session layer's socket api socket
    app-socket-api /var/run/vpp/app_ns_sockets/default

    # Message queues use eventfds for notifications
    use-mq-eventfd

    # VCL worker incoming message queue size
    event-queue-size 40000
  }
