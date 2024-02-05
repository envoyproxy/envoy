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

Currently the filter supports :ref:`gzip <envoy_v3_api_msg_extensions.compression.gzip.decompressor.v3.Gzip>`,
:ref:`brotli <envoy_v3_api_msg_extensions.compression.brotli.decompressor.v3.Brotli>`
and :ref:`zstd <envoy_v3_api_msg_extensions.compression.zstd.decompressor.v3.Zstd>`
compression only. Other compression libraries can be supported as extensions.

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

- A request/response does **not** contain ``content-encoding`` header.
- A request/response contains a ``content-encoding`` header, but it does not contain the configured
  decompressor's ``content-encoding``.
- A request/response contains a ``cache-control`` header whose value includes ``no-transform``,
  unless :ref:`ignore_no_transform_header <envoy_v3_api_field_extensions.filters.http.decompressor.v3.Decompressor.CommonDirectionConfig.ignore_no_transform_header>`
  is set to ``true``.

Decompression is *applied* when:

- The ``content-length`` is removed from headers.

  .. note::

    If an updated ``content-length`` header is desired, the :ref: `buffer filter <_config_http_filters_buffer>` can be installed as part
    of the filter chain to buffer decompressed frames, and ultimately update the header. Due to
    :ref:`filter ordering <arch_overview_http_filters_ordering>` a buffer filter needs to be
    installed after the decompressor for requests and prior to the decompressor for responses.

- The ``content-encoding`` header is modified to remove the decompression that was applied.

- ``x-envoy-decompressor-<decompressor_name>-<compressed/uncompressed>-bytes`` trailers are added to
  the request/response to relay information about decompression.

Using different decompressors for requests and responses
--------------------------------------------------------

If different compression libraries are desired for requests and responses, it is possible to install
multiple decompressor filters enabled only for requests or responses. For instance:

.. code-block:: yaml

  http_filters:
  # This filter is only enabled for requests.
  - name: envoy.filters.http.decompressor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
      decompressor_library:
        name: small
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
          window_bits: 9
          chunk_size: 8192
      response_direction_config:
        common_config:
          enabled:
            default_value: false
            runtime_key: response_decompressor_enabled
  # This filter is only enabled for responses.
  - name: envoy.filters.http.decompressor
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.http.decompressor.v3.Decompressor
      decompressor_library:
        name: large
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.compression.gzip.decompressor.v3.Gzip"
          window_bits: 12
          chunk_size: 16384
      request_direction_config:
        common_config:
          enabled:
            default_value: false
            runtime_key: request_decompressor_enabled

.. _decompressor-statistics:

Statistics
----------

Every configured Deompressor filter has statistics rooted at
``<stat_prefix>.decompressor.<decompressor_library.name>.<decompressor_library_stat_prefix>.<request/response>*``
with the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  decompressed, Counter, Number of request/responses compressed.
  not_decompressed, Counter, Number of request/responses not compressed.
  total_uncompressed_bytes, Counter, The total uncompressed bytes of all the request/responses that were marked for decompression.
  total_compressed_bytes, Counter, The total compressed bytes of all the request/responses that were marked for decompression.

Additional stats for the decompressor library are rooted at
``<stat_prefix>.decompressor.<decompressor_library.name>.<decompressor_library_stat_prefix>.decompressor_library``.
