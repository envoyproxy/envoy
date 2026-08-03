Removed the runtime guard ``envoy.reloadable_features.use_response_decoder_handle`` and the legacy
code path it guarded. Upstream connection pools and the HTTP/3 client stream now always create and
use a ``ResponseDecoderHandle`` to safely access the response decoder.
