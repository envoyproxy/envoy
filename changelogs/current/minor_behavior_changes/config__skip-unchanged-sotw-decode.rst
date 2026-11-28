State-of-the-world xDS subscriptions (the filesystem subscription and the gRPC multiplexers) now reuse the
decoded form of a resource whose serialized bytes are identical to the previous update instead of unpacking
and validating it again. This lowers main thread time for control planes that re-send every resource on each
update. It can be disabled by setting the runtime guard
``envoy.reloadable_features.xds_skip_unchanged_resource_decode`` to ``false``. Delta xDS is unaffected.
