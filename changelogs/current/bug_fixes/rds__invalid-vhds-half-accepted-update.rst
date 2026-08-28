Fixed a bug where an RDS update carrying an invalid
:ref:`VHDS <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>` configuration was applied
only halfway. The new route configuration was recorded before the VHDS subscription it configures
was created, so when creating that subscription failed the update was rejected after the recorded
state had already moved on: the admin ``/config_dump`` endpoint reported the rejected route
configuration while the workers kept serving the previous one.
