Fixed ``envoy.filters.http.on_demand`` to refresh a stale route configuration snapshot in
``decodeHeaders`` when no route is cached and to return ``StopIteration`` from
``decodeTrailers`` when decoding is paused waiting for on-demand discovery.
