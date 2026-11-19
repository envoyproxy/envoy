The admin server now returns to the event loop between the chunks of a large response, such as
``/stats``, so a big response no longer blocks other work on the main thread, like health checks.
This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.admin_filter_yield_between_chunks`` to ``false``.
