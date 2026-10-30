Added ``shared_with_upstream`` to the arguments of the ``set_envoy_filter_state`` Wasm foreign
function. It shares the filter state object with the upstream internal connections, with the same
``ONCE`` and ``TRANSITIVE`` options as the :ref:`set filter state
<envoy_v3_api_field_extensions.filters.common.set_filter_state.v3.FilterStateValue.shared_with_upstream>`
filters. The default ``NONE`` keeps the current behavior.
