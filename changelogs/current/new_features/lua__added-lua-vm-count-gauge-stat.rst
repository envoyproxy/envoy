Added a process-wide ``lua.lua_vm_count`` gauge stat, documented under the :ref:`Lua HTTP filter
<config_http_filters_lua_stats>`, that tracks the number of active Lua VMs across every
filter-config-level and route-level Lua script.
