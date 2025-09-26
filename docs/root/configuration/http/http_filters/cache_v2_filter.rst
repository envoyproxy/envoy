.. _config_http_filters_cache_v2:

CacheV2 filter
==============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.cache_v2.v3.CacheV2Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.cache_v2.v3.CacheV2Config>`
* :ref:`v3 SimpleHTTPCache API reference <envoy_v3_api_msg_extensions.http.cache_v2.simple_http_cache.v3.SimpleHttpCacheV2Config>`
* This filter doesn't support virtual host-specific configurations.
* When the cache is enabled, cacheable requests are only sent through filters in the
  :ref:`upstream_http_filters <envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_http_filters>`
  chain and *not* through any filters in the regular filter chain that are further
  upstream than the cache filter, while non-cacheable requests still go through the
  listener filter chain. It is therefore recommended for consistency that only the
  router filter should be further upstream in the listener filter chain than the
  cache filter, and even then only if the router filter does not perform any mutations
  such as if ``request_headers_to_add`` is set.

.. image:: /_static/cache-v2-filter-chain.svg
   :width: 80%
   :align: center

* For more complex filter chains where some filters must be upstream of the cache
  filter for correct behavior, or if the router filter is configured to perform
  mutations via
  :ref:`RouteConfiguration <envoy_v3_api_field_config.route.v3.RouteConfiguration.request_headers_to_add>`
  the recommended way to configure this so that it works correctly is to configure
  an internal listener which duplicates the part of the filter chain that is
  upstream of the cache filter, and the ``RouteConfiguration``.

.. image:: /_static/cache-v2-filter-internal-listener.svg
   :width: 80%
   :align: center

The HTTP Cache filter implements most of the complexity of HTTP caching semantics.

For HTTP Requests:

* HTTP Cache respects request's ``Cache-Control`` directive. For example, if request comes with ``Cache-Control: no-store`` the request won't be cached, unless
  :ref:`ignore_request_cache_control_header <envoy_v3_api_field_extensions.filters.http.cache_v2.v3.CacheV2Config.ignore_request_cache_control_header>` is true.
* HTTP Cache wont store HTTP HEAD Requests.

For HTTP Responses:

* HTTP Cache only caches responses with enough data to calculate freshness lifetime as per `RFC7234 <https://httpwg.org/specs/rfc7234.html#calculating.freshness.lifetime>`_.
* HTTP Cache respects ``Cache-Control`` directive from the upstream host. For example, if HTTP response returns status code 200 with ``Cache-Control: max-age=60`` and no ``vary`` header, it will be cached.
* HTTP Cache only caches responses with status codes: 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 451, 501.

HTTP Cache delegates the actual storage of HTTP responses to implementations of the ``HttpCache`` interface. These implementations can
cover all points on the spectrum of persistence, performance, and distribution, from local RAM caches to globally distributed
persistent caches. They can be fully custom caches, or wrappers/adapters around local or remote open-source or proprietary caches.
Currently the only available cache storage implementation is :ref:`SimpleHTTPCache <envoy_v3_api_msg_extensions.http.cache_v2.simple_http_cache.v3.SimpleHttpCacheV2Config>`.

Example configuration
---------------------

Example filter configuration with a ``SimpleHttpCache`` cache implementation:

.. literalinclude:: _include/http-cache-v2-configuration.yaml
   :language: yaml
   :lines: 29-34
   :linenos:
   :lineno-start: 29
   :caption: :download:`http-cache-v2-configuration.yaml <_include/http-cache-v2-configuration.yaml>`

The more complicated filter chain configuration required if mutations occur upstream of the cache filter
involves duplicating the full route config into an internal listener (unfortunately this is currently unavoidable):

.. literalinclude:: _include/http-cache-v2-configuration-internal-listener.yaml
   :language: yaml
   :lines: 38-113
   :linenos:
   :lineno-start: 38
   :caption: :download:`http-cache-v2-configuration-internal-listener.yaml <_include/http-cache-v2-configuration-internal-listener.yaml>`

.. TODO(ravenblackx): Add sandbox and link it below, similar to what cache_filter does.

.. TODO(ravenblackx): Update the docs like the recent update to the old cache docs.

.. seealso::

   :ref:`Old cache filter <config_http_filters_cache>`
      The deprecated cache filter.
