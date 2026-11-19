The tag-extracted names of per-worker stats now keep ``worker`` as a separate name segment when the
``worker_id`` tag is extracted: ``server.worker_1.watchdog_miss`` is now extracted to
``server.worker.watchdog_miss`` instead of ``server.worker_watchdog_miss``, and the same applies to
the per-worker stats under ``listener.<address>.``, ``listener_manager.`` and
``thread_local_cluster_manager.``. Prometheus names are unchanged, because ``.`` and ``_`` are both
sanitized to ``_`` there. Metric names change only for sinks that emit the tag-extracted name
verbatim, such as ``dog_statsd``, ``metrics_service`` and the OpenTelemetry sink.
