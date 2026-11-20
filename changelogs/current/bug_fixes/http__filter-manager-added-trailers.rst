Fixed filter manager handling of trailers held by a filter, either added by it
(``addDecodedTrailers()`` / ``addEncodedTrailers()``) or received while it stopped all iteration.
Added response trailers skipped a later filter that stopped iteration on the data, which could
hold the whole response when that filter waits for the end of the stream, and added request
trailers could be decoded twice by the later filters when a filter stopped on headers continued on
the data. Trailers are now forwarded exactly once from the filter holding them, once it iterates,
and reach every later filter. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_forward_held_trailers`` to ``false``.
