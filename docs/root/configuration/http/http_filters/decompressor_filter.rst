.. _config_http_filters_decompressor:

Decompressor
============
Decompressor is an HTTP filter which enables Envoy to bidirectionally decompress data.


Configuration
-------------
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.decompressor.v3.Decompressor>`

How it works
------------
When the decompressor filter is enabled, headers are inspected to
determine whether or not the content should be decompressed. The content is
decompressed and passed on to the rest of the filter chain. Note that decompression happens
independently for request and responses based on the rules described below.

Currently the filter supports :ref:`gzip compression <envoy_v3_api_msg_extensions.compression.gzip.decompressor.v3.Gzip>`
only. Other compression libraries can be supported as extensions.

An example configuration of the filter may look like the following:

.. code-block:: yaml

    http_filters:
    - name: decompressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
        decompressor_library:
          name: basic
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip
            window_bits: 10

By *default* decompression will be *skipped* when:

- A request/response does NOT contain *content-encoding* header.
- A request/response includes *content-encoding* header, but it does not contain the configured
  decompressor's content-encoding.
- A request/response contains a *cache-control* header whose value includes "no-transform".

When decompression is *applied*:

- The *content-length* is removed from headers.

  .. note::

    If an updated *content-length* header is desired, the buffer filter can be installed as part
    of the filter chain to buffer decompressed frames, and ultimately update the header. Due to
    :ref:`filter ordering <arch_overview_http_filters_ordering>` a buffer filter needs to be
    installed after the decompressor for requests and prior to the decompressor for responses.

- The *content-encoding* header is modified to remove the decompression that was applied.

.. _decompressor-statistics:

Statistics
----------

Every configured Deompressor filter has statistics rooted at
<stat_prefix>.decompressor.<decompressor_library.name>.<decompressor_library_stat_prefix>.<request/response>*
with the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decompressed, Counter, Number of request/responses compressed.
  not_decompressed, Counter, Number of request/responses not compressed.
  total_uncompressed_bytes, Counter, The total uncompressed bytes of all the request/responses that were marked for decompression.
  total_compressed_bytes, Counter, The total compressed bytes of all the request/responses that were marked for decompression.
