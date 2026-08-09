Fixed a bug where a :ref:`VHDS <envoy_v3_api_field_config.route.v3.RouteConfiguration.vhds>`
subscription configured in an inline ``route_config`` was never started when its listener arrived
over LDS after the server had finished initializing.
