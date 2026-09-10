Added :ref:`shadow_mode
<envoy_v3_api_field_extensions.filters.network.ext_authz.v3.ExtAuthz.shadow_mode>` to the network
ext_authz filter. When enabled, the filter calls the authorization service as normal but never
closes the connection. The authorization decision is written to FilterState as a
:ref:`ShadowDecision <envoy_v3_api_msg_extensions.filters.network.ext_authz.v3.ShadowDecision>`
under ``envoy.filters.network.ext_authz.<stat_prefix>.shadow`` so that a subsequent filter can read
and optionally enforce it.
