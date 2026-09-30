Added ``envoy_dynamic_module_callback_get_runtime_bool``,
``envoy_dynamic_module_callback_get_runtime_int`` and
``envoy_dynamic_module_callback_get_runtime_number`` so that dynamic modules can read boolean,
integer and double values from the :ref:`runtime <config_runtime>`. Each takes the key and the
value to fall back to when the key is absent or holds a value of another type. They are exposed in
the Rust SDK as ``get_runtime_bool``, ``get_runtime_int`` and ``get_runtime_number``, and in the Go
and C++ SDKs as ``GetRuntimeBool`` / ``getRuntimeBool`` and friends on the new ``CommonHandle``
interface, which every config handle inherits.
