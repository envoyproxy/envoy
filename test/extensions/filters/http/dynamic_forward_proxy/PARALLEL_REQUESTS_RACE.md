# DFP ParallelRequests race: root cause and proposed fix

## Summary

- A cluster-host miss followed by a DNS-cache hit incorrectly becomes a failed host selection.
- `Cluster::LoadBalancer::chooseHost()` returns a null host for `InCache`, even after successful DNS.
- The current first-resolution publication order is correct; the two lookups are not one transaction.
- Fix host selection to consume a coherent resolution result; unify generation-aware worker publication.
- Static analysis establishes this execution; the sandbox could not build the instrumented reproduction.

## Reproduction

This is a docs/analysis-only, AI-assisted report. No product code or test changes are included.
References describe source commit `39ecf16aab5d29c1812900ef40395cd401f25e96`, the parent of
the report branch's initial planning commit. Absolute paths below use this investigation checkout.
For compact sequence diagrams and tables, these aliases denote the exact files:

| Alias | File |
| --- | --- |
| C | `/home/runner/work/envoy/envoy/source/extensions/clusters/dynamic_forward_proxy/cluster.cc` |
| CH | `/home/runner/work/envoy/envoy/source/extensions/clusters/dynamic_forward_proxy/cluster.h` |
| D | `/home/runner/work/envoy/envoy/source/extensions/common/dynamic_forward_proxy/dns_cache_impl.cc` |
| DH | `/home/runner/work/envoy/envoy/source/extensions/common/dynamic_forward_proxy/dns_cache_impl.h` |
| F | `/home/runner/work/envoy/envoy/source/extensions/filters/http/dynamic_forward_proxy/proxy_filter.cc` |
| R | `/home/runner/work/envoy/envoy/source/common/router/router.cc` |
| T | `/home/runner/work/envoy/envoy/test/extensions/filters/http/dynamic_forward_proxy/proxy_filter_integration_test.cc` |

`ParallelRequests` is at
`/home/runner/work/envoy/envoy/test/extensions/filters/http/dynamic_forward_proxy/proxy_filter_integration_test.cc:690-712`.
It uses HTTP/2 downstream and upstream, disables upstream TLS, enables the autonomous upstream,
and issues two header-only requests on one connection without waiting between them.
The bootstrap explicitly installs getaddrinfo (`T:137-145`).

**Important correction to the premise:** `initializeWithArgs()` does not unconditionally prepend
the DFP filter. Its `use_dfp_even_when_cluster_resolves_hosts` argument defaults to false
(`T:57-65`). Both configuration branches prepend the filter only when
`dfp_cluster_resolves_hosts` is false or that argument is true (`T:100-129`).
`ParallelRequests` supplies neither override (`T:702`). The runtime guard defaults on at
`/home/runner/work/envoy/envoy/source/common/runtime/runtime_features.cc:54`.
Consequently, this test normally exercises **router → DFP cluster → DNS cache**, not
filter → DNS cache → router. The stream-info-to-headers filter is not the DFP filter.

For local diagnosis, one temporary line, `useAccessLog("%RESPONSE_CODE_DETAILS%");`, was
inserted at the beginning of `ParallelRequests`, then removed after this command failed:

```sh
cd /home/runner/work/envoy/envoy
/tmp/fix-bazel-truststore.sh
bazel test -c fastbuild --define wasm=wasmtime \
  //test/extensions/filters/http/dynamic_forward_proxy:proxy_filter_integration_test \
  --runs_per_test=50 --test_filter='*ParallelRequests/*' --test_output=errors
```

Bazel 8.8.1 exited with code 32 before compilation:

```text
ERROR: Error computing the main repository mapping: Error accessing registry https://bcr.bazel.build:
Failed to fetch registry file https://bcr.bazel.build/modules/googleapis-python/1.0.0/MODULE.bazel:
Unknown host: bcr.bazel.build
```

**Executed runs: 0. Observed failing `RESPONSE_CODE_DETAILS`: unavailable.** This is not a
claim that 50 runs passed or that the failure was reproduced. The sandbox cannot build Envoy
with its current registry connectivity. No jemalloc-specific run completed either.
The supplied status assertion alone does not distinguish DNS failure, selection failure, or
transport failure. The execution below proves a product defect matching the reported status
pattern; associating a particular historical failure with it still requires its access log.

