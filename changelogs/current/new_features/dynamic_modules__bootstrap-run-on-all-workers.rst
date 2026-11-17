Added ``envoy_dynamic_module_callback_bootstrap_extension_run_on_all_workers`` to the dynamic module
bootstrap extension. It runs a module hook on every worker thread and then a done hook on the main
thread. See :ref:`Running Code on Workers
<config_bootstrap_extensions_dynamic_modules_run_on_all_workers>`. The documentation of
``envoy_dynamic_module_on_bootstrap_extension_worker_thread_initialized`` now says that it runs once
on the main thread, so per-worker setup belongs in this new callback.
