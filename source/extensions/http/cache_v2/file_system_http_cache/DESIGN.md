# File system cache design

## Goals

(Unchecked boxes are not yet achieved; checked boxes are implemented features)

- [x] Cache should be usable by two processes at once (e.g. during hot restart)
- [x] Cache should evict the least recently used (LRU) entry when full
- [ ] Eviction should be configurable as a "window", like watermarks, or with an optional frequency constraint, so the eviction thread can be kept from churning.
- [x] Cache should be limited to a specified amount of storage
- [ ] Cache should be configurable to periodically update the internal size from the filesystem, to account for external alterations.
- [ ] There should be an ability to remove objects from the cache with some kind of API call.
- [ ] Cache should expose counters for eviction stats (files evicted, bytes evicted).
- [ ] Cache should expose counters for timing information (eviction thread idle, eviction thread busy)
- [x] Cache should expose gauges for total size stored.
- [ ] Cache should optionally expose histogram for cache entry sizes.
- [x] Cache should index by the request route *and* a key generated from headers that may affect the outcome of a request (See [allowed_vary_headers](https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/filters/http/cache_v2/v3/cache.proto.html))
- [ ] Cache should create a [tree structure](#tree-structure) of folders (may be configured as just one branch), so user may avoid filesystem performance issues with overcrowded directories.
- [ ] Cache should validate the existence of the file path it is configured to use, at startup. (Maybe optionally try to create it if not present?)

## Storage design

* A `CacheSession` maintains an open file handle of which ownership is passed to the `CacheSession`. It is possible for such an entry to be evicted (on a validation fail most likely), which should be fine - the file will be unlinked and the open file handle will keep the data "alive" until the requests using the old file handle are completed.
* Simultaneous writes don't break anything, and may occur when multiple processes are touching the same cache.
* The cache can be configured with a maximum number of cache entry files, thereby effectively enforcing a maximum number of files per path.
* A new cache entry that causes the cache to exceed the configured maximum size or maximum number of entries triggers the eviction thread to evict sufficient LRU entries to bring it back below the threshold\[s\] exceeded.
* Each cache entry file starts with [a fixed structure header followed by a serialized proto](cache_file_header.proto), followed by raw body, proto-serialized trailers and proto-serialized headers. Headers are at the end to facilitate updating headers on validate operations.
* Cache entry files are named `cache-` followed by a stable hash key for the entry.
<a name="tree-structure"></a>
* (When implemented) the tree structure of folders is simply one level deep of folders named `cache-0000`, `cache-0001` etc. as four-digit hexadecimal numbers up to the configured number of subdirectories. Cache files are placed in a folder according to a short stable hash of their key. On cache startup, any cache entries found to be in the wrong folder (as would be the case if the number of folders was reconfigured) will simply be removed.
