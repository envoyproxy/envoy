Fixed a bug where response metadata added by HTTP encoder filters could be dropped when a later
encoder filter sent a direct local reply before final response headers were encoded to the codec.
Saved response metadata is now flushed before the local reply ends the stream. This behavior can be
temporarily reverted by setting the runtime guard
``envoy.reloadable_features.direct_local_reply_flush_saved_response_metadata`` to ``false``.
