Fixed a vulnerability where HTTP/2 PRIORITY and WINDOW_UPDATE frame flood protection could be bypassed
by rapidly opening and closing streams. The protection now scales with the number of active streams
rather than cumulative opened streams, and frame usage is retired upon stream closure. This behavioral
change can be toggled on or off via the guard
``envoy.reloadable_features.http2_flood_protection_active_streams``.
