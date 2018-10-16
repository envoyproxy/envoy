# Envoy Stats System

Envoy statistics track numeric metrics on an Envoy instance, optionally spanning
binary restarts. The metrics are tracked as:

 * Counters: strictly increasing 64-bit integers.
 * Gauges: 64-bit integers that can rise and fall.
 * Histograms: mapping ranges of values to frequency. The ranges are auto-adjusted as
   data accumulates. Unliked counters and gauges, histogram data is not retained across
   binary restarts.

## Controlling hot-restart

TBD

## Performance and Thread Local Storage

This implementation supports the following features:

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

## Stat naming infrastructure and scopes

TBD

## Stat memory consumption

TBD

## Tags and Tag Extraction

TBD

## Disabling statistics by substring or regex

TBD
