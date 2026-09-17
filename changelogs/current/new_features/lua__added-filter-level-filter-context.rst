Added :ref:`filter_context <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.filter_context>`
to the Lua filter's own configuration, so parameters shared by every route the filter serves no
longer have to be repeated in each route's :ref:`LuaPerRoute
<envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>`. ``handle:filterContext()`` returns
the route's context when the route configures one and this one otherwise; a route's context
replaces rather than merges into it.