## Request lifecycle (worker)

The filter path matters for comparison and for deployments that retain the filter:

1. `ProxyFilter::decodeHeaders()` finds the route and thread-local cluster, checks its DFP
   cluster type, chooses the default port, and applies host rewrites (`F:192-244`).
   It acquires a pending-DNS resource (`F:266-274`) and calls
   `loadDnsCacheEntryWithForceRefresh()` (`F:331-336`).
   The legacy `loadDnsCacheEntry()` delegates with `force_refresh=false` at
   `/home/runner/work/envoy/envoy/source/extensions/common/dynamic_forward_proxy/dns_cache.h:230-234`.
2. The cache normalizes the authority and reads **shared** `primary_hosts_` under
   `primary_hosts_lock_`, accepting a cache hit only after `firstResolveComplete()`
   (`D:127-148`). There is no worker-local DNS `host_map_` in this implementation:
   `ThreadLocalHostInfo` contains `pending_resolutions_`, not a host map (`DH:105-114`).
3. `InCache` returns the shared host info and no handle (`D:150-164`). The filter continues
   for a usable address, or sends a DNS-failure local reply for a null address
   (`F:338-355`). `firstResolveComplete` means an attempt completed, **not** that it succeeded.
   The null-address re-resolve and unused-connection force-refresh guards default off at
   `/home/runner/work/envoy/envoy/source/common/runtime/runtime_features.cc:258-260`.
4. `Overflow` returns no handle when a miss encounters the maximum host count (`D:166-169`);
   the filter sends 503 `dns_cache_overflow` (`F:361-367`).
5. On a miss, the cache posts `startCacheLoad()` to main (`D:171-175`), constructs
   `LoadDnsCacheEntryHandleImpl`, and returns `Loading` (`D:177-194`).
   Its `RaiiMapOfListElement` constructor registers it in this worker's per-host list
   (`DH:82-92`). Destroying the handle cancels registration. The filter retains the handle
   and stops with `StopAllIterationAndWatermark` (`F:333,357-360`).
6. `ThreadLocalHostInfo::onHostMapUpdate()` moves the completed host's list out of
   `pending_resolutions_`, erases the map entry, cancels each registration, and invokes
   `onLoadDnsCacheComplete()` (`D:687-702`). Moving the list protects against callbacks
   inserting more host keys and invalidating the map iterator.
   The filter releases its circuit-breaker resource and resumes decoding for an address,
   or sends 503 with `host_info->details()` for no address (`F:475-489`).

The comment's historical **“L171-183”** is now `D:177-193`: after inserting the handle,
the cache rechecks `firstResolveComplete()` under the shared-map read lock. If completion
overtook registration, it posts a deferred `onHostMapUpdate()` to the same worker.
That deferral prevents a completion callback from re-entering `decodeHeaders()` before the
caller has stored its handle and suspended. It makes an already-completed result deliverable
to the newly registered waiter, addressing the missed-completion/late-registration case
described in PR #44515; it does **not** change the `InCache` return path.
“Main posted a notification before insertion” alone does not prove an orphan: an ordinary
worker cannot execute that queued callback in the middle of its current request stack.
An actual orphan requires the completion to have been consumed before registration with
no subsequent notification. The old disabled test's comment is not proof of that ordering.

## Resolution lifecycle (main)

`startCacheLoad()` coalesces duplicate loads: an existing entry with no forced refresh
returns immediately (`D:237-257`). Otherwise it creates the primary entry (`D:263-291`).
The entry is visible through the shared map immediately, **without an address**, but is
not yet an `InCache` result because `firstResolveComplete` is false.

`startResolve()` increments `dns_query_attempt`, arms the timeout, and invokes the resolver
(`D:419-433`). getaddrinfo runs on its own resolver thread and posts its callback to the
main dispatcher at
`/home/runner/work/envoy/envoy/source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.cc:234-241`.
The callback calls `finishResolve()` (`D:428-432`).

For a first successful, nonempty answer, `finishResolve()` does this in order:

1. Capture `first_resolve`, stop the timeout, and clear the active query (`D:524-535`).
2. Generate the address list, update persistence and staleness (`D:538-568`).
3. `setAddresses()` stores addresses, details, and resolver status under `resolve_lock_`
   (`D:577-579,859-866`). Workers can now read the address on the shared object.
