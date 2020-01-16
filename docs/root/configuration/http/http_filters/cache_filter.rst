.. _config_http_filters_cache:

HTTP Cache Filter
=================
**Work in Progress--not ready for deployment**

HTTP caching can improve system throughput and latency and reduce network and
backend loads when the same content is requested multiple times. Caching is
particularly valuable for edge proxies and browser-based traffic, which
typically include many cacheable static resources, but it can be useful any time
there is enough repeatedly served content.

Configuration
-------------
* :ref:`Configuration API <envoy_api_msg_config.filter.http.cache.v2.cacheConfig>`
* This filter should be configured with the name *envoy.filters.http.cache*.

The only required configuration field is :ref:`name
<envoy_api_field_config.filter.http.cache.v2.CacheConfig.name>`, which must
specify a valid cache storage implementation linked into your Envoy
binary. Specifying 'SimpleHttpCache' will select a proof-of-concept
implementation included in the Envoy source. More implementations can (and will)
be provided by implementing Envoy::Extensions::HttpFilters::Cache::HttpCache.

The remaining configuration options control caching behavior and limits. By
default, this filter will cache almost all responses that are considered
cacheable by `RFC7234 <https://httpwg.org/specs/rfc7234.html>`_, with handling
of conditional (`RFC7232 <https://httpwg.org/specs/rfc7232.html>`_), and range
(`RFC7233 <https://httpwg.org/specs/rfc7233.html>`_) requests. Those RFC define
which request methods and response codes are cacheable, subject to the
cache-related headers they also define: cache-control, range, if-match,
if-none-match, if-modified-since, if-unmodified-since, if-range, authorization,
date, age, expires, and vary. Responses with a 'vary' header will only be cached
if the named headers are listed in :ref:`allowed_vary_headers
<envoy_api_field_config.filter.http.cache.v2.CacheConfig.allowed_vary_headers>`

Status
------
* This filter *is* ready for developers to write cache storage plugins; please
  contribute them to the Envoy repository if possible.
* This filter *is* ready for contributions to help finish its implementation of
  HTTP caching semantics.
* This filter *is not* ready for actual use. Please see TODOs in the code.
