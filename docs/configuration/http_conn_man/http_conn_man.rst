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
      "rds": "{...}",
      "route_config": "{...}",
      "filters": [],
      "add_user_agent": "...",
      "tracing": "{...}",
      "http_codec_options": "...",
      "http2_settings": "{...}",
      "server_name": "...",
      "idle_timeout_s": "...",
      "drain_timeout_ms": "...",
      "access_log": [],
      "use_remote_address": "...",
      "generate_request_id": "..."
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

:ref:`rds <config_http_conn_man_rds>`
  *(sometimes required, object)* The connection manager configuration must specify one of *rds* or
  *route_config*. If *rds* is specified, the connection manager's route table will be dynamically
  loaded via the RDS API. See the :ref:`documentation <config_http_conn_man_rds>` for more
  information.

:ref:`route_config <config_http_conn_man_route_table>`
  *(sometimes required, object)* The connection manager configuration must specify one of *rds* or
  *route_config*. If *route_config* is specified, the :ref:`route table <arch_overview_http_routing>`
  for the connection manager is static and is specified in this property.

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

:ref:`tracing <config_http_conn_man_tracing>`
  *(optional, object)* Presence of the object defines whether the connection manager
  emits :ref:`tracing <arch_overview_tracing>` data to the :ref:`configured tracing provider <config_tracing>`.

.. _config_http_conn_man_http_codec_options:

http_codec_options
  **DEPRECATED**, use :ref:`http2_settings <config_http_conn_man_http2_settings>` instead.

  *(optional, string)* Additional options that are passed directly to the codec. Not all options
  are applicable to all codecs. Possible values are:

  no_compression
    The codec will not use compression. In practice this only applies to HTTP/2 which will disable
    header compression if set.

  These are the same options available in the upstream cluster :ref:`http_codec_options
  <config_cluster_manager_cluster_http_codec_options>` option. See the comment there about
  disabling HTTP/2 header compression.

.. _config_http_conn_man_http2_settings:

http2_settings
  *(optional, object)* Additional HTTP/2 codec options that are passed directly to the HTTP/2 codec. 
  Currently supported settings are:

  hpack_table_size
    *(optional, integer)* `Maximum Table Size`_ (in octets) that the encoder is permitted to use for
    the dynamic HPACK table. Valid values range from 0 to 4294967295 (2^32 - 1) and defaults to 4096,
    with 0 effectively disables header compression.

  max_concurrent_streams
    *(optional, integer)* `Maximum concurrent streams`_ allowed on one HTTP/2 connection. Valid values
    range from 0 to 2147483647 (2^31 - 1) and defaults to 1024.

    NOTE: total streams is 32-bit unsigned, one-side (client/server) is half that, and we also need to
    exclude stream 0.

  initial_window_size
    *(optional, integer)* `Initial stream-level flow-control window`_ size. Valid values range from 65535
    (2^16 - 1, HTTP/2 default) to 2147483647 (2^31 - 1, HTTP/2 maximum) and defaults to 65535 (2^16 - 1).

    NOTE: 65535 is the initial window size from HTTP/2 spec. We only support increase the default window
    size now, so it's also the minimum.

  These are the same options available in the upstream cluster :ref:`http2_settings
  <config_cluster_manager_cluster_http2_settings>` option.

  .. _Maximum Table Size: http://httpwg.org/specs/rfc7541.html#rfc.section.4.2
  .. _Maximum concurrent streams: http://httpwg.org/specs/rfc7540.html#rfc.section.5.1.2
  .. _Initial stream-level flow-control window: http://httpwg.org/specs/rfc7540.html#rfc.section.6.9.2

.. _config_http_conn_man_server_name:

server_name
  *(optional, string)* An optional override that the connection manager will write to the
  :ref:`config_http_conn_man_headers_server` header in responses. If not set, the default is
  *envoy*.

idle_timeout_s
  *(optional, integer)* The idle timeout in seconds for connections managed by the connection
  manager. The idle timeout is defined as the period in which there are no active requests. If not
  set, there is no idle timeout. When the idle timeout is reached the connection will be closed. If
  the connection is an HTTP/2 connection a drain sequence will occur prior to closing the
  connection. See :ref:`drain_timeout_s <config_http_conn_man_drain_timeout_ms>`.

.. _config_http_conn_man_drain_timeout_ms:

drain_timeout_ms
  *(optional, integer)* The time in milliseconds that Envoy will wait between sending an HTTP/2
  "shutdown notification" (GOAWAY frame with max stream ID) and a final GOAWAY frame. This is used
  so that Envoy provides a grace period for new streams that race with the final GOAWAY frame.
  During this grace period, Envoy will continue to accept new streams. After the grace period, a
  final GOAWAY frame is sent and Envoy will start refusing new streams. Draining occurs both
  when a connection hits the idle timeout or during general server draining. The default grace
  period is 5000 milliseconds (5 seconds) if this option is not specified.

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

generate_request_id
  *(optional, boolean)* Whether the connection manager will generate the
  :ref:`config_http_conn_man_headers_x-request-id` header if it does not exist. This defaults to
  *true*. Generating a random UUID4 is expensive so in high throughput scenarios where this
  feature is not desired it can be disabled.

.. toctree::
  :hidden:

  route_config/route_config
  filters
  access_log
  tracing
  headers
  stats
  runtime
  rds
