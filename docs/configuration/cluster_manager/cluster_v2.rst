.. _config_cluster_manager_cluster_v2:

Cluster (V2 API)
================

In the V2 API, Cluster supports all the :ref:`V1 fields <config_cluster_manager_cluster>`, plus the
following new and modified fields.

Modified fields:

.. csv-table::
  :header: V1 Name, V2 Name, Notes
  :widths: 1, 1, 2

  type, type, EDS replaces SDS as a cluster type.
  connection_timeout_ms, connection_timeout, In JSON the field is encoded as a string containing numeric seconds followed by `s`. For example: `"1.5s"`.
  lb_type, lb_policy, V2 values are UPPER_CASE.
  hosts, hosts, Now an array of :ref:`addresses <config_cluster_manager_cluster_v2_address>`.
  service_name, eds_cluster_config, *service name* is now a field in *eds_cluster_config*.
  health_check, health_checks, See protobuf IDL for format.
  ssl_context, tls_context, See protobuf IDL for format.
  cleanup_internal_ms, cleanup_interval, See *connection_timeout_ms*.
  dns_refresh_rate_ms, dns_refresh_rate, See *connection_timeout_ms*.
  dns_resolvers, dns_resolvers, Now an array of :ref:`addresses <config_cluster_manager_cluster_v2_address>`.
  features, , Specify *http2_protocol_options* to use HTTP2 connections.
  http2_settings, http2_protocol_options, Enables HTTP2.

New fields:

.. code-block:: json

  {
    "eds_cluster_config": "{...}",
    "tcp_protocol_options": "{...}",
    "http_protocol_options": "{...}",
    "http2_protocol_options": "{...}",
    "grpc_protocol_options": "{...}",
    "upstream_bind_config": "{...}",
    "lb_subset_config": "{...}"
  }

.. _config_cluster_manager_cluster_v2_eds_cluster_config:

eds_cluster_config
  *(required, object)* Supersedes the *service_name* field. See the protobuf IDL for details.

.. _config_cluster_manager_cluster_v2_tcp_protocol_options:

tcp_protocol_options
  *(optional, object)* TCP protocol options. See the protobuf IDL for details.

.. _config_cluster_manager_cluster_v2_http_protocol_options:

http_protocol_options
  *(optional, object)* HTTP protocol options. See the protobuf IDL for details.

.. _config_cluster_manager_cluster_v2_http2_protocol_options:

http2_protocol_options
  *(optional, object)* HTTP2 protocol options. See the protobuf IDL for details.

.. _config_cluster_manager_cluster_v2_grpc_protocol_options:

grpc_protocol_options
  *(optional, object)* GRPC protocol options. See the protobuf IDL for details.

.. _config_cluster_manager_cluster_v2_upstream_bind_config:

:ref:`upstream_bind_config <config_cluster_manager_cluster_v2_bind_config>`
  *(optional, object)* Optional configuration used to bind newly established upstream connections.
  This overrides any bind_config specified in the bootstrap proto.

:ref:`lb_subset_config <config_cluster_manager_cluster_v2_lb_subset_config>`
  *(optional, object)* Optional configuration for :ref:`load balancer subsets
  <arch_overview_load_balancer_subsets>`.

.. _config_cluster_manager_cluster_v2_bind_config:

Bind Config
-----------

.. code-block:: json

  {
    "source_address": "{...}"
  }

.. _config_cluster_manager_cluster_v2_bind_config_source_address:

:ref:`source_address <config_cluster_manager_cluster_v2_address>`
  *(required, object)* The address to bind to when creating a socket. If the address and port are
  empty, no bind will be performed.

.. _config_cluster_manager_cluster_v2_address:

Address
-------

.. code-block::json

  {
    "socket_address": {
      "protocol": "...",
      "address": "...",
      "port_value": "...",
      "named_port": "...",
      "resolver_name": "..."
    }
    "pipe": {
      "path": "..."
    }
  }

.. _config_cluster_manager_cluster_v2_address_socket_address:

socket_address
  *(sometimes required, object)* One of *socket_address* or *pipe* is required.

.. _config_cluster_manager_cluster_v2_address_socket_address_protocol:

protocol
  *(required, string)* *TCP* or *UDP*.

.. _config_cluster_manager_cluster_v2_address_socket_address_address:

address
  *(required, string)* The address for this socket.  Listeners will bind to the address. An empty
  address implies a bind to 0.0.0.0 or ::. It's still possible to distinguish on address via the
  prefix/suffix matching in FilterChainMatch after connection. For clusters, an address may be
  either an IP or hostname to be resolved via DNS. If it is a hostname, *resolver_name* should be
  set.

.. _config_cluster_manager_cluster_v2_address_socket_address_port_value:

port_value
  *(sometimes required, uint32)* The port to use for this socket. One of *port_value* or
  *named_port* is required.

.. _config_cluster_manager_cluster_v2_address_socket_address_named_port:

named_port
  *(sometimes required, string)* This is only valid if DNS SRV or if *resolver_name* is specified
  below and the named resolver is capable of named port resolution. One of *port_value* or
  *named_port* is required.

.. _config_cluster_manager_cluster_v2_address_pipe:

pipe
  *(sometimes required, object)* One of *socket_address* or *pipe* is required.

.. _config_cluster_manager_cluster_v2_address_pipe_path:

path
  *(required, string)* The pipe's path.

.. toctree::
  :hidden:

  cluster_v2_lb_subset_config
