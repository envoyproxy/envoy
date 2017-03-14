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
    "bind_to_port": "...",
    "use_proxy_proto": "...",
    "use_original_dst": "...",
    "per_connection_buffer_limit_bytes": "..."
  }

port
  *(required, integer)* The TCP port that the listener should listen on. Currently only TCP
  listeners are supported which bind to all local interfaces.

:ref:`filters <config_listener_filters>`
  *(required, array)* A list of individual :ref:`network filters <arch_overview_network_filters>`
  that make up the filter chain for connections established with the listener. Order matters as the
  filters are processed sequentially as connection events happen.

  **Note:** If the filter list is empty, the connection will close by default.

:ref:`ssl_context <config_listener_ssl_context>`
  *(optional, object)* The :ref:`TLS <arch_overview_ssl>` context configuration for a TLS listener.
  If no TLS context block is defined, the listener is a plain text listener.

bind_to_port
  *(optional, boolean)* Whether the listener should bind to the port. A listener that doesn't bind
  can only receive connections redirected from other listeners that set use_origin_dst parameter to
  true. Default is true.

use_proxy_proto
  *(optional, boolean)* Whether the listener should expect a
  `PROXY protocol V1 <http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt>`_ header on new
  connections. If this option is enabled, the listener will assume that that remote address of the
  connection is the one specified in the header. Some load balancers including the AWS ELB support
  this option. If the option is absent or set to false, Envoy will use the physical peer address
  of the connection as the remote address.

use_original_dst
  *(optional, boolean)* If a connection is redirected using *iptables*, the port on which the proxy
  receives it might be different from the original destination port. When this flag is set to true,
  the listener hands off redirected connections to the listener associated with the original
  destination port. If there is no listener associated with the original destination port, the
  connection is handled by the listener that receives it. Default is false.

per_connection_buffer_limit_bytes
  *(optional, integer)* Soft limit on size of the listener's new connection read and write buffers.
  If unspecified, an implementation defined default is applied (1MB).

.. toctree::
  :hidden:

  filters
  ssl
  stats
  runtime
