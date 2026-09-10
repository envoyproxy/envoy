Added a :ref:`filter_state
<envoy_v3_api_field_extensions.filters.network.http_connection_manager.v3.ScopedRoutes.ScopeKeyBuilder.FragmentBuilder.filter_state>`
scope key fragment type, allowing a scoped routes (SRDS) scope key to be built from a filter state
object rather than only from a request header. Objects at any life span reachable from the stream are
visible, including ``Connection`` life span objects written by a network filter. Filter state is not
available on the on-demand scoped route discovery path, so a scope key using this fragment will not
trigger an on-demand SRDS update.
