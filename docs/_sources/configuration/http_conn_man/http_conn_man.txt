.. _config_http_conn_man:

HTTP connection manager
=======================

* HTTP connection manager :ref:`architecture overview <arch_overview_http_conn_man>`.
* HTTP protocols :ref:`architecture overview <arch_overview_http_protocols>`.

.. code-block:: json

  {
    "type": "read",
    "name": "http_connection_manager",
    "config": {
      "codec_type": "...",
      "stat_prefix": "...",
      "route_config": "{...}",
      "filters": [],
      "add_user_agent": "...",
      "tracing_enabled": "...",
      "http_codec_options": "...",
      "server_name": "...",
      "idle_timeout_s": "...",
      "access_log": [],
      "use_remote_address": "..."
    }
  }

.. _config_http_conn_man_codec_type:

codec_type
  *(required, string)* Supplies the type of codec that the connection manager should use. Possible
  values are:

  http1
    The connection manager will assume that the client is speaking HTTP/1.1.

  http2
    The connection manager will assume that the client is speaking HTTP/2 (Envoy does not require
    HTTP/2 to take place over TLS or to use ALPN. Prior knowledge is allowed).

  auto
    For every new connection, the connection manager will determine which codec to use. This mode
    supports both ALPN for TLS listeners as well as protocol inference for plaintext listeners.
    If ALPN data is available, it is preferred, otherwise protocol inference is used. In almost
    all cases, this is the right option to choose for this setting.

.. _config_http_conn_man_stat_prefix:

stat_prefix
  *(required, string)* The human readable prefix to use when emitting statistics for the
  connection manager. See the :ref:`statistics <config_http_conn_man_stats>` documentation
  for more information.

:ref:`route_config <config_http_conn_man_route_table>`
  *(required, object)* The :ref:`route table <arch_overview_http_routing>` for the connection
  manager. All connection managers must have a route table, even if it is empty.

:ref:`filters <config_http_conn_man_filters>`
  *(required, array)* A list of individual :ref:`HTTP filters <arch_overview_http_filters>` that
  make up the filter chain for requests made to the connection manager. Order matters as the filters
  are processed sequentially as request events happen.

.. _config_http_conn_man_add_user_agent:

add_user_agent
  *(optional, boolean)* Whether the connection manager manipulates the
  :ref:`config_http_conn_man_headers_user-agent` and
  :ref:`config_http_conn_man_headers_downstream-service-cluster` headers. See the linked
  documentation for more information. Defaults to false.

.. _config_http_conn_man_tracing_enabled:

tracing_enabled
  *(optional, boolean)* Whether the connection manager emits :ref:`tracing <arch_overview_tracing>`
  data to the :ref:`configured tracing provider <config_tracing>`. Defaults to false.

.. _config_http_conn_man_http_codec_options:

http_codec_options
  *(optional, string)* Additional options that are passed directly to the codec. Not all options
  are applicable to all codecs. Possible values are:

  no_compression
    The codec will not use compression. In practice this only applies to HTTP/2 which will disable
    header compression if set.

  These are the same options available in the upstream cluster :ref:`http_codec_options
  <config_cluster_manager_cluster_http_codec_options>` option. See the comment there about
  disabling HTTP/2 header compression.

.. _config_http_conn_man_server_name:

server_name
  *(optional, string)* An optional override that the connection manager will write to the
  :ref:`config_http_conn_man_headers_server` header in responses. If not set, the default is
  *envoy*.

idle_timeout_s
  *(optional, integer)* The idle timeout in seconds for connections managed by the connection
  manager. The idle timeout is defined as the period in which there are no active requests. If not
  set, there is no idle timeout. When the idle timeout is reached the connection will be closed. If
  the connection is an HTTP/2 connection a GOAWAY frame will be sent prior to closing
  the connection.

:ref:`access_log <config_http_conn_man_access_log>`
  *(optional, array)* Configuration for :ref:`HTTP access logs <arch_overview_http_access_logs>`
  emitted by the connection manager.

.. _config_http_conn_man_use_remote_address:

use_remote_address
  *(optional, boolean)* If set to true, the connection manager will use the real remote address
  of the client connection when determining internal versus external origin and manipulating 
  various headers. If set to false or absent, the connection manager will use the
  :ref:`config_http_conn_man_headers_x-forwarded-for` HTTP header. See the documentation for
  :ref:`config_http_conn_man_headers_x-forwarded-for`,
  :ref:`config_http_conn_man_headers_x-envoy-internal`, and
  :ref:`config_http_conn_man_headers_x-envoy-external-address` for more information.

.. toctree::
  :hidden:

  route_config/route_config
  filters
  access_log
  headers
  stats
  runtime
