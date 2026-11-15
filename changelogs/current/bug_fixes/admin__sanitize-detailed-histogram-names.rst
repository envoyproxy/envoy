Sanitize histogram names rendered as JSON into the inline scripts of the admin
``/stats?format=html&histogram_buckets=detailed`` page, so that a stat name cannot close the script
element and inject markup. This change is guarded by the runtime guard
``envoy.reloadable_features.sanitize_html_stats_names``.
