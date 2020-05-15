.. _control_plane_version_support:

Which xDS transport and resource versions does my control plane need to support?
================================================================================

If a control plane is serving a well known set of clients at a given API major version, it only
needs to support that version (both transport and resource version). However, even in this
relatively basic scenario, if the set of clients straddles a major version drop or the control plane
wishes to move from v2/v3, there are considerations around rollout of client and server binaries.

One approach to this problem is to add temporary support to the management server for both v2 and v3
transport versions (see https://github.com/envoyproxy/go-control-plane). For resources, messages
are binary compatible modulo deprecated or new fields between API major versions. If the control
plane no longer emits resources with deprecated fields, this allows for a trivial replacement of
type URL based on the requested resource from the client to serve the same resource for v2 and v3. A
typical rollout sequence might look like:

1. Clients with a mix of v2 and v3 support are in operation, with a v2 management server. The
   client bootstraps will reference v2 API transport endpoints.

2. A management server with dual v2/v3 API support is rolled out. Both v2 and v3 transport endpoints
   are supported, while a trivial type URL replacement in the returned resource is sufficient for
   matching the requested v2 or v3 resource type URL with the existing v2 resource in the control
   plane. When returning resources with embedded `ConfigSource` messages pointing at xDS resources
   for a v3 request, it will be necessary to set the `transport_api_version` and
   `resource_api_version` to v3. No deprecated v2 fields or new v3 fields can be used at this point.

3. Client bootstraps are upgraded to v3 API transport endpoints and v3 API resource versions.

4. Support for v2 is removed in the management server. The management server moves to v3 exclusively
   internally and can support newer fields.

If you are operating a managed control plane as-a-service, you will likely need to support a wide
range of client versions. In this scenario, you will require long term support for multiple major
API transport and resource versions. Strategies for managing this support are described :ref:`here
<control_plane>`.
