.. _config_http_filters_compressor:

Compressor
==========
Compressor is an HTTP filter which enables Envoy to compress dispatched data
from an upstream service upon client request. Compression is useful in
situations when bandwidth is scarce and large payloads can be effectively compressed
at the expense of higher CPU load or offloading it to a compression accelerator.

.. note::

 This filter deprecates the :ref:`HTTP Gzip filter <config_http_filters_gzip>`.

Configuration
-------------
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.compressor.v3.Compressor>`
* This filter should be configured with the name *envoy.filters.http.compressor*.

How it works
------------
When compressor filter is enabled, request and response headers are inspected to
determine whether or not the content should be compressed. The content is
compressed and then sent to the client with the appropriate headers, if
response and request allow.

Currently the filter supports :ref:`gzip compression <envoy_v3_api_msg_extensions.compression.gzip.compressor.v3.Gzip>`
only. Other compression libraries can be supported as extensions.

An example configuration of the filter may look like the following:

.. code-block:: yaml

    http_filters:
    - name: envoy.filters.http.compressor
      typed_config:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor
        disable_on_etag_header: true
        content_length: 100
        content_type:
          - text/html
          - application/json
        compressor_library:
          name: text_optimized
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.compression.gzip.compressor.v3.Gzip
            memory_level: 3
            window_bits: 10
            compression_level: best_compression
            compression_strategy: default_strategy

By *default* compression will be *skipped* when:

- A request does NOT contain *accept-encoding* header.
- A request includes *accept-encoding* header, but it does not contain "gzip" or "\*".
- A request includes *accept-encoding* with "gzip" or "\*" with the weight "q=0". Note
  that the "gzip" will have a higher weight then "\*". For example, if *accept-encoding*
  is "gzip;q=0,\*;q=1", the filter will not compress. But if the header is set to
  "\*;q=0,gzip;q=1", the filter will compress.
- A request whose *accept-encoding* header includes any encoding type with a higher
  weight than "gzip"'s given the corresponding compression filter is present in the chain.
- A response contains a *content-encoding* header.
- A response contains a *cache-control* header whose value includes "no-transform".
- A response contains a *transfer-encoding* header whose value includes "gzip".
- A response does not contain a *content-type* value that matches one of the selected
  mime-types, which default to *application/javascript*, *application/json*,
  *application/xhtml+xml*, *image/svg+xml*, *text/css*, *text/html*, *text/plain*,
  *text/xml*.
- Neither *content-length* nor *transfer-encoding* headers are present in
  the response.
- Response size is smaller than 30 bytes (only applicable when *transfer-encoding*
  is not chunked).

Please note that in case the filter is configured to use a compression library extension
other than gzip it looks for content encoding in the *accept-encoding* header provided by
the extension.

When compression is *applied*:

- The *content-length* is removed from response headers.
- Response headers contain "*transfer-encoding: chunked*", and
  "*content-encoding*" with the compression scheme used (e.g., ``gzip``).
- The "*vary: accept-encoding*" header is inserted on every response.

Also the "*vary: accept-encoding*" header may be inserted even if compression is *not*
applied due to incompatible "*accept-encoding*" header in a request. This happens
when the requested resource still can be compressed given compatible "*accept-encoding*".
Otherwise, if an uncompressed response is cached by a caching proxy in front of Envoy,
the proxy won't know to fetch a new incoming request with compatible "*accept-encoding*"
from upstream.

.. _compressor-statistics:

Statistics
----------

Every configured Compressor filter has statistics rooted at
<stat_prefix>.compressor.<compressor_library.name>.<compressor_library_stat_prefix>.*
with the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  compressed, Counter, Number of requests compressed.
  not_compressed, Counter, Number of requests not compressed.
  no_accept_header, Counter, Number of requests with no accept header sent.
  header_identity, Counter, Number of requests sent with "identity" set as the *accept-encoding*.
  header_compressor_used, Counter, Number of requests sent with "gzip" set as the *accept-encoding*.
  header_compressor_overshadowed, Counter, Number of requests skipped by this filter instance because they were handled by another filter in the same filter chain.
  header_wildcard, Counter, Number of requests sent with "\*" set as the *accept-encoding*.
  header_not_valid, Counter, Number of requests sent with a not valid *accept-encoding* header (aka "q=0" or an unsupported encoding type).
  total_uncompressed_bytes, Counter, The total uncompressed bytes of all the requests that were marked for compression.
  total_compressed_bytes, Counter, The total compressed bytes of all the requests that were marked for compression.
  content_length_too_small, Counter, Number of requests that accepted gzip encoding but did not compress because the payload was too small.
  not_compressed_etag, Counter, Number of requests that were not compressed due to the etag header. *disable_on_etag_header* must be turned on for this to happen.
