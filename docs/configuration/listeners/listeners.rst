.. _config_listeners:

Listeners
=========

The top level Envoy configuration contains a list of :ref:`listeners <arch_overview_listeners>`.
Each individual listener configuration has the following format:

.. code-block:: json

  {
    "port": "...",
    "filters": [],
    "ssl_context": "{...}",
    "use_proxy_proto": "..."
  }

port
  *(required, integer)* The TCP port that the listener should listen on. Currently only TCP
  listeners are supported which bind to all local interfaces.

:ref:`filters <config_listener_filters>`
  *(required, array)* A list of individual :ref:`network filters <arch_overview_network_filters>`
  that make up the filter chain for connections established with the listener. Order matters as the
  filters are processed sequentially as connection events happen.

:ref:`ssl_context <config_listener_ssl_context>`
  *(optional, object)* The :ref:`TLS <arch_overview_ssl>` context configuration for a TLS listener.
  If no TLS context block is defined, the listener is a plain text listener.

use_proxy_proto
  *(optional, boolean)* Whether the listener should expect a
  `PROXY protocol V1 <http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt>`_ header on new
  connections. If this option is enabled, the listener will assume that that remote address of the
  connection is the one specified in the header. Some load balancers including the AWS ELB support
  this option. If the option is absent or set to false, Envoy will use the physical peer address
  of the connection as the remote address.

.. toctree::
  :hidden:

  filters
  ssl
  stats
  runtime
