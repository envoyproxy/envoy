Envoy builds using ``gperftools`` tcmalloc now honor
:ref:`memory_allocator_manager.bytes_to_release
<envoy_v3_api_field_config.bootstrap.v3.MemoryAllocatorManager.bytes_to_release>`, releasing the
configured number of bytes every ``memory_release_interval``. Previously the setting was ignored. A
``memory_release_interval`` of zero, or shorter than one millisecond, now disables background
release for both Google's tcmalloc and ``gperftools`` tcmalloc. Builds using neither allocator now
log a warning when ``bytes_to_release`` is non-zero.
