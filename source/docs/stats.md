# Envoy Stats System

Envoy statistics track numeric metrics on an Envoy instance, optionally spanning
binary program restarts. The metrics are tracked as:

 * Counters: strictly increasing 64-bit integers.
 * Gauges: 64-bit integers that can rise and fall.
 * Histograms: mapping ranges of values to frequency. The ranges are auto-adjusted as
   data accumulates. Unliked counters and gauges, histogram data is not retained across
   binary program restarts.

In order to support restarting the Envoy binary program without losing counter and gauge
values, they are passed from parent to child in an RPC protocol.
They were previously held in shared memory, which imposed various restrictions.
Unlike the shared memory implementation, the RPC passing *requires special indication
in source/common/stats/stat_merger.cc when simple addition is not appropriate for
combining two instances of a given stat*.

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
 * Overlapping scopes will not share the same backing store. This is to keep things simple,
   it could be done in the future if needed.

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

 * Held with the stat values, in `HeapStatData`
 * In [MetricImpl](https://github.com/envoyproxy/envoy/blob/master/source/common/stats/metric_impl.h)
   in a transformed state, with tags extracted into vectors of name/value strings.
 * In static strings across the codebase where stats are referenced
 * In a [set of
   regexes](https://github.com/envoyproxy/envoy/blob/master/source/common/config/well_known_names.cc)
   used to perform tag extraction.

There are stat maps in `ThreadLocalStore` for capturing all stats in a scope,
and each per-thread caches. However, they don't duplicate the stat names.
Instead, they reference the `char*` held in the `HeapStatData` itself, and thus
are relatively cheap; effectively those maps are all pointer-to-pointer.

For this to be safe, cache lookups from locally scoped strings must use `.find`
rather than `operator[]`, as the latter would insert a pointer to a temporary as
the key. If the `.find` fails, the actual stat must be constructed first, and
then inserted into the map using its key storage. This strategy saves
duplication of the keys, but costs an extra map lookup on each miss.

### Naming Representation

When stored as flat strings, stat names can dominate Envoy memory usage when
there are a large number of clusters. Stat names typically combine a small
number of keywords, cluster names, host names, and response codes, separated by
`.`. For example `CLUSTER.upstream_cx_connect_attempts_exceeded`. There may be
thousands of clusters, and roughly 100 stats per cluster. Thus, the number
of combinations can be large. It is significantly more efficient to symbolize
each `.`-delimited token and represent stats as arrays of symbols.

The transformation between flattened string and symbolized form is CPU-intensive
at scale. It requires parsing, encoding, and lookups in a shared map, which must
be mutex-protected. To avoid adding latency and CPU overhead while serving
requests, the tokens can be symbolized and saved in context classes, such as
[Http::CodeStatsImpl](https://github.com/envoyproxy/envoy/blob/master/source/common/http/codes.h).
Symbolization can occur on startup or when new hosts or clusters are configured
dynamically. Users of stats that are allocated dynamically per cluster, host,
etc, must explicitly store partial stat-names their class instances, which later
can be composed dynamically at runtime in order to fully elaborate counters,
gauges, etc, without taking symbol-table locks, via `SymbolTable::join()`.

### Current State and Strategy To Deploy Symbol Tables

As of April 1, 2019, there are a fairly large number of files that directly
lookup stats by name, e.g. via `Stats::Scope::counter(const std::string&)` in
the request path. In most cases, this runtime lookup concatenates the scope name
with a string literal or other request-dependent token to form the stat name, so
it is not possible to fully memoize the stats at startup; there must be a
runtime name lookup.

If a PR is issued that changes the underlying representation of a stat name to
be a symbol table entry then each stat-name will need to be transformed
whenever names are looked up, which would add CPU overhead and lock contention
in the request-path, violating one of the principles of Envoy's [threading
model](https://blog.envoyproxy.io/envoy-threading-model-a8d44b922310). Before
issuing such a PR we need to first iterate through the codebase memoizing the
symbols that are used to form stat-names.

To resolve this chicken-and-egg challenge of switching to symbol-table stat-name
representation without suffering a temporary loss of performance, we employ a
["fake" symbol table
implementation](https://github.com/envoyproxy/envoy/blob/master/source/common/stats/fake_symbol_table_impl.h).
This implemenation uses elaborated strings as an underlying representation, but
implements the same API as the ["real"
implemention](https://github.com/envoyproxy/envoy/blob/master/source/common/stats/symbol_table_impl.h).
The underlying string representation means that there is minimal runtime
overhead compared to the current state. But once all stat-allocation call-sites
have been converted to use the abstract [SymbolTable
API](https://github.com/envoyproxy/envoy/blob/master/include/envoy/stats/symbol_table.h),
the real implementation can be swapped in, the space savings realized, and the
fake implementation deleted.

## Tags and Tag Extraction

TBD

## Disabling statistics by substring or regex

TBD
