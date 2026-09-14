The dynamic modules SDKs can now keep a tracing span past the event hook that created it. The Rust
SDK ``get_active_span`` and ``spawn_child_span`` return owned spans, so a module can store a child
span in the filter and finish it from a later event hook such as ``on_scheduled``, which lets a
span cover off-thread work. The C++ and Go SDKs already supported this and are now documented to
guarantee it.
