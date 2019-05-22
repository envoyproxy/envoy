tldr: xDS can be filesystem, REST, or gRPC, and gRPC xDS comes in four flavors,
but Envoy code uses all of that via the same Subscription interface. If you are
an Envoy developer with your hands on a valid Subscription object, you can
forget the filesystem/REST/gRPC distinction, and you can especially forget about
the gRPC flavors. All of that is specified in the bootstrap config, which is
read and put into action by ClusterManagerImpl.

If you are working on Envoy's gRPC xDS client logic itself, read on.

When using gRPC, xDS has two pairs of options: aggregated/non-aggregated, and
delta/state-of-the-world updates. All four combinations of these are usable.

"Aggregated" means that EDS, CDS, etc resources are all carried by the same gRPC stream.
For Envoy's implementation of xDS client logic, there is effectively no difference
between aggregated xDS and non-aggregated: they both use the same request/response protos. The
non-aggregated case is handled by running the aggregated logic, and just happening to only have 1
xDS subscription type to "aggregate", i.e., GrpcDeltaXdsContext only has one
DeltaSubscriptionState entry in its map.

However, to the config server, there is a huge difference: when using ADS (caused
by the user providing an ads_config in the bootstrap config), the gRPC client sets
its method string to {Delta,Stream}AggregatedResources, as opposed to {Delta,Stream}Clusters,
{Delta,Stream}Routes, etc. So, despite using the same request/response protos,
and having identical client code, they're actually different gRPC services.

Delta vs state-of-the-world is a question of wire format: the protos in question are named
[Delta]Discovery{Request,Response}. That is what the GrpcMux (TODO rename) interface is useful for: its
GrpcDeltaXdsContext implementation works with DeltaDiscovery{Request,Response} and has
delta-specific logic, and its GrpxMuxImpl implementation (TODO will be merged into GrpcDeltaXdsContext) works with Discovery{Request,Response}
and has SotW-specific logic. A GrpcSubscriptionImpl has its shared_ptr<GrpcMux>.
Both GrpcDeltaXdsContext (delta) or GrpcMuxImpl (SotW) will work just fine. The shared_ptr allows
for both non- and aggregated: if non-aggregated, you'll be the only holder of that shared_ptr. By
those two mechanisms, the single class (TODO rename) DeltaSubscriptionImpl handles all 4
delta/SotW and non-/aggregated combinations.