4. Synchronously call `runAddUpdateCallbacks()` (`D:587,648-654`), which calls each
   cluster's `onDnsHostAddOrUpdate()` **on main**, not via a worker post.
   `addOrUpdateHost()` creates an addressed `LogicalHost` and inserts it under
   `host_map_lock_` (`C:273-335`), then updates the priority set (`C:342-369`).
   Workers calling `findHostByName()` read this same global map (`C:514-529`).
5. Only after those callbacks return, set `firstResolveComplete()` (`D:590`).
   The first-attempt completion also sets it at `D:601-603`, including failed attempts.
6. `notifyThreads()` posts one `onHostMapUpdate()` per worker (`D:604-606,670-675`).
   Each worker notifies its existing pending handles when that post executes.
7. Run resolution-complete callbacks (`D:608,656-661`) and schedule refresh
   (`D:610-644`). `Cluster::onDnsResolutionComplete()` is a **no-op** (`CH:42-44`);
   it does not publish another host map or release the waiters.

Thus there are **not** two TLS DNS posts, one inserting an empty host and another filling
its address. A request can observe an unresolved primary entry, but the cache returns
`Loading`, not successful `InCache`. A first successful cluster host never starts life
with a null address: its constructor receives the resolved address (`C:318-320`).
The successful first-resolution readiness ordering already prevents “cache ready before
cluster insertion.” It does not prevent a worker's earlier cluster miss from becoming stale.

On failure or an empty answer, `D:517-520` forms `dns_resolution_failure{...}`.
With no previous address, details/status are stored (`D:593-599`), completion is marked,
and pending workers are notified without adding a cluster host. Existing non-null
addresses survive subsequent resolution failures (`D:539-545`).

## Cluster resolve path

The router calls `cluster->chooseHost(this)` and either handles a synchronous result or
retains its cancelable and suspends (`R:740-783`). DFP's load balancer normalizes the
authority and calls `findHostByName()` (`C:452-463`).

- A healthy host is returned directly (`C:464-470`). Lookup and address publication use
  locks, not unsynchronized C++ memory accesses.
- A miss with cluster DNS enabled acquires a DNS pending-request resource and calls the
  same cache API used by the filter (`C:473-487`).
- `Loading` attaches the DNS handle and resource guard to `DFPHostSelectionHandle` and
  returns an asynchronous host-selection result (`C:491-498`).
  Its completion **re-looks up the cluster host** (`CH:182-192`) and calls the router's
  `onAsyncHostSelection()`, which obtains a pool or sends the failure (`R:787-798`).
- **`InCache` unconditionally returns `{nullptr, cached_details}` (`C:489-490`).**
  It neither checks the cached address nor retries `findHostByName()`.
  This branch assumes the earlier cluster miss proves cached DNS is unusable.
  Main is free to complete resolution between those two reads; that assumption is false.

`Cluster::onDnsHostRemove()` erases the shared cluster host under its own lock, then updates
priority state (`C:372-382`). It is not a per-worker DNS callback.
Cluster-manager priority/pool updates are separate from DNS waiter notifications; they
must not be confused with `findHostByName()`'s shared-map visibility.

## 503 exits and RESPONSE_CODE_DETAILS

The following covers selection and connection failures for the two-request, one-host,
header-only HTTP/2 flow. “Exact” means the emitted string, not the response body or response
flag. `R:1085-1094` uses nonempty selection details **verbatim**, falling back to
`no_healthy_upstream` only for empty details.

