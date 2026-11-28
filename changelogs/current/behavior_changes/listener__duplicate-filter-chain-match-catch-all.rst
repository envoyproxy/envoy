Listener filter chain matching now treats a ``source_prefix_ranges`` entry with ``prefix_len`` 0
(e.g. ``0.0.0.0/0``) as equivalent to having no source range constraint when checking for duplicate
filter chains. A listener that combines a filter chain with no match and another whose only
constraint is a catch-all source range is now rejected at load time as a duplicate, instead of
being accepted and matching connections non-deterministically.
