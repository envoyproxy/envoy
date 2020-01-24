How do I support multiple xDS API major versions in my control plane?
=====================================================================

Where possible, it is highly recommended that control planes support a single major version at a
given point in time for simplicity. This works in situations where control planes need to only
support a window of Envoy versions which spans less than a year.

For control planes that need to support a wider range of versions, there are a few approaches:

1. Independent v2/v3 configuration generation pipelines. This is simple to understand but
   involves significant duplication of code and can be expensive engineering wise. This may
   work well if the API surface in use is small.
2. Have the control plane use v2 canonically and mechanically transform v2 messages to their
   v3 equivalents. This does not allow for the use of any new v3 features. It is necessary to
   avoid the use of any deprecated v2 fields. With these caveats aside, a simple transformation
   is possible where the v2 proto message is serialized and then deserialized as a v3 proto
   message (this binary compatibility is guaranteed). An optimization when serving
   *google.protobuf.Any* resources in a *DiscoveryResponse* is to simply rewrite the type
   URL.
3. Have the control plane use v3 canonically and mechanically transform v3 messages to their
   v2 equivalents when serving v2-only Envoys. This works providing it is safe to ignore
   v3-only fields from the perspective of the operator's intent when providing input to the
   config pipeline (e.g. if a new regex type is requested and silently ignored in a
   *RouteMatch* for v2 Envoys, this is problematic). Similar to (2), the v3 message may be
   transformed to a v2 equivalent by a serialization round-trip. If the goal is to support
   the widest range of v2 clients, it's necessary to transform, using hand-written code,
   fields that are present in both v2/v3 to their v2 deprecated counterparts, since some
   earlier v2 Envoy clients will not have the newer fields common to v2 and v3.