| Exit | Exact `RESPONSE_CODE_DETAILS` | Evidence and applicability |
| --- | --- | --- |
| Cluster miss, then successful cache `InCache` | Successful resolver detail, whitespace replaced by `_`; on this sandbox's glibc: **`Unknown_error`** | `C:489-490`, `D:518-520,578-579`, `R:1085-1094`. This is the demonstrated race, not a DNS failure. |
| DNS fails or returns no usable addresses, synchronously cached or asynchronously completed | `dns_resolution_failure{<whitespace-normalized resolver detail>}`; timeout: **`dns_resolution_failure{resolve_timeout}`** | `D:304-310,517-520,593-605`, `C:489-490`, `CH:182-192`. Both paths have no selected host. |
| Null/unusable host or pool with no supplied selection detail | **`no_healthy_upstream`** | `R:1057-1094`; also the filter-only legacy cluster miss (`C:468-470`). |
| Pool connection failure, connection timeout, overflow, or upstream reset before response headers | **`upstream_reset_before_response_started{<reason>[\|<transport failure>]}`**, all whitespace replaced by `_` | `/home/runner/work/envoy/envoy/source/common/router/upstream_request.cc:626-649`, `R:1692-1773`. This requires the flow to reach an upstream attempt; it is not the null-selection race. |

The successful getaddrinfo detail is not `"dns_resolution_failure"` and is not reliably
`"no_healthy_upstream"`: the resolver calls `gai_strerror(rc.return_value_)` even for zero at
`/home/runner/work/envoy/envoy/source/extensions/network/dns_resolver/getaddrinfo/getaddrinfo.cc:187-189,231`.
A local libc call during this investigation returned `"Unknown error"` for zero.
`StringUtil::replaceAllEmptySpace` makes that `Unknown_error`. This verifies the local libc
string, **not** an Envoy reproduction. Other libc implementations/locales supply their own
success detail. DNS tracing is off by default (`D:82-83`); enabling it appends trace data.
For example, getaddrinfo's `Name or service not known` produces
`dns_resolution_failure{Name_or_service_not_known}`.

For resets, concrete no-transport-suffix strings include
`upstream_reset_before_response_started{local_connection_failure}`,
`upstream_reset_before_response_started{remote_connection_failure}`,
`upstream_reset_before_response_started{connection_timeout}`,
`upstream_reset_before_response_started{connection_termination}`,
`upstream_reset_before_response_started{overflow}`,
`upstream_reset_before_response_started{local_reset}`,
`upstream_reset_before_response_started{local_refused_stream_reset}`,
`upstream_reset_before_response_started{remote_reset}`, and
`upstream_reset_before_response_started{remote_refused_stream_reset}`.
The reason mapping is at
`/home/runner/work/envoy/envoy/source/common/http/utility.cc:1161-1198`;
`R:1770-1772` adds the optional `|...` suffix. A protocol error returns 502, not 503
(`R:1742-1744`). A response timeout is ordinarily 504, not this selection failure.
The defensive `RemoteResetNoError` branch emits the bare
`upstream_reset_before_response_started` (`R:1699-1715`); its own comment identifies it
as an invalid pre-header ordering. The bare early-reset data branch (`R:1212-1215`)
requires downstream data and is not reachable for these header-only requests.

For completeness, these neighboring 503 exits must be distinguished rather than attributed
to this cold-host race:

| Exit | Exact detail | Why it is not the demonstrated two-request failure |
| --- | --- | --- |
| Cluster DNS pending breaker | `dns_cache_pending_requests_overflow` (`C:474-478`) | Two requests do not exhaust the configured 1024 slots. |
| DNS host-count overflow | `dns_cache_overflow` (`C:499-504`, `F:364-366`) | One hostname does not exhaust the configured 1024 hosts. |
| Filter pending breaker | `dynamic_forward_proxy_pending_request_overflow` (`F:268-272`) | Filter absent by default; also a 1024-request limit. |
| Filter cluster removed | `dynamic_forward_proxy_cluster_is_gone` (`F:248-259`) | Filter absent; test does not remove the cluster. |
| Filter subcluster failures | `sub_cluster_overflow`, `sub_cluster_warming_timeout` (`F:406-412,453-459`) | Subclusters are disabled in this test. |
| Empty authority | `empty_host_header` (`C:452-455`) | Test uses `localhost:<port>`, not an empty authority. |
| LB destroyed during async selection | `load_balancer_destroyed` (`C:532-540`) | Test keeps the cluster alive until responses finish. |
| Router cluster disappeared, maintenance, shedding, or configured drop | `cluster_not_found`, `maintenance_mode`, `overload`, `drop_overload`, `unconditional_drop_overload` | `R:579-628,820-823,2576-2610`; these require configuration/lifecycle changes absent here. |

