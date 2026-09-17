Removed the runtime guard ``envoy.reloadable_features.safe_http2_options`` and the legacy code path
it guarded. HTTP/2 connections now always fall back to the safe defaults (max concurrent streams of
1024, 16 MiB initial stream window and 24 MiB initial connection window) when the corresponding
options are unset, and the unused legacy default constants are removed.
