Added :ref:`RouteConfiguration.formatters
<envoy_v3_api_field_config.route.v3.RouteConfiguration.formatters>`, allowing extension
formatters (for example ``envoy.formatter.req_without_query``) to be used in
``request_headers_to_add`` and
``response_headers_to_add`` values at the route configuration, virtual host, route, and weighted
cluster levels. Previously only built-in command operators were available in header values, and
using an extension formatter was rejected at configuration load with
``Not supported field in StreamInfo: <COMMAND>``.
