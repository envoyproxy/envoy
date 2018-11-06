# Envoy Stats System

Envoy statistics track numeric metrics on an Envoy instance, optionally spanning
binary restarts. The metrics are tracked as:

 * Counters: strictly increasing 64-bit integers.
 * Gauges: 64-bit integers that can rise and fall.
 * Histograms: mapping ranges of values to frequency. The ranges are auto-adjusted as
   data accumulates. Unliked counters and gauges, histogram data is not retained across
   binary restarts.

## Hot-restart: `RawStatData` vs `HeapStatData`

In order to support restarting the Envoy binary without losing counter and gauge
values, they are stored in a shared-memory block, including stats that are
created dynamically at runtime in response to discovery of new clusters at
runtime. To simplify memmory management, each stat is allocated a fixed amount
of storage, controlled via [command-line
flags](https://www.envoyproxy.io/docs/envoy/latest/operations/cli):
`--max-stats` and `--max-obj-name-len`, which determine the size of the pre-allocated
shared-memory block. See
[RawStatData](https://github.com/envoyproxy/envoy/blob/master/source/common/stats/raw_stat_data.h).

Note in particular that the full stat name is retained in shared-memory, making
it easy to correlate stats across restarts even as the dynamic cluster
configuration changes.

One challenge with this fixed memory allocation strategy is that it limits
cluster scalabilty. A deployment wishing to use a single Envoy instance to
manage tens of thousands of clusters, each with its own set of scoped stats,
will use more memory than is ideal.

A flag `--disable-hot-restart` pivots the system toward an alternate heap-based
stat allocator that allocates stats on demand in the heap, with no preset limits
on the number of stats or their length. See
[HeapStatData](https://github.com/envoyproxy/envoy/blob/master/source/common/stats/heap_stat_data.h).

## Performance and Thread Local Storage

A key tenant of the Envoy architecture is high performance on machines with
large numbers of cores. See
https://blog.envoyproxy.io/envoy-threading-model-a8d44b922310 for details. This
requires lock-free access to stats on the fast path -- when proxying requests.

For stats, this is implemented in
[ThreadLocalStore](https://github.com/envoyproxy/envoy/blob/master/source/common/stats/thread_local_store.h), supporting the following features:

 * Thread local per scope stat caching.
 * Overlapping scopes with proper reference counting (2 scopes with the same name will point to
   the same backing stats).
 * Scope deletion.
 * Lockless in the fast path.

This implementation is complicated so here is a rough overview of the threading model.

 * The store can be used before threading is initialized. This is needed during server init.
 * Scopes can be created from any thread, though in practice they are only created from the main
   thread.
 * Scopes can be deleted from any thread, and they are in practice as scopes are likely to be
   shared across all worker threads.
 * Per thread caches are checked, and if empty, they are populated from the central cache.
 * Scopes are entirely owned by the caller. The store only keeps weak pointers.
 * When a scope is destroyed, a cache flush operation is run on all threads to flush any cached
   data owned by the destroyed scope.
 * Scopes use a unique incrementing ID for the cache key. This ensures that if a new scope is
   created at the same address as a recently deleted scope, cache references will not accidentally
   reference the old scope which may be about to be cache flushed.
 * Since it's possible to have overlapping scopes, we de-dup stats when counters() or gauges() is
   called since these are very uncommon operations.
 * Though this implementation is designed to work with a fixed shared memory space, it will fall
   back to heap allocated stats if needed. NOTE: In this case, overlapping scopes will not share
   the same backing store. This is to keep things simple, it could be done in the future if
   needed.

### Histogram threading model

Each Histogram implementation will have 2 parts.

 * *main* thread parent which is called `ParentHistogram`.
 * *per-thread* collector which is called `ThreadLocalHistogram`.

Worker threads will write to ParentHistogram which checks whether a TLS
histogram is available. If there is one it will write to it, otherwise creates
new one and writes to it. During the flush process the following sequence is
followed.

 * The main thread starts the flush process by posting a message to every worker which tells the
   worker to swap its *active* histogram with its *backup* histogram. This is achieved via a call
   to the `beginMerge` method.
 * Each TLS histogram has 2 histograms it makes use of, swapping back and forth. It manages a
   current_active index via which it writes to the correct histogram.
 * When all workers have done, the main thread continues with the flush process where the
   *actual* merging happens.
 * As the active histograms are swapped in TLS histograms, on the main thread, we can be sure
   that no worker is writing into the *backup* histogram.
 * The main thread now goes through all histograms, collect them across each worker and
   accumulates in to *interval* histograms.
 * Finally the main *interval* histogram is merged to *cumulative* histogram.

## Stat naming infrastructure and memory consumption

Stat names are replicated in several places in various forms.

 * Held fully elaborated next to the values, in `RawStatData` and `HeapStatData`
 * In [MetricImpl](https://github.com/envoyproxy/envoy/blob/master/source/common/stats/metric_impl.h)
   in a transformed state, with tags extracted into vectors of name/value strings.
 * In static strings across the codebase where stats are referenced
 * In a [set of
   regexes](https://github.com/envoyproxy/envoy/blob/master/source/common/config/well_known_names.cc)
   used to perform tag extraction.

There are stat maps in `ThreadLocalStore` for capturing all stats in a scope,
and each per-thread caches. However, they don't duplicate the stat
names. Instead, they reference the `char*` held in the `RawStatData` or
`HeapStatData itself, and thus are relatively cheap; effectively those maps are
all pointer-to-pointer.

For this to be safe, cache lookups from locally scoped strings must use `.find`
rather than `operator[]`, as the latter would insert a pointer to a temporary as
the key. If the `.find` fails, the actual stat must be constructed first, and
then inserted into the map using its key storage. This strategy saves
duplication of the keys, but costs an extra map lookup on each miss.

## Tags and Tag Extraction

TBD

## Disabling statistics by substring or regex

TBD
