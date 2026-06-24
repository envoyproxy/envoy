Added :ref:`watch_modify <envoy_v3_api_field_config.core.v3.WatchedDirectory.watch_modify>` field
to :ref:`WatchedDirectory <envoy_v3_api_msg_config.core.v3.WatchedDirectory>`. When set to ``true``,
the watcher subscribes to ``Modified`` (``IN_MODIFY``) inotify events in addition to ``MovedTo``
(``IN_MOVED_TO``). This allows in-place file writes to trigger reload callbacks, enabling secret
managers that write certificate files directly (rather than via atomic rename) to trigger SDS
certificate rotation. By default, only move/rename events are watched.
