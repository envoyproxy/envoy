# Replaying cached xDS resources to late watches

Status: implemented behind `envoy.reloadable_features.xds_replay_cached_resources` in
`WatchMap`, so it covers `NewGrpcMuxImpl` (the default delta mux) and the unified
`XdsMux::GrpcMuxSotw`/`GrpcMuxDelta`. The legacy SotW `GrpcMuxImpl`, still the default for
SotW while `envoy.reloadable_features.unified_mux` is false, is intentionally not changed.
EDS cache retirement and SDS seeding removal (rollout steps 3 and 4) are not done.

## Problem

Several Envoy subscribers can watch the same xDS resource name on one gRPC mux. The mux
collapses them into a single wire subscription, so the management server sees one interest in
the name and sends the resource once. The first watch receives it. A watch that is added
later, for a name that is already on the wire, is never told about the resource:

- For state-of-the-world (SotW) the new `DiscoveryRequest` lists the same `resource_names`
  as before. A conformant server (go-control-plane, Istio, gRPC xDS) only answers when the
  resource version differs from the ACKed version, so it stays silent.
- For delta xDS the name is already in the subscription, so no `resource_names_subscribe`
  change is sent at all.

The late watch therefore stays empty until either its `initial_fetch_timeout` fires, or the
server pushes a new version of the resource for an unrelated reason. Depending on the
subscriber this shows up as:

