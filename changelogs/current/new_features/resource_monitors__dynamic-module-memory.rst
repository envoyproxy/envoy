Added the :ref:`dynamic module memory resource monitor
<envoy_v3_api_msg_extensions.resource_monitors.dynamic_module_memory.v3.DynamicModuleMemoryConfig>`,
which reports the heap memory allocated by a dynamic module as a fraction of a configured heap size.
The memory is estimated from the tcmalloc heap profile.
