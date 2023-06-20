.. _config_http_caches_file_system_http_cache:

File System Http Cache
======================

The file system cache caches http responses in a specified file system directory.

A maximum size or maximum number of entries may be specified; upon exceeding that limit, the cache will remove some of the least recently used entries.

.. note::

 This filter is not yet supported on Windows.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig>`
