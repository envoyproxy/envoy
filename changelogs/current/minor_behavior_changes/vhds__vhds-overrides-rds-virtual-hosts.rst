When a virtual host discovered through :ref:`VHDS <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>`
has the same :ref:`name <envoy_v3_api_field_config.route.v3.VirtualHost.name>` as a virtual host
defined in the RDS :ref:`virtual_hosts <envoy_v3_api_field_config.route.v3.RouteConfiguration.virtual_hosts>`,
the VHDS virtual host now replaces the RDS one in the merged route configuration. Previously both
virtual hosts were kept. Removing the VHDS virtual host restores the RDS one. This behavior change
can be reverted by setting the runtime guard
``envoy.reloadable_features.vhds_overrides_rds_virtual_hosts_with_same_name`` to ``false``.
