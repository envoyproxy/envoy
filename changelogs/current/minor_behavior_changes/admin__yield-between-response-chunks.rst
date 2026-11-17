The admin server now returns to the event loop between the chunks of a large response, such as
``/stats``, so a big response no longer blocks other work on the main thread, like health checks.
``/stats/prometheus`` and ``/stats?format=prometheus`` are now also rendered in chunks of whole
metric families instead of all at once; the response content is unchanged. Yielding between chunks
can be reverted by setting the runtime guard
``envoy.reloadable_features.admin_filter_yield_between_chunks`` to ``false``.
