Added a :ref:`route specifier
<envoy_v3_api_msg_extensions.router.route_specifiers.dynamic_modules.v3.DynamicModuleRouteSpecifier>`
backed by a dynamic module. For each request the module keeps the route that route matching
resolved, refines it, replaces it with one of the route templates it declares, drops it, or lets
route matching carry on with the next route. It can be validated against the route table it replaces
with shadow mode, which never changes the routing of a request.
