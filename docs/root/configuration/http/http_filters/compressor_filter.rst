.. _config_http_filters_compressor:

Compressor
==========
Compressor is an HTTP filter which enables Envoy to compress dispatched data
from an upstream service upon client request. Compression is useful in
situations when bandwidth is scarce and large payloads can be effectively compressed
at the expense of higher CPU load or offloading it to a compression accelerator.

Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.compressor.v3.Compressor``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.compressor.v3.Compressor>`

How it works
------------
When compressor filter is enabled, request and response headers are inspected to
determine whether or not the content should be compressed. The content is
compressed and then sent to the client with the appropriate headers, if
response and request allow.

Currently the filter supports :ref:`gzip <envoy_v3_api_msg_extensions.compression.gzip.compressor.v3.Gzip>`,
:ref:`brotli <envoy_v3_api_msg_extensions.compression.brotli.compressor.v3.Brotli>`
and :ref:`zstd <envoy_v3_api_msg_extensions.compression.zstd.compressor.v3.Zstd>`
compression only. Other compression libraries can be supported as extensions.

An example configuration of the filter may look like the following:

.. literalinclude:: _include/compressor-filter.yaml
    :language: yaml
    :linenos:
    :lineno-start: 37
    :lines: 37-60
    :caption: :download:`compressor-filter.yaml <_include/compressor-filter.yaml>`

By *default* request compression is disabled, but when enabled it will be *skipped* if:

- A request does **not** contain a ``content-type`` value that matches one of the selected
  mime-types, which default to the following:

  - ``application/javascript``
  - ``application/json``
  - ``application/xhtml+xml``
  - ``image/svg+xml``
  - ``text/css``
  - ``text/html``
  - ``text/plain``
  - ``text/xml``

- A request does **not** contain a ``content-length`` header.
- A request contains a ``content-encoding`` header.
- A request contains a ``transfer-encoding`` header whose value includes a known
  compression name.

By *default* response compression is enabled, but it will be *skipped* when:

- A request does **not** contain ``accept-encoding`` header.
- A request contains an ``accept-encoding`` header, but it does not contain ``gzip`` or ``\*``.
- A request contains an ``accept-encoding`` header with ``gzip`` or ``\*`` with the weight ``q=0``. Note
  that the ``gzip`` will have a higher weight than ``\*``. For example, if ``accept-encoding``
  is ``gzip;q=0,\*;q=1``, the filter will not compress. But if the header is set to
  ``\*;q=0,gzip;q=1``, the filter will compress.
- A request whose ``accept-encoding`` header includes any encoding type with a higher
  weight than ``gzip``'s given the corresponding compression filter is present in the chain.
- A response contains a ``content-encoding`` header.
- A response contains a ``cache-control`` header whose value includes ``no-transform``.
- A response contains a ``transfer-encoding`` header whose value includes a known
  compression name.
- A response does **not** contain a ``content-type`` value that matches one of the selected
  mime-types, which default to:

  - ``application/javascript``
  - ``application/json``
  - ``application/xhtml+xml``
  - ``image/svg+xml``
  - ``text/css``
  - ``text/html``
  - ``text/plain``
  - ``text/xml``

- A response does **not** contain a ``content-length`` or ``transfer-encoding`` headers.
- Response size is smaller than 30 bytes (only applicable when ``transfer-encoding``
  is not chunked).
- A response code is on the list of uncompressible response codes, which is empty by default.

Please note that in case the filter is configured to use a compression library extension
other than gzip it looks for content encoding in the ``accept-encoding`` header provided by
the extension.

When response compression is *applied*:

- The ``content-length`` is removed from response headers.
- Response headers contain ``transfer-encoding: chunked``, and
  ``content-encoding`` with the compression scheme used (e.g., ``gzip``).
- The ``vary: accept-encoding`` header is inserted on every response.

Also the ``vary: accept-encoding`` header may be inserted even if compression is **not**
applied due to incompatible ``accept-encoding`` header in a request. This happens
when the requested resource can still be compressed given compatible ``accept-encoding``.
Otherwise, if an uncompressed response is cached by a caching proxy in front of Envoy,
the proxy won't know to fetch a new incoming request with compatible ``accept-encoding``
from upstream.

When request compression is *applied*:

- ``content-length`` is removed from request headers.
- ``content-encoding`` with the compression scheme used (e.g., ``gzip``) is added to
  request headers.

Compression Status Header
-------------------------

To aid upstream caches and clients in understanding why a response was or was not compressed, the Compressor filter can add a response header ``x-envoy-compression-status``. This header provides visibility into the filter's decision-making process, which is particularly useful for cache invalidation strategies when compression settings or request/response characteristics change.

To enable this feature, the ``status_header_enabled`` configuration option within ``ResponseDirectionConfig`` must be set to ``true``.

The header value follows the format: ``<encoder-type>;<status>[;<additional-params>]``. Where:

- ``<encoder-type>``: The name of the compressor library configured (e.g., ``gzip``, ``br``).
- ``<status>``: The result of the compression check.
- ``<additional-params>``: Optional key-value pairs providing more context.

If multiple Compressor filters are present in the chain, each filter will append its status to the header, separated by commas. For example: ``gzip;ContentTypeNotAllowed,br;Compressed;OriginalLength=1024``

Possible status values:

- ``Compressed``: The response was compressed by this filter.
   - Additional Parameter: ``OriginalLength=<value>``, where ``<value>`` is the original value of the ``Content-Length`` header before compression.
- ``ContentLengthTooSmall``: Compression was skipped because the content length is below the configured minimum threshold.
- ``ContentTypeNotAllowed``: Compression was skipped because the response ``Content-Type`` is not in the allowed list.
- ``EtagNotAllowed``: Compression was skipped because the response contains an ``ETag`` header and the ``disable_on_etag_header`` option is enabled.
- ``StatusCodeNotAllowed``: Compression was skipped because the response status code is in the list of uncompressible status codes.

Behavior Notes:

- When the ``status_header_enabled`` configuration option is enabled, the order of internal checks within the filter is adjusted to ensure the most accurate reason for skipping compression is reported.
- The conditions are evaluated in a specific order. The first condition that causes compression to be skipped is the one reported in the ``x-envoy-compression-status`` header. Subsequent checks are not performed for that filter instance. For example, if a response has both a disallowed content type and a content length below the threshold, only the reason that is checked first will be reported.

Per-Route Configuration
-----------------------

Response compression can be enabled and disabled on individual virtual hosts and routes.
For example, to disable response compression for a particular virtual host, but enable response compression for its ``/static`` route:

.. literalinclude:: _include/compressor-filter.yaml
    :language: yaml
    :linenos:
    :lineno-start: 14
    :lines: 14-36
    :caption: :download:`compressor-filter.yaml <_include/compressor-filter.yaml>`

Additionally, the compressor library can be overridden on a per-route basis. This allows
different routes to use different compression algorithms (e.g., gzip, brotli, zstd) while
maintaining the same filter configuration. For example, to use brotli compression for a
specific route while using gzip as the default:

.. code-block:: yaml

  routes:
  - match:
      prefix: "/api"
    route:
      cluster: service
    typed_per_filter_config:
      envoy.filters.http.compressor:
        "@type": type.googleapis.com/envoy.extensions.filters.http.compressor.v3.CompressorPerRoute
        overrides:
          compressor_library:
            name: brotli
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.compression.brotli.compressor.v3.Brotli

Using different compressors for requests and responses
--------------------------------------------------------

If different compression libraries are desired for requests and responses, it is possible to install
multiple compressor filters enabled only for requests or responses. For instance:

.. literalinclude:: _include/compressor-filter-request-response.yaml
    :language: yaml
    :linenos:
    :lines: 25-64
    :caption: :download:`compressor-filter-request-response.yaml <_include/compressor-filter-request-response.yaml>`

.. _compressor-statistics:

Statistics
----------

Every configured Compressor filter has statistics rooted at
<stat_prefix>.compressor.<compressor_library.name>.<compressor_library_stat_prefix>.<direction_prefix>.*
with the following:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  compressed, Counter, Number of requests compressed.
  not_compressed, Counter, Number of requests not compressed.
  total_uncompressed_bytes, Counter, The total uncompressed bytes of all the requests that were marked for compression.
  total_compressed_bytes, Counter, The total compressed bytes of all the requests that were marked for compression.
  content_length_too_small, Counter, Number of requests that accepted the compressor encoding but did not compress because the payload was too small.

In addition to the statics common for requests and responses there are statistics
specific to responses only:

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

  no_accept_header, Counter, Number of requests with no accept header sent.
  header_identity, Counter, Number of requests sent with "identity" set as the ``accept-encoding``.
  header_compressor_used, Counter, Number of requests sent with filter's configured encoding set as the ``accept-encoding``.
  header_compressor_overshadowed, Counter, Number of requests skipped by this filter instance because they were handled by another filter in the same filter chain.
  header_wildcard, Counter, Number of requests sent with ``\*`` set as the ``accept-encoding``.
  header_not_valid, Counter, Number of requests sent with a not valid ``accept-encoding`` header (aka ``q=0`` or an unsupported encoding type).
  not_compressed_etag, Counter, Number of requests that were not compressed due to the etag header. ``disable_on_etag_header`` must be turned on for this to happen.

.. attention::

   In case the compressor is not configured to compress responses with the field
   ``response_direction_config`` of the :ref:`Compressor <envoy_v3_api_msg_extensions.filters.http.compressor.v3.Compressor>`
   message the stats are rooted in the legacy tree
   ``<stat_prefix>.compressor.<compressor_library.name>.<compressor_library_stat_prefix>.*``, that is without
   the direction prefix.
