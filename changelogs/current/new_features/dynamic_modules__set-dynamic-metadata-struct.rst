Added ``set_dynamic_metadata_struct`` and ``set_dynamic_typed_metadata`` to dynamic module network
filters, listener filters and cluster load balancers. A module can merge a serialized
``google.protobuf.Struct`` into a dynamic metadata namespace or set the typed metadata of a
namespace from a serialized ``google.protobuf.Any``. Unlike the HTTP filter callbacks of the same
names, they return whether the call succeeded, and the struct setter does not create the namespace
for an empty struct. The typed setter also replaces an existing entry instead of merging into it
and rejects an ``Any`` without a type URL. The Rust SDK exposes them on ``EnvoyNetworkFilter``,
``EnvoyListenerFilter`` and ``ClusterLbContext``. The C++ SDK exposes them as ``setMetadataStruct``
and ``setTypedMetadata`` on ``NetworkFilterHandle`` and as ``setDynamicMetadataStruct`` and
``setDynamicTypedMetadata`` on ``ListenerFilterHandle``. The Go SDK exposes them as
``SetMetadataStruct`` and ``SetTypedMetadata`` on ``NetworkFilterHandle`` and as
``SetDynamicMetadataStruct`` and ``SetDynamicTypedMetadata`` on ``ListenerFilterHandle``.
