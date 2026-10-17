The Istio peer metadata conversion now takes a peer's app name and version from the ``app`` and
``version`` labels again, and uses ``app.kubernetes.io/name`` and ``app.kubernetes.io/version``
only when those labels are absent or empty. A workload's own stats always use ``app`` and
``version``, so giving the ``app.kubernetes.io/*`` labels precedence made ``source_app``,
``destination_app``, ``source_version`` and ``destination_version`` reported by a peer differ from
the values the workload reports for itself, and the same workload showed up under two names. This
restores the label precedence Istio proxies used before ``app.kubernetes.io/*`` labels were
preferred.
