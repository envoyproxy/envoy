.. _control_plane:

How do I support multiple xDS API major versions in my control plane?
=====================================================================

Where possible, it is highly recommended that control planes support a single major version at a
given point in time for simplicity. This works in situations where control planes need to only
support a window of Envoy versions which spans less than a year. Temporary support for multiple
versions during rollout in this scenario is described :ref:`here <control_plane_version_support>`.

For control planes that need to support a wider range of versions, there are a few approaches:

1. Independent vN/v(N+1) configuration generation pipelines. This is simple to understand but
   involves significant duplication of code and can be expensive engineering wise. This may work
   well if the API surface in use is small.
2. Have the control plane use vN canonically and mechanically transform vN messages to their v(N+1)
   equivalents. This does not allow for the use of any new v(N+1) features. It is necessary to avoid
   the use of any deprecated vN fields. With these caveats aside, a simple transformation is
   possible where the vN proto message is serialized and then deserialized as a v(N+1) proto message
   (this binary compatibility is guaranteed). An optimization when serving *google.protobuf.Any*
   resources in a *DiscoveryResponse* is to simply rewrite the type URL.
3. Have the control plane use v(N+1) canonically and mechanically transform v(N+1) messages to their
   vN equivalents when serving vN-only Envoys. This works provided it is safe to ignore v(N+1)-only
   fields from the perspective of the operator's intent when providing input to the config pipeline
   (e.g. if a new regex type is requested and silently ignored in a *RouteMatch* for vN Envoys, this
   is problematic). Similar to (2), the v(N+1) message may be transformed to a vN equivalent by a
   serialization round-trip. If the goal is to support the widest range of vN clients, it's
   necessary to transform, using hand-written code, fields that are present in both vN/v(N+1) to
   their vN deprecated counterparts, since some earlier vN Envoy clients will not have the newer
   fields common to vN and v(N+1).
