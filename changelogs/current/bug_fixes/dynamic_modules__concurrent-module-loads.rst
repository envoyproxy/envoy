Fixed concurrent loads of dynamic modules. Envoy could use a module before another thread finished
calling its ``envoy_dynamic_module_on_program_init``, and two threads could call it at the same
time. This could happen with the upstream HTTP TCP bridge
(``envoy.upstreams.http.dynamic_modules``), which loads modules on worker threads. Other loads of a
module now wait until its ``envoy_dynamic_module_on_program_init`` returns, while the loads of
other modules do not wait. A module that stayed loaded after its
``envoy_dynamic_module_on_program_init`` failed, for example with ``do_not_close``, is now
initialized again on its next load instead of being used without it. This behavioral change can be
temporarily reverted by setting runtime guard
``envoy.reloadable_features.dynamic_modules_rerun_failed_program_init`` to ``false``.
