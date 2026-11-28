The ``/stats/prometheus`` and ``/stats?format=prometheus`` admin endpoints now render the stats in
chunks of whole metric families instead of all at once. With the runtime guard
``envoy.reloadable_features.admin_filter_yield_between_chunks`` enabled, a scrape with many stats
no longer blocks the main thread for the whole render. The response content is unchanged.
