Added :ref:`package_paths <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.package_paths>`
and :ref:`package_cpaths
<envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.package_cpaths>`, and the same two fields on
:ref:`LuaPerRoute <envoy_v3_api_msg_extensions.filters.http.lua.v3.LuaPerRoute>`, which prepend
module search patterns to a Lua VM's ``package.path`` and ``package.cpath``. This lets a script
``require`` modules from a configured location instead of only from the paths the interpreter
searches by default. The patterns are applied before any configured code runs, including the run
that validates the configuration, so a ``require`` at the top level of a script is resolved at
config load time.