With the filter enabled, its DNS failure uses the same cached `dns_resolution_failure{...}`
details (`F:462-484`); its missing-info sentinel is `no_host` (`F:349-350`).
It does not emit a bare `dns_resolution_failure` at those call sites.
An upstream-generated 503 is a separate response path, normally `via_upstream`
(`R:2082-2083`); the autonomous upstream in this test is configured for success, not an
intentional 503.

For a confirming run, keep the requested details log, and additionally log
`%RESPONSE_CODE% %RESPONSE_FLAGS% %UPSTREAM_HOST% %UPSTREAM_TRANSPORT_FAILURE_REASON%`.
503 + `UH` + the successful resolver detail and no upstream host distinguishes this
selection defect from `dns_resolution_failure{...}` and a pre-header transport reset.
`no_healthy_upstream` alone does not establish this particular interleaving.

## Main→worker hand-offs

“Atomic” here means one protected operation or one worker-dispatcher callback, not that
all workers execute simultaneously. Separately locked reads do not form a transaction.

| Hand-off | Atomic? | Window | Effect |
| --- | --- | --- | --- |
| Worker posts `startCacheLoad` to main (`D:171-178`) | Multi-step | Main can process the load before the caller registers its handle. | `Loading` caller must register and receive deferred completion; recheck at `D:180-193` handles an already completed entry. |
| Resolver thread posts result to main (getaddrinfo:234-241) | Single callback | Result remains queued until main executes it. | Requests continue to register `Loading`; this does not publish readiness. |
| Main inserts `primary_hosts_` (`D:274-291`) | Single locked insertion, part of multi-step resolution | Entry exists with no address until `D:578`. | Cache requests see unresolved state and take `Loading`, not successful `InCache`. |
| Main stores address/status/details (`D:859-866`) | Single locked update | Address visible before cluster callback and first-completion bit. | First-resolution cache lookup still takes `Loading`; direct `getHost()` is a different API. |
| Main adds cluster host, marks cache complete, then posts DNS completion (`D:587-605`) | Multi-step | Shared cluster host is visible before cache completion and before queued notification executes. | New cluster lookup succeeds; existing waiters resume later. No first-success “ready cache, absent cluster” publication gap. |
| Worker cluster lookup, then shared DNS-cache lookup (`C:463-490`) | **Multi-step** | Main can finish all publication between those reads. | Earlier miss + later `InCache` incorrectly returns null and sends 503. This window survives perfectly ordered publication. |
| Priority membership/pool update vs DNS notification (`C:351,367-369`, `D:605`) | Separate worker callbacks | Shared host map changes before worker priority/pool bookkeeping. | New lookup uses shared host map, not an empty TLS DNS host; pool bookkeeping and waiter delivery are not one transaction. |
| Removed-host pool drain vs membership update (cluster manager:593-622,1194-1210) | Separate worker posts | Membership changes before removed-host pools are drained on each worker. | In-flight users retain the old host; future selections must not resurrect its generation. |
| Per-worker DNS completion (`D:670-700`) | Single worker callback | Other workers consume their own queues independently. | Moves/removes pending list, then calls waiters; no DNS-map insertion occurs in this callback. |
| Worker-local deferred catch-up (`D:180-193`) | Single queued callback after a second shared-state read | Main completed resolution before the registration recheck. | Delivers to the registered waiters after the caller suspends; a duplicate notification finds no remaining handles. |
| Main TTL/force-refresh removal (`D:330-355`, `C:372-382`) | **Multi-step** | Cluster host is erased before primary DNS entry; notifications follow. | Cluster miss can meet old completed cache entry and hit the same null `InCache` branch. Existing selected hosts retain shared ownership. |
| Address change / replacement (`D:578-590`, `C:288-335`) | Multi-step overall; cluster-map replacement itself is locked | DNS address changes before cluster logical-host update/replacement and later pool-drain delivery. | Lookups can use the previous host generation; the locked erase+insert does not expose an empty map between them. |
| Successful recovery after a failed first attempt (`D:577-603`) | Multi-step | The first-completion bit is already true when a formerly null address is filled, before the cluster callback. | Filter/cache consumers can read the new address before a cluster host exists; this is distinct from the first-success ordering and belongs in generation-aware recovery coverage. |

