Added ``invert_filter`` query parameter to the ``/stats`` and ``/stats/prometheus`` admin
endpoints. When set, the ``filter`` regex is inverted so matching stats are excluded from the
output (e.g. ``/stats?filter=server&invert_filter``).
