Added :ref:`shared_vm_id <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.shared_vm_id>` and
:ref:`LuaPerRoute.shared_vm_id
<envoy_v3_api_field_extensions.filters.http.lua.v3.LuaPerRoute.shared_vm_id>` to the Lua filter.
Configurations that set the same id share one set of Lua VMs for every script whose contents and
package search paths match, instead of each building its own. This is off by default: with the
field unset the filter keeps building one set of VMs per configured script.