| Subscriber | Symptom |
|------------|---------|
| SDS (`SdsApi`) | Listener or cluster update that only changes `sds_config` bytes creates a new provider; it times out and goes active without a certificate, or warms forever with a zero timeout (#47309). |
| EDS (`EdsClusterImpl`) | Cluster update with unchanged EDS config has no endpoints until the timeout; then falls back to `EdsResourcesCache` if `envoy.restart_features.use_eds_cache_for_ads` is set. |
| RDS (`RdsRouteConfigProviderImpl`) | Avoided by keying providers on the hash of the whole `rds` config and sharing them across listeners. Any change to `config_source` bytes recreates the problem. |
| VHDS, SRDS, ECDS, on-demand CDS | Same mechanism, no workaround today. |

Each workaround lives in the subscriber, is keyed on something slightly different, and
requires the subscriber to know whether a sibling that still holds the resource exists.
PR #48183 adds a third such cache (SDS seeding) and the review of it found an init-ordering
bug that only exists because the seed is applied outside the normal subscription lifecycle.

The common cause is that the mux already knows "name X was delivered at version V and at
least one watch still cares about it", but throws that knowledge away as soon as the response
batch is dispatched. The fix belongs in the mux.

## Proposal

Keep, per mux and per type URL, the last accepted resource for every explicitly watched
name. When a watch is added (or its interest is widened) to a name that

1. is already watched by another watch on the same mux (so no new request will be sent), and
2. has a cached resource,

post a dispatcher task that delivers the cached resource to the new watch through its
ordinary `onConfigUpdate()` path, with the version it was originally delivered under. The
delivery does not touch the request state, does not ACK, and is indistinguishable to the
subscriber from a server push.

Resources are cached only for names that appear in an explicit interest set. Wildcard
watches (LDS, CDS in the common deployment) and xdstp glob collections are never cached,
so the memory cost is bounded by the number of explicitly named resources, which the
subscribers already hold a copy of.

The cache entry for a name is dropped when the last watch for that name goes away. There is
no expiry timer: a name with no watches has no wire subscription, so the next watch for it is
new to the mux and the server will send it.

### Where it lives

`WatchMap` (`source/extensions/config_subscription/grpc/watch_map.{h,cc}`) is the right
place. It is shared by `NewGrpcMuxImpl` (delta) and the unified `XdsMux::GrpcMuxImpl`
(SotW and delta), already owns `watch_interest_` (name to set of watches, which is the
refcount we need), already receives every delivered resource via `onConfigUpdate()`, and
already hosts the EDS cache hooks. The legacy SotW `GrpcMuxImpl` keeps its own
`ApiState::watches_` list and does not use `WatchMap`. It is not changed: it is meant to be
replaced by the unified mux, so SotW gets replay when `unified_mux` becomes the default.

### Changes to `WatchMap`

```cpp
struct CachedResource {
  // Owned copy. Decoded resources are arena-allocated per response batch (#48358) and do
  // not outlive the dispatch, so we must copy.
  ProtobufTypes::MessagePtr resource_;
  std::string version_;          // system version_info (SotW) or resource version (delta)
  std::vector<std::string> aliases_;
};
absl::flat_hash_map<std::string, CachedResource> cached_resources_;
Event::Dispatcher& dispatcher_;  // for the async replay post
```

- `onConfigUpdate(const std::vector<DecodedResourcePtr>&, version)` (SotW path): after the
  delivery loop, for every resource whose `interesting_resources[i]` is set **and** whose
  name has an entry in `watch_interest_` (so it was explicitly named, not routed via a
  wildcard or accept pattern), store a copy under `cached_resources_[name]`. This is the
  same place the EDS cache is updated today. A cached name that is absent from a later SotW
  response is **not** evicted: for explicitly named types the server may send a subset, and
  the watches that already hold the resource keep it too, so the cache follows them. Entries
  leave only with the last watch (below) or a delta removal.
- `onConfigUpdate(added_resources, removed_resources, system_version)` (delta path): store
  every added resource whose name is in `watch_interest_`; erase every name in
  `removed_resources`.
- `findRemovals()`: when `watch_interest_.erase(entry)` fires for a name, erase
  `cached_resources_[name]` too. This replaces the `eds_resources_cache_->removeResource()`
  calls in both `updateWatchInterest()` overloads.
- `findAdditions()`: when a name is **not** new to the subscription (the `else` branch,
  watch joins an existing `watch_interest_` set) and `cached_resources_` has it, record the
  pair `(watch, name)` in a pending-replay list. After `updateWatchInterest()` finishes, if
  the list is non-empty, post one dispatcher task to flush it.
- Replay task: for each pending `(watch, name)`, if the watch still exists in `watches_` and
  still has `name` in its `resource_names_`, build a `DecodedResourceImpl` from the cached
  copy (the public constructor taking `Arena::UniquePtr<Message>`, name, aliases, version
  already exists for this) and call `watch->callbacks_.onConfigUpdate({ref}, version)`.
  Group by watch so a watch that joined several cached names gets one call, which matters
  for SotW subscribers that treat each call as a full set. The replay uses the same callback
  form (SotW or delta) as the last real delivery on the map, and for SotW it marks the watch
  non-empty so a later update that drops the resource reaches it. Watches removed before the task
  runs are skipped: hold `Watch*` plus check membership, do not hold a reference across the
  post. Wrap each delivery in `TRY_ASSERT_MAIN_THREAD` and log on failure; a replay failure
  must not NACK anything because there is no outstanding response.
- Remove `eds_resources_cache_` from `WatchMap` once EDS is migrated (see rollout).

### Changes to the muxes

- `NewGrpcMuxImpl::SubscriptionStuff` and `XdsMux::GrpcMuxImpl::addWatch()`: pass the
  dispatcher into `WatchMap`. Both already have `dispatcher_`.
- Legacy `GrpcMuxImpl` (`grpc_mux_impl.{h,cc}`): no change.

### What is **not** done on replay

- No change to the SotW request state or to `DeltaSubscriptionState` resource versions.
  The mux's view of what the server has sent is already correct; we are only catching up a
  watch that joined late.
- No `xds_config_tracker_->onConfigAccepted()` call. The tracker already saw the resource
  when it arrived.
- No `config_validators_->executeValidators()`. Validators ran at delivery time and the
  resource is unchanged.
- No TTL registration. The `TtlManager` is keyed by name, not by watch, so the existing
  timer covers the new watch as well.
- No interaction with `pause()`/`resume()`. Replay reads only the local cache.

### Interaction with `initial_fetch_timeout`

`GrpcSubscriptionImpl::start()` arms the timer and then calls `addWatch()`, so the replay task
is always posted after the timer is armed. The replay goes through
`GrpcSubscriptionImpl::onConfigUpdate()` which calls `disableInitFetchTimeoutTimer()`, so a
replayed resource cancels the timeout exactly like a real response. A zero timeout (wait
forever) now works for a late watch, which is the SDS case in #47309.

### Why async

`addWatch()` is called from `GrpcSubscriptionImpl::start()`, which for SDS runs inside
`SdsApi::initialize()` inside an init-manager callback, and for EDS runs inside
`ClusterImplBase::startPreInit()`. Delivering synchronously would re-enter the subscriber
before `watch_` is assigned and would mark init targets ready in the middle of the init
manager's own loop. Every existing subscriber is written on the assumption that
`onConfigUpdate()` arrives on a later dispatcher iteration, so a posted task is the only
delivery that needs no subscriber changes. The watch must be re-validated when the task runs
because it may have been removed in between.

### Memory

One copy of each explicitly named, currently watched resource per mux, held for as long as
any watch exists. For EDS this is what `EdsResourcesCache` already costs (minus the expiry
window). For RDS, SDS, VHDS and SRDS it is new, but each of these resources is already held
by its subscriber, so the increase is at most 2x for those types and zero for wildcard LDS
and CDS. Large deployments with thousands of named EDS resources are the worst case and are
already paying it when the EDS cache flag is on.

### Runtime guard

`envoy.reloadable_features.xds_replay_cached_resources`, default true. When false,
`WatchMap` still maintains the cache (cheap, and keeps the flag reloadable) but
`findAdditions()` does not queue replays.

## Rollout

1. Land the `WatchMap` cache plus replay behind the guard. Unit tests in
   `test/extensions/config_subscription/grpc/watch_map_test.cc`:
   late watch on cached name receives one `onConfigUpdate` on the next dispatcher run; watch
   removed before the task runs receives nothing; name whose last watch left is not cached;
   wildcard watch never populates the cache; delta removal drops the cache entry.
2. Integration test: the ADS test added in #48183 (`ClusterSdsConfigChangeReusesSecret`)
   with the SDS seeding guard off and delta or `unified_mux`, which must then pass on the mux
   cache alone. Add an EDS
   equivalent that updates a cluster with unchanged EDS config and asserts the cluster leaves
   warming without a second EDS response.
3. Both of the following wait until `unified_mux` is the default, since the legacy SotW mux
   has no replay and still needs `EdsResourcesCache` and the SDS seeding. Migrate EDS: `EdsClusterImpl` stops consulting `EdsResourcesCache` on fetch timeout once
   the replay guard is on; remove `EdsResourcesCache`, its runtime flag, and the
   `eds_resources_cache_` plumbing from `GrpcMuxContext` and both muxes. The `endpoint_stale_
   after` TTL handling in the cache is already duplicated by `TtlManager` and is not lost.
4. Simplify SDS: with the mux replaying, `SdsApi::seedFrom` and `SecretManagerImpl::
   seedFromExistingProvider` from #48183 become unnecessary for the ADS and same-source
   case. They can be kept for one release and then removed with their guard.

Steps 1 and 2 are one PR. Steps 3 and 4 are separate follow-ups so each can be reverted alone.

## Not covered

- A name whose last watch is removed and then re-added within the same response batch. The
  delta `WatchMap` already handles this with `deferred_removed_during_update_`; the cache
  entry is dropped and the name is re-requested, which is the existing behaviour.
- Subscribers that want *not* to reuse a cached resource, which was the motivation for
  making the EDS cache opt-in per subscription (#5168, non-TLS to TLS cluster change). With
  replay, the new watch sees the same bytes the server last sent, and the subscriber applies
  them to its new configuration, which is what would happen if the server resent the
  resource. The #5168 concern was about reusing already-built host objects, not about the
  resource bytes, and does not apply here.
- Multiple muxes. The cache is per mux, so two config sources never share resources, which
  matches the sharing rule in #48183.

## References

- #47309, #48183: SDS provider recreated on `sds_config` change never receives the secret.
- #5168, #28233 (EDS cache, Envoy 1.28): the EDS-only precedent and its opt-in flag.
- #48358: arena-scoped resource decoding, which is why the cache must copy.
- `envoy/config/eds_resources_cache.h`: the interface this proposal generalises and retires.
