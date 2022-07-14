.. _config_network_filters_thrift_proxy:

Thrift proxy
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.network.thrift_proxy.v3.ThriftProxy``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.ThriftProxy>`

Statistics
----------

Every configured Thrift proxy filter emits the following statistics:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  cx_destroy_local_with_active_rq, Counter, Connections destroyed locally with an active request
  cx_destroy_remote_with_active_rq, Counter, Connections destroyed remotely with an active request
  downstream_cx_max_requests, Counter, Connections that have been closed due to reaching the max requests limit
  downstream_response_drain_close, Counter, Connections that have received the drain close header in a response
  request, Counter, Total number of requests
  request_call, Counter, Total number of requests of type call
  request_decoding_error, Counter, Total number of requests that caused a decoding error
  request_invalid_type, Counter, Total number of requests with an invalid type
  request_oneway, Counter, Total number of requests of type oneway
  request_passthrough, Counter, Total number of requests with payload passthrough enabled
  response, Counter, Total number of responses
  response_decoding_error, Counter, Total number of responses with a decoding error
  response_error, Counter, Total number of responses with an error
  response_exception, Counter, Total number of responses with an exception
  response_invalid_type, Counter, Total number of responses with an invalid type
  response_passthrough, Counter, Total number of responses with payload passthrough enabled
  response_reply, Counter, Total number of responses of type reply
  response_success, Counter ,Total number of responses of type success
  request_active, Gauge, Number of currently active requests
  request_time_ms, Histogram, Request time in millisecondse

Cluster Protocol Options
------------------------

Thrift connections to upstream hosts can be configured by adding an entry to the appropriate
Cluster's :ref:`extension_protocol_options<envoy_v3_api_field_config.cluster.v3.Cluster.typed_extension_protocol_options>`
keyed by ``envoy.filters.network.thrift_proxy``. The
:ref:`ThriftProtocolOptions<envoy_v3_api_msg_extensions.filters.network.thrift_proxy.v3.ThriftProtocolOptions>`
message describes the available options.

Downstream Requests Limit
-------------------------
Thrift Proxy can set the
:ref:`maximum number of requests<envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.ThriftProxy.max_requests_per_connection>`
that each downstream connection can handle. When the number of requests exceeds the connection limit, Thrift Proxy will
actively disconnect from the Thrift client.


Thrift Request Metadata
-----------------------

The :ref:`HEADER transport<envoy_v3_api_enum_value_extensions.filters.network.thrift_proxy.v3.TransportType.HEADER>`
support metadata. In particular, the
`Header transport <https://github.com/apache/thrift/blob/master/doc/specs/HeaderFormat.md>`_
supports informational key/value pairs.

Header Transport Metadata
~~~~~~~~~~~~~~~~~~~~~~~~~

Header transport key/value pairs are available for routing as
:ref:`headers <envoy_v3_api_field_extensions.filters.network.thrift_proxy.v3.RouteMatch.headers>`.

Metadata Interoperability
~~~~~~~~~~~~~~~~~~~~~~~~~

Request metadata that is available for routing (see above) is automatically converted between wire
formats when translation between downstream and upstream connections occurs.
