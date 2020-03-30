### HTTP Cache Filter
Work in Progress--not ready for deployment

HTTP caching can improve system throughput, latency, and network/backend load
levels when the same content is requested multiple times. Caching is
particularly valuable for edge proxies and browser-based traffic, which
typically include many cacheable static resources, but it can be useful any time
there is enough repeatedly served cacheable content.

## Configuration
CacheFilter is configured with
envoy.config.filter.http.cache.v2alpha.CacheConfig. The only required
configuration field is typed_config. The type of this message will select what
HttpCache plugin to use, and will be passed to the selected plugin. HttpCache
plugins are located in subdirectories of
source/extensions/filters/http/cache. Specifying a message of type
envoy.source.extensions.filters.http.cache.SimpleHttpCacheConfig will select a
proof-of-concept implementation included in the Envoy source. More
implementations can be provided by implementing
Envoy::Extensions::HttpFilters::Cache::HttpCache. To write a cache storage
implementation, see [Writing Cache Filter
Implementations](cache_filter_plugins.md).

TODO(toddmgreer) Describe other fields as they get implemented.
The remaining configuration fields control caching behavior and limits. By
default, this filter will cache almost all responses that are considered
cacheable by [RFC7234](https://httpwg.org/specs/rfc7234.html), with handling
of conditional ([RFC7232](https://httpwg.org/specs/rfc7232.html)), and *range*
[RFC7233](https://httpwg.org/specs/rfc7233.html) requests. Those RFC define
which request methods and response codes are cacheable, subject to the
cache-related headers they also define: *cache-control*, *range*, *if-match*,
*if-none-match*, *if-modified-since*, *if-unmodified-since*, *if-range*, *authorization*,
*date*, *age*, *expires*, and *vary*. Responses with a *vary* header will only be cached
if the named headers are listed in *allowed_vary_headers*.

## Status
 * ready for developers to write cache storage plugins; please contribute them
  to the Envoy repository if possible.
 * ready for contributions to help finish its implementation of HTTP caching
  semantics.
 * *not* ready for actual use. Please see TODOs in the code.
