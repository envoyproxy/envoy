Added :ref:`filter_state
<envoy_v3_api_msg_extensions.internal_redirect.filter_state.v3.FilterStateConfig>`,
a new internal redirect predicate that gates redirect decisions on a boolean filter-state
object set earlier in the request lifecycle (for example by a Lua filter, ext_proc,
``set_filter_state``, or a dynamic module). The predicate follows the redirect when the
boolean value is true (or false if ``invert: true``), enabling per-request redirect
control without changing route matching. Also added the generic ``envoy.bool`` filter
state factory for creating boolean filter state objects.
