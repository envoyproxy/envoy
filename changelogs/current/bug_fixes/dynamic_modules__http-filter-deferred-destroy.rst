Fixed a use-after-free crash in the dynamic modules HTTP filter. An event hook that ends the
stream, for example ``envoy_dynamic_module_callback_http_filter_recreate_stream``, tears the filter
chain down on the module's own stack, which freed the in-module filter the hook was still running
on. The in-module filter is now destroyed from the dispatcher's deferred deletion list, so
``envoy_dynamic_module_on_http_filter_destroy`` runs once every other event hook has returned. The
callbacks that need the torn-down stream no longer dereference it, and HTTP callouts started after
the teardown are refused instead of outliving the filter.
