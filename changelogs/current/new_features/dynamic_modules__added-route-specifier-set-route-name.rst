Added a route name setter to the dynamic modules route specifier. A module can now record the name
of the route it produces through ``set_route_name`` on the route specifier context, so that a module
built route carries an identity of its own for the ``%ROUTE_NAME%`` access log command operator and
other route name consumers. The Rust SDK exposes this as ``RouteSpecifierContext::set_route_name``.
