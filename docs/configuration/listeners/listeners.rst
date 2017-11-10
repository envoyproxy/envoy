.. _config_listeners:

Listeners
=========

.. toctree::
  :hidden:

  filters
  ssl
  stats
  runtime
  lds

The top level Envoy configuration contains a list of :ref:`listeners <arch_overview_listeners>`.
Each individual listener configuration has the following format:

.. code-block:: json

  {
    "name": "...",
    "address": "...",
    "filters": [],
    "ssl_context": "{...}",
    "bind_to_port": "...",
    "use_proxy_proto": "...",
    "use_original_dst": "...",
    "per_connection_buffer_limit_bytes": "...",
    "drain_type": "..."
  }

.. _config_listeners_name:

name
  *(optional, string)* The unique name by which this listener is known. If no name is provided,
  Envoy will allocate an internal UUID for the listener. If the listener is to be dynamically
  updated or removed via :ref:`LDS <config_listeners_lds>` a unique name must be provided.
  By default, the maximum length of a listener's name is limited to 60 characters. This limit can be
  increased by setting the :option:`--max-obj-name-len` command line argument to the desired value.

address
  *(required, string)* The address that the listener should listen on. Currently only TCP
  listeners are supported, e.g., "tcp://127.0.0.1:80". Note, "tcp://0.0.0.0:80" is the wild card
  match for any IPv4 address with port 80.

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
  can only receive connections redirected from other listeners that set use_original_dst parameter to
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

.. _config_listeners_per_connection_buffer_limit_bytes:

per_connection_buffer_limit_bytes
  *(optional, integer)* Soft limit on size of the listener's new connection read and write buffers.
  If unspecified, an implementation defined default is applied (1MiB).

.. _config_listeners_drain_type:

drain_type
  *(optional, string)* The type of draining that the listener does. Allowed values include *default*
  and *modify_only*. See the :ref:`draining <arch_overview_draining>` architecture overview for
  more information.

Statistics
----------

The listener manager has a statistics tree rooted at *listener_manager.* with the following
statistics. Any ``:`` character in the stats name is replaced with ``_``.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  listener_added, Counter, Total listeners added (either via static config or LDS)
  listener_modified, Counter, Total listeners modified (via LDS)
  listener_removed, Counter, Total listeners removed (via LDS)
  listener_create_success, Counter, Total listener objects successfully added to workers.
  listener_create_failure, Counter, Total failed listener object additions to workers.
  total_listeners_warming, Gauge, Number of currently warming listeners
  total_listeners_active, Gauge, Number of currently active listeners
  total_listeners_draining, Gauge, Number of currently draining listeners
