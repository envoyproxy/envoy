Added the :ref:`dynamic module memory resource monitor
<envoy_v3_api_msg_extensions.resource_monitors.dynamic_module_memory.v3.DynamicModuleMemoryConfig>`,
which reports the heap memory allocated by a dynamic module as a fraction of a configured heap size.
The memory is estimated from the tcmalloc heap profile, unless the module reports it through the new
optional ``envoy_dynamic_module_on_program_get_memory_usage`` event hook, which the Rust SDK
implements with ``declare_tracking_allocator!``.
