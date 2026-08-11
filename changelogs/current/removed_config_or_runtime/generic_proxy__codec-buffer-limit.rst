Removed the runtime guard ``envoy.reloadable_features.generic_proxy_codec_buffer_limit`` and the
legacy code path it guarded. The generic proxy Dubbo, HTTP/1 and Kafka codecs now always fail
decoding when the buffered data exceeds the connection buffer limit.
