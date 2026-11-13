Fixed filter manager handling of trailers added by a filter (``addDecodedTrailers()`` /
``addEncodedTrailers()``): added response trailers were never forwarded past a later filter that
stopped iteration on the data, which could hold the whole response when that filter waits for the
end of the stream, and added request trailers could be decoded twice by the later filters when a
filter stopped on headers continued on the data. Trailers are now forwarded once from the filter
holding them, once it iterates. This change can be reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_forward_held_trailers`` to ``false``.
