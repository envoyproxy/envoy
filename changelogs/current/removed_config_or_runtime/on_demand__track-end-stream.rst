Removed the runtime guard ``envoy.reloadable_features.on_demand_track_end_stream`` and the legacy
code path it guarded. The on-demand filter now always tracks the downstream ``end_stream`` state to
decide whether a stream with a fully read body can be recreated, instead of rejecting all requests
that carry a body.
