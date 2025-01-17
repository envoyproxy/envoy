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
- A request contains an ``accept-encoding`` header with ``gzip`` or ``\*```` with the weight ``q=0``. Note
  that the ``gzip`` will have a higher weight than ``\*``. For example, if ``accept-encoding``
  is ``gzip;q=0,\*;q=1``, the filter will not compress. But if the header is set to
  ``\*;q=0,gzip;q=1``, the filter will compress.
- A request whose ``accept-encoding`` header includes any encoding type with a higher
  weight than ``gzip``'s given the corresponding compression filter is present in the chain.
- A response contains a ``content-encoding`` header.
- A response contains a ``cache-control```` header whose value includes ``no-transform``.
- A response contains a ``transfer-encoding```` header whose value includes a known
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

.. attention:

   In case the compressor is not configured to compress responses with the field
   ``response_direction_config`` of the :ref:`Compressor <envoy_v3_api_msg_extensions.filters.http.compressor.v3.Compressor>`
   message the stats are rooted in the legacy tree
   ``<stat_prefix>.compressor.<compressor_library.name>.<compressor_library_stat_prefix>.*``, that is without
   the direction prefix.
