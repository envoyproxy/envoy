:ref:`WatchedDirectory <envoy_v3_api_msg_config.core.v3.WatchedDirectory>` can now subscribe to
``Modified`` (``IN_MODIFY``) inotify events in addition to ``MovedTo`` (``IN_MOVED_TO``).
Previously, only atomic file renames (e.g. Kubernetes ConfigMap updates) triggered reload callbacks.
In-place file writes are now also detected, enabling secret managers that write certificate files
directly (rather than via atomic rename) to trigger SDS certificate rotation. This behavior is
disabled by default and can be enabled by setting runtime guard
``envoy.reloadable_features.watched_directory_modified_events`` to ``true``.
