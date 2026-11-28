Fixed ``membership_total`` and ``membership_healthy`` (and the other cluster membership gauges)
reporting the previous cluster's values after a cluster update. A replacement cluster that went
active with the host set it started with, for example a DNS cluster whose first lookup failed and
that came up with no hosts, kept showing the old cluster's host counts.
