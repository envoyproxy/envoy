.. _config_http_filters_gzip:

Gzip
====
Gzip is an HTTP filter which enables Envoy to compress dispatched data
from an upstream service upon client request. Compression is useful in
situations where large payloads need to be transmitted without
compromising the response time.

Configuration
-------------
* :ref:`v2 API reference <envoy_api_msg_config.filter.http.gzip.v2.Gzip>`

.. attention::

  The *window bits* is a number that tells the compressor how far ahead in the
  text the algorithm should be looking for repeated sequence of characters.
  Due to a known bug in the underlying zlib library, *window bits* with value
  eight does not work as expected. Therefore any number below that will be
  automatically set to 9. This issue might be solved in future releases of
  the library.

Runtime
-------

The Gzip filter supports the following runtime settings:

gzip.filter_enabled
    The % of requests for which the filter is enabled. Default is 100.


How it works
------------
When gzip filter is enabled, request and response headers are inspected to
determine whether or not the content should be compressed. The content is
compressed and then sent to the client with the appropriate headers if either
response and request allow.

By *default* compression will be *skipped* when:

- A request does NOT contain *accept-encoding* header.
- A request includes *accept-encoding* header, but it does not contain "gzip".
- A response contains a *content-encoding* header.
- A Response contains a *cache-control* header whose value includes "no-transform".
- A response contains a *transfer-encoding* header whose value includes "gzip".
- A response does not contain a *content-type* value that matches one of the selected
  mime-types, which default to *application/javascript*, *application/json*,
  *application/xhtml+xml*, *image/svg+xml*, *text/css*, *text/html*, *text/plain*,
  *text/xml*.
- Neither *content-length* nor *transfer-encoding* headers are present in
  the response.
- Response size is smaller than 30 bytes (only applicable when *transfer-encoding*
  is not chuncked).

When compression is *applied*:

- The *content-length* is removed from response headers.
- Response headers contain "*transfer-encoding: chunked*" and
  "*content-encoding: gzip*".
- The "*vary: accept-encoding*" header is inserted on every response.
