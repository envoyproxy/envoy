.. _config_vcl_sock_interface:

VCL Socket Interface
====================

* :ref:`v3 API reference <envoy_v3_api_msg_extensions.network.socket_interface.vcl.v3alpha.VclSocketInterface>`

.. attention::

  The VCL socket interface extension is experimental and is currently under active development.

This socket interface extension provides Envoy with high speed L2-L7 user space networking by integrating with `Link fd.io VPP <https://fd.io>`_ through VPP's Comms Library (VCL).

The VCL socket interface is only included in :ref:`contrib images <install_contrib>

Example configuration
---------------------

.. code-block:: yaml

  bootstrap_extensions:
    - name: envoy.extensions.network.socket_interface.vcl.vcl_socket_interface
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.network.socket_interface.vcl.v3alpha.VclSocketInterface
  default_socket_interface: "envoy.extensions.network.socket_interface.vcl.vcl_socket_interface"