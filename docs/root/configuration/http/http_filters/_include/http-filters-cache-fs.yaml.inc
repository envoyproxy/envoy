http_filters:
- name: envoy.filters.http.cache
  typed_config:
    "@type": type.googleapis.com/envoy.extensions.filters.http.cache.v3.CacheConfig
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.http.cache.file_system_http_cache.v3.FileSystemHttpCacheConfig
      manager_config:
        thread_pool:
          thread_count: 2
      cache_path: /var/cache/envoy
      max_cache_size_bytes: 1073741824