TLS implementation:
`/home/runner/work/envoy/envoy/source/common/thread_local/thread_local_impl.cc:183-192`
posts to workers and invokes the callback directly on main.
Cluster-manager membership delivery and pool draining are separately implemented at
`/home/runner/work/envoy/envoy/source/common/upstream/cluster_manager_impl.cc:1239-1364`
and
`/home/runner/work/envoy/envoy/source/common/upstream/cluster_manager_impl.cc:1194-1210`.
Neither mechanism makes the two reads at `C:463` and `D:145` atomic together.

## Interleaving that yields request2 → 503

Each `file-alias:line` below expands to the absolute file in the reproduction table.
All steps obey the existing locks and callback ordering; no reentrant worker execution
or failed localhost lookup is required.

```text
Worker W (both HTTP/2 streams)              Main / resolver
---------------------------------------   --------------------------------------
request1: C:463 -> no cluster host
C:486 -> D:145 -> cache miss
D:172 posts startCacheLoad
D:177 registers handle1
C:491-498 returns async; R:764 stores it
                                          D:263 creates primary host
                                          D:428 starts getaddrinfo
request2: C:463 -> no cluster host
           [pause before C:486]
                                          resolver callback -> D:432 finishResolve
                                          D:578 stores non-null address + details
                                          D:587 -> C:349 adds addressed LogicalHost
                                          D:590 marks firstResolveComplete
                                          D:605 posts worker notification
           [resume same worker stack]
C:486 -> D:145 sees completed DNS entry
D:163 returns InCache + successful details
C:489-490 returns NULL HOST + those details
R:747 -> R:1077 -> R:1093 sends request2 503
return to worker dispatcher
D:687-700 consumes queued notification
handle1 -> CH:185 finds addressed cluster host
CH:192 -> R:787 resumes request1
autonomous upstream responds; request1 200
```

Waiting for response1 first in the test does not impose a server-side order on stream2.
Request2's failure is synchronous, while request1 waits for the queued callback.
Slow instrumentation/build configurations change scheduling opportunities; this analysis
does not claim an allocator-specific cause or quantify the window.

## Prior fixes and why they were insufficient

History was fetched beyond the initial shallow clone. Investigation used `git log -S`
and `git blame` on all four requested implementation/test files, then inspected the diffs:

```sh
cd /home/runner/work/envoy/envoy
git log --oneline -S 'ParallelRequestsWithFakeResolver' -- \
  test/extensions/filters/http/dynamic_forward_proxy/proxy_filter_integration_test.cc
git log --oneline -S 'ParallelRequestSecondHandleOrphanedWithoutFix' -- \
  test/extensions/filters/http/dynamic_forward_proxy/proxy_filter_integration_test.cc
git log --oneline -S 'resolved during handle registration' -- \
  source/extensions/common/dynamic_forward_proxy/dns_cache_impl.cc
git log --oneline -S 'dfp_cluster_resolves_hosts' -- \
  source/extensions/clusters/dynamic_forward_proxy/cluster.cc \
  source/extensions/filters/http/dynamic_forward_proxy/proxy_filter.cc
git blame -L 177,194 -- source/extensions/common/dynamic_forward_proxy/dns_cache_impl.cc
git blame -L 463,490 -- source/extensions/clusters/dynamic_forward_proxy/cluster.cc
```

