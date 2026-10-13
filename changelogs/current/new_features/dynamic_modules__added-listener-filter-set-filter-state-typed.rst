Added the ``envoy_dynamic_module_callback_listener_filter_set_filter_state_typed`` and
``envoy_dynamic_module_callback_listener_filter_get_filter_state_typed`` ABI callbacks so a
dynamic-module listener filter can write and read typed filter state, mirroring the existing bytes
setter/getter. Unlike the bytes variant which stores a raw ``Router::StringAccessor``, the typed
setter uses the key's registered ``ObjectFactory`` to build a properly typed filter state object,
so a built-in Envoy filter that reads the key as a typed object can consume it. The Rust SDK
exposes these as ``EnvoyListenerFilter::set_filter_state_typed`` and
``EnvoyListenerFilter::get_filter_state_typed``.
