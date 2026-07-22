Added :ref:`default_resources <envoy_v3_api_field_config.route.v3.Vhds.default_resources>` to the VHDS
configuration, allowing a route configuration to subscribe to a default set of virtual host resources
(domains) at startup, before any on-demand update. Each entry is either ``*`` (subscribe to all
virtual hosts of the owning route configuration) or a specific domain, and is prefixed with the route
configuration name (``<route_config_name>/<value>``) before being sent to the management server. When
empty, the existing behavior is preserved and all virtual hosts are fetched on demand.
