.. _config_sock_interface_vcl:

VCL Socket Interface
====================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.vcl.v3alpha.VclSocketInterface>`

.. attention::

  The VCL socket interface extension is experimental and is currently under active development.

This socket interface extension provides Envoy with high speed L2-L7 user space networking by integrating with `Link fd.io VPP <https://fd.io>`_ through VPP's ``Comms`` Library (VCL).

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
