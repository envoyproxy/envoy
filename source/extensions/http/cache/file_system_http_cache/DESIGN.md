# File system cache design

## Goals

(Unchecked boxes are not yet achieved; checked boxes are implemented features)

- [x] Cache should be usable by two processes at once (e.g. during hot restart)
- [x] Cache should evict the least recently used (LRU) entry when full
- [ ] Eviction should be configurable as a "window", like watermarks, or with an optional frequency constraint, so the eviction thread can be kept from churning.
- [x] Cache should be limited to a specified amount of storage
- [ ] Cache should be configurable to periodically update the internal size from the filesystem, to account for external alterations.
- [ ] Cache should mitigate thundering herd problem (i.e. if two or more workers request the same cacheable uncached result at the same time, only one worker should hit upstream). See [discussion](#thundering-herd).
- [ ] There should be an ability to remove objects from the cache with some kind of API call.
- [ ] Cache should expose counters for eviction stats (files evicted, bytes evicted).
- [ ] Cache should expose counters for timing information (eviction thread idle, eviction thread busy)
- [x] Cache should expose gauges for total size stored.
- [ ] Cache should optionally expose histograms for insert and lookup latencies.
- [ ] Cache should optionally expose histogram for cache entry sizes.
- [x] Cache should index by the request route *and* a key generated from headers that may affect the outcome of a request (See [allowed_vary_headers](https://www.envoyproxy.io/docs/envoy/latest/api-v3/extensions/filters/http/cache/v3/cache.proto.html))
- [ ] Cache should create a [tree structure](#tree-structure) of folders (may be configured as just one branch), so user may avoid filesystem performance issues with overcrowded directories.
- [ ] Cache should validate the existence of the file path it is configured to use, at startup. (Maybe optionally try to create it if not present?)

## Storage design

* The only state stored in memory is that a cache entry is in the process of being written; this allows other requests for the same resource in the same process to avoid creating duplicate write operations. (This is an optimization only - simultaneous writes don't break anything, and may occur when multiple processes are involved.)
* The cache can be configured with a maximum number of cache entry files, thereby effectively enforcing a maximum number of files per path.
* A new cache entry that causes the cache to exceed the configured maximum size or maximum number of entries triggers the eviction thread to evict sufficient LRU entries to bring it back below the threshold\[s\] exceeded.
* Each cache entry file starts with [a fixed structure header followed by a serialized proto](cache_file_header.proto), followed by proto-serialized headers, raw body and proto-serialized trailers.
* Cache entry files are named `cache-` followed by a stable hash key for the entry.
<a name="tree-structure"></a>
* (When implemented) the tree structure of folders is simply one level deep of folders named `cache-0000`, `cache-0001` etc. as four-digit hexadecimal numbers up to the configured number of subdirectories. Cache files are placed in a folder according to a short stable hash of their key. On cache startup, any cache entries found to be in the wrong folder (as would be the case if the number of folders was reconfigured) will simply be removed.

## Discussions

<a name="thundering-herd"></a>
### Thundering herd

The current implementation, if there are multiple requests for the same resource before the cache is populated, has only one of them perform an insert operation to the cache, and the rest simply bypass the cache. This can cause the "thundering herd" problem - if requests come in bursts the cache will not protect the upstream from that load.

One possible solution would be to have all requesters for the same cache entry stream as the cache entry is written. However, if we do that, and the inserting stream gets closed prematurely, all the dependent streams would be forced to drop their also-incomplete responses.

Another possible solution is to have secondary requesters wait until the cache entry is populated or abandoned before deciding whether to become cache readers or inserters (or bypass, if it turns out to be uncacheable). The downside of this option is that for large content, the dependent clients won't start streaming at all until the first client *finishes* streaming.

An ideal solution would be to either make an inserting stream non-cancellable (i.e. if the client cancels, the upstream connection continues to stream to populate the cache). This could be achieved either by using the existing stream and adding a "non-cancellable" feature in the core (a bit of a large scale change), or by making insertion not use the existing stream at all, instead creating its own client. The problem with that option is that ideally the client would only pass through the filters that are upstream of the cache filter, and there is currently no mechanism for creating a new "partial filter chain" client like this.

_Proposal from jmarantz:_

The lock object could go into one of several states:
* In-progress, size unknown
* headers-complete, content length known and less than a threshold
* headers-complete, content length known and more than a threshold
* headers-complete, chunked encoding

Each state could be individually configured as "block" or "pass through", allowing the user to decide which option is more appropriate for a particular use-case.

This proposal would be redundant if we can figure a reliable way to stream a cache entry.
