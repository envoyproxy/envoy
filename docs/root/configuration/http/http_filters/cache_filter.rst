.. _config_http_filters_cache:

Cache filter
============

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.cache.v3.CacheConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.cache.v3.CacheConfig>`
* :ref:`v3 SimpleHTTPCache API reference <envoy_v3_api_msg_extensions.http.cache.simple_http_cache.v3.SimpleHttpCacheConfig>`
* This filter doesn't support virtual host-specific configurations.
* When the cache is enabled, cacheable requests are only sent through filters in the
  :ref:`upstream_http_filters <envoy_v3_api_field_extensions.filters.http.router.v3.Router.upstream_http_filters>`
  chain and *not* through any filters in the regular filter chain that are further
  upstream than the cache filter, while non-cacheable requests still go through the
  listener filter chain. It is therefore recommended for consistency that only the
  router filter should be further upstream in the listener filter chain than the
  cache filter.

.. image:: /_static/cache-filter-chain.svg
   :width: 80%
   :align: center

The HTTP Cache filter implements most of the complexity of HTTP caching semantics.

For HTTP Requests:

* HTTP Cache respects request's ``Cache-Control`` directive. For example, if request comes with ``Cache-Control: no-store`` the request won't be cached, unless
  :ref:`ignore_request_cache_control_header <envoy_v3_api_field_extensions.filters.http.cache.v3.CacheConfig.ignore_request_cache_control_header>` is true.
* HTTP Cache wont store HTTP HEAD Requests.

For HTTP Responses:

* HTTP Cache only caches responses with enough data to calculate freshness lifetime as per `RFC7234 <https://httpwg.org/specs/rfc7234.html#calculating.freshness.lifetime>`_.
* HTTP Cache respects ``Cache-Control`` directive from the upstream host. For example, if HTTP response returns status code 200 with ``Cache-Control: max-age=60`` and no ``vary`` header, it will be cached.
* HTTP Cache only caches responses with status codes: 200, 203, 204, 206, 300, 301, 308, 404, 405, 410, 414, 451, 501.

HTTP Cache delegates the actual storage of HTTP responses to implementations of the ``HttpCache`` interface. These implementations can
cover all points on the spectrum of persistence, performance, and distribution, from local RAM caches to globally distributed
persistent caches. They can be fully custom caches, or wrappers/adapters around local or remote open-source or proprietary caches.
Built-in cache storage backends include :ref:`SimpleHttpCacheConfig <envoy_v3_api_msg_extensions.http.cache.simple_http_cache.v3.SimpleHttpCacheConfig>` (in-memory) and :ref:`FileSystemHttpCacheConfig <envoy_v3_api_msg_extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig>` (persistent; LRU).

Architecture and extension points
---------------------------------

Envoy’s HTTP caching is split into:

* **HTTP Cache filter** (extension name ``envoy.filters.http.cache``, category ``envoy.filters.http``) — configured via ``CacheConfig`` to apply HTTP caching semantics.
* **Cache storage backends** (extension category ``envoy.http.cache``) — the filter delegates object storage/retrieval to a backend, selected via a nested ``typed_config`` in ``CacheConfig``.

Example configuration
---------------------

Example filter configuration with a ``SimpleHttpCache`` cache implementation:

.. literalinclude:: _include/http-cache-configuration.yaml
   :language: yaml
   :lines: 29-34
   :linenos:
   :lineno-start: 29
   :caption: :download:`http-cache-configuration.yaml <_include/http-cache-configuration.yaml>`

Example filter configuration with a ``FileSystemHttpCache`` cache implementation:

.. literalinclude:: _include/http-cache-configuration-fs.yaml
   :language: yaml
   :start-at: http_filters:
   :end-before: envoy.filters.http.router
   :linenos:
   :lineno-match:
   :caption: :download:`http-cache-configuration-fs.yaml <_include/http-cache-configuration-fs.yaml>`

.. seealso::

   :ref:`Envoy Cache Sandbox <install_sandboxes_cache_filter>`
      Learn more about the Envoy Cache filter in the step by step sandbox.

   :ref:`HTTP Cache filter (proto file) <envoy_v3_api_file_envoy/extensions/filters/http/cache/v3/cache.proto>`
      ``CacheConfig`` API reference.

   :ref:`In-memory storage backend <envoy_v3_api_file_envoy/extensions/http/cache/simple_http_cache/v3/config.proto>`
      ``SimpleHttpCacheConfig`` API reference.

   :ref:`Persistent on-disk storage backend <config_http_caches_file_system_http_cache>`
      Docs page for File System Http Cache; links to ``FileSystemHttpCacheConfig`` API reference.

   :ref:`Cache filter V2 <config_http_filters_cache_v2>`
      Version 2 of the cache filter.
