Added :ref:`filter_state
<envoy_v3_api_msg_extensions.internal_redirect.filter_state.v3.FilterStateConfig>`,
a new internal redirect predicate that gates redirect decisions on a filter-state string
object set earlier in the request lifecycle (for example by a Lua filter, ext_proc,
``set_filter_state``, or a dynamic module). The predicate can either follow the redirect only
when the value matches (``allow_value``) or follow it unless the value matches (``deny_value``),
enabling per-request redirect control without changing route matching.
