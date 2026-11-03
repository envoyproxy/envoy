Fixed oghttp2 (``envoy.reloadable_features.http2_use_oghttp2``) sending outbound DATA frames as large as the
peer's ``SETTINGS_MAX_FRAME_SIZE``, which caused legitimate flow-control ``WINDOW_UPDATE`` traffic from clients
that acknowledge data in small increments to be misclassified as a flood
(``http2.inbound_window_update_frames_flood``) and the connection to be closed. Outbound DATA frame payloads are
now capped at 16 KiB, matching nghttp2. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.http2_oghttp2_cap_data_frame_payload`` to false.
