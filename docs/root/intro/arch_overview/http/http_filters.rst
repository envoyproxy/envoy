.. _arch_overview_http_filters:

HTTP filters
============

Much like the :ref:`network level filter <arch_overview_network_filters>` stack, Envoy supports an
HTTP level filter stack within the connection manager. Filters can be written that operate on HTTP
level messages without knowledge of the underlying physical protocol (HTTP/1.1, HTTP/2, etc.) or
multiplexing capabilities. There are three types of HTTP level filters:

* **Decoder**: Decoder filters are invoked when the connection manager is decoding parts of the
  request stream (headers, body, and trailers).
* **Encoder**: Encoder filters are invoked when the connection manager is about to encode parts of
  the response stream (headers, body, and trailers).
* **Decoder/Encoder**: Decoder/Encoder filters are invoked both when the connection manager is
  decoding parts of the request stream and when the connection manager is about to encode parts of
  the response stream.

The API for HTTP level filters allows the filters to operate without knowledge of the underlying
protocol. Like network level filters, HTTP filters can stop and continue iteration to subsequent
filters. This allows for more complex scenarios such as health check handling, calling a rate
limiting service, buffering, routing, generating statistics for application traffic such as
DynamoDB, etc. HTTP level filters can also share state (static and dynamic) among
themselves within the context of a single request stream. Refer to :ref:`data sharing
between filters <arch_overview_data_sharing_between_filters>` for more details. Envoy already
includes several HTTP level filters that are documented in this architecture overview as well as
the :ref:`configuration reference <config_http_filters>`.

.. _arch_overview_http_filters_ordering:

Filter ordering
---------------

Filter ordering in the :ref:`http_filters field <envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.HttpConnectionManager.http_filters>`
matters. If filters are configured in the following order (and assuming all three filters are
decoder/encoder filters):

.. code-block:: yaml

  http_filters:
    - A
    - B
    # The last configured filter has to be a terminal filter, as determined by the
    # NamedHttpFilterConfigFactory::isTerminalFilter() function. This is most likely the router
    # filter.
    - C

The connection manager will invoke decoder filters in the order: ``A``, ``B``, ``C``.
On the other hand, the connection manager will invoke encoder filters in the **reverse**
order: ``C``, ``B``, ``A``.