- **`37e9414964de` — [#14349](https://github.com/envoyproxy/envoy/pull/14349),
  “Replace copy-on-add/remove with lock-guarded global map.”**
  Removed per-worker DNS-map copies in favor of the shared primary map and completion
  notifications. Describing today's bug as two posts that populate a TLS DNS map is
  describing an obsolete architecture.
- **`d85d262d874f` — [#30506](https://github.com/envoyproxy/envoy/pull/30506),
  “Http filter dynamic forward proxy: bug fix (attempt two).”**
  Removed `setAddresses()`'s eager assignment of `first_resolve_complete_` from the header
  implementation (under a runtime guard), setting it after add/update callbacks instead.
  This closed the concrete window where a successful first answer became `InCache` before
  its cluster host existed. The diff's release note explicitly identifies concurrent-authority
  503 `no_healthy_upstream`. It did not reset an already-true bit during later recovery.
  Its runtime guard was removed by `c3241e42bc`
  ([#35379](https://github.com/envoyproxy/envoy/pull/35379)). Correct publication order
  does not invalidate a miss already observed before that publication.
- **`e1828f4913da` — [#33735](https://github.com/envoyproxy/envoy/pull/33735),
  “dns: add parallel resolution test.”**
  Added **both** `ParallelRequests` and `ParallelRequestsWithFakeResolver`.
  The fake test was not first introduced by #44515, despite that later commit message's
  wording. Its blocked resolver exercises concurrent pending requests; the real resolver
  is free to complete between cluster lookup and cache lookup.
- **`d21b7ed585da` — [#38237](https://github.com/envoyproxy/envoy/pull/38237),
  “allowing the dfp cluster to do dns lookups.”**
  Added router-driven asynchronous DNS selection and the null-host `InCache` branch;
  `git blame` attributes today's `C:489-490` to this commit. Updated the fixture to omit
  the DFP filter when the cluster resolves hosts. This introduced a distinct miss-then-hit
  problem into the new consumer; the older filter publication fix does not address it.
- **`c82157703bf8` — [#44515](https://github.com/envoyproxy/envoy/pull/44515),
  completion/registration race fix.**
  Added the post-registration recheck, a worker-dispatcher reference in
  `ThreadLocalHostInfo`, unit coverage, and
  `DISABLED_ParallelRequestSecondHandleOrphanedWithoutFix` (`T:748-783`).
  Its added recovery is on the `Loading` path, not the synchronous `InCache` path.
  The disabled test neither asserts the exact critical interleaving nor runs normally.
  Its comment says response2 would wait forever, a different failure from response2 503.
  The enabled fake test's current gates (`T:735-740`) arrange DNS-in-flight concurrency,
  not a barrier between `C:463` and `C:486`.

The downstream-request counter is not a proof that request2 has reached a particular LB
instruction or inserted its DNS handle. Passing the fake-resolver test therefore does not
prove the two-read selection race absent.

## Proposed structural fix

**Is the root problem two independent consumers reading a cache whose ready state is
published in multiple steps?** That describes a broader consistency risk, but not the
precise current failure. This test normally has no DFP filter. Even in the cluster alone,
two independent reads span a valid publication: cluster miss at time A, DNS hit at time B.
The first-success publish order is already correct. Combining two main→worker posts
without changing host selection would **not** close the diagrammed race.

**Required immediate correctness change, in a subsequent code PR:** change
`Cluster::LoadBalancer::chooseHost()`'s `InCache` branch to consume the resolved result,
including re-looking up the cluster host after a successful cached resolution, instead of
returning null unconditionally. Preserve real DNS-failure details when no usable result
exists. Use one result-handling function for synchronous and asynchronous completion
(`CH:182-192`), so the two paths cannot disagree about a resolved entry.
This closes the demonstrated first-resolution execution, but a relookup alone is not an
eviction/replacement protocol.

**Structural proposal: a generation-aware resolve-and-select transaction.**

New invariant: for a host key and generation, a worker observes one of `Resolving`,
`Ready(addresses, cluster LogicalHost)`, `Failed(details)`, or `Removed`.
`Ready` must include the host the LB can return, not only a DNS-completion boolean.
Every waiter for that generation receives exactly one outcome or cancels. A completed
successful result cannot be translated to “no host” because of an earlier lookup miss.
Failed outcomes remain publishable and wake waiters; hiding all null-address entries
forever would turn DNS failures into hangs. Proxy lookups that intentionally need no
address require an explicit outcome, not the normal addressed `Ready` invariant.

Concrete function changes for that design:

1. `createHost()` / `startCacheLoad()` allocate a generation and retain in-flight main
   state without advertising it as a usable endpoint. Both filter and LB register through
   a shared resolve-result subscription, including cached results.
2. `finishResolve()` constructs an immutable outcome. `runAddUpdateCallbacks()` /
   `Cluster::onDnsHostAddOrUpdate()` prepare the corresponding cluster hosts before
   publishing `Ready`. Host-construction failure must become an explicit failed outcome,
   not merely `ENVOY_BUG` followed by marking complete (`D:587-590` today).
   Multiple clusters sharing a DNS cache need per-cluster prepared-host results.
3. Replace `notifyThreads()`'s mutable-info-only message with an outcome/generation message.
   In one callback per worker, install that generation's DNS and cluster selection view,
   perform required membership bookkeeping, then detach and notify its pending handles.
   `ThreadLocalHostInfo::onHostMapUpdate()` is the natural installation/delivery boundary.
   Integrate the cluster-manager membership update with this transaction rather than
   assuming a separately queued priority update constitutes readiness.
4. Change `LoadBalancer::chooseHost()` and
   `DFPHostSelectionHandle::onLoadDnsCacheComplete()` to consume the same worker-visible
   outcome. `ProxyFilter::decodeHeaders()` / `onLoadDnsCacheComplete()` use that outcome's
   DNS portion. Remove independent “miss means cache hit must be failure” inference.
   Cached results and newly completed results obey the same contract.
5. Make `removeHost()` / `Cluster::onDnsHostRemove()` publish a generation tombstone in
   the same protocol. Late DNS callbacks cannot resurrect a removed generation or complete
   handles belonging to a new one. Keep already selected shared hosts alive; drain their
   pools through ordered membership removal. Preserve force-refresh waiters intentionally
   carried to the next generation (`D:351-355`).

This includes `host_ttl` eviction (`D:313-355`), forced removal/re-resolution
(`D:253-260`), and address change. In particular,
`dfp_cluster_replace_host_on_address_change` replaces the logical host at `C:290-334`
([#48176](https://github.com/envoyproxy/envoy/pull/48176), `3ae2dbdfa3`).
The erase and insert are already protected by **one** cluster write lock; there is no
observable empty-map gap inside that replacement. The cross-object generation change and
later pool drain still need coordinated publication. Preserve the old host if replacement
construction fails, as the existing implementation does (`C:318-328`).

Do not hold a global map lock while invoking filter/router callbacks: they re-enter cache
and cluster operations. Snapshot installation and waiter-list detachment precede callbacks;
cancellation and teardown must remain safe.

## Test strategy

Keep `ParallelRequests` as a regression test, but make the critical ordering deterministic;
do not remove it as redundant with `ParallelRequestsWithFakeResolver`.
The existing fake test is a useful **both requests pending before DNS completion** case,
not a forced **cluster miss before completion, cache hit after completion** case.

In a follow-up code/test PR:

1. Add a deterministic cluster test with an initially empty cluster map. After the first
   `findHostByName()` miss and before the cache call, complete the pending DNS query and
   cluster insertion. Assert the cache reports `InCache` and `chooseHost()` returns the
   new host. Pre-fix it returns null with successful resolver details. Test both IP families.
2. Gate the integration regression at the same boundary using the test resolver and a
   narrowly scoped synchronization hook; keep HTTP/2/autonomous upstream coverage.
   Assert both responses are 200 and log details for failures. Waiting on a request counter
   or sleeping is not an instruction-level ordering guarantee.
3. Retain the pending/pending fake-resolver test. Add precise registration-vs-completion
   coverage for #44515 rather than relying on the disabled test's schedule.
4. Cover filter-only, cluster-only, and explicitly filter-plus-cluster configurations,
   cached failure, empty answer, timeout, cancellation, and host-construction failure.
5. For the structural design, force eviction and refresh between reads, ignore stale
   generations, verify replacement/pool draining, multiple clusters sharing a cache, and
   no callback after cancellation or worker/cluster destruction.

Finally repeat the reproduction command in a build-capable environment and retain access
logs from all failures, including slow fastbuild/wasmtime/jemalloc configurations.
The 50-run stress check supplements the deterministic regression; it does not replace it.
This report-only PR adds no tests and changes no existing tests.

## Production impact

**Yes, the demonstrated product race is reachable in production.** A burst to a cold host
can put one request in asynchronous DNS selection and a second between the cluster miss
and cache lookup when DNS completes. The second receives a spurious 503 despite successful
resolution and an available addressed cluster host. The two reads are not serialized
against main; no test-only resolver or autonomous-upstream behavior is required.
HTTP/2 merely makes concurrent same-worker streams straightforward.

This does not establish the frequency or the exact signature of the supplied historical
failure without a reproducing access log. It establishes a concrete legal execution,
explains why the previous fixes and fake-resolver gates do not eliminate it, and defines
the invariant and regression needed to prevent it.
