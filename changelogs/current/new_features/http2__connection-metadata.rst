Added HTTP/2 connection-level metadata support, allowing metadata to be associated with an
entire connection (HTTP/2 stream ID 0) rather than only individual streams. Connection metadata
is gated by the existing ``http2_protocol_options.allow_metadata`` flag: when enabled, the codec delivers received connection
metadata through ``ConnectionCallbacks::onMetadata`` and exposes
``Connection::encodeMetadata`` for sending a connection-level METADATA frame. Per-stream
metadata behavior is unchanged. This change is HTTP/2 only; HTTP/3 connection metadata is not
yet supported.
