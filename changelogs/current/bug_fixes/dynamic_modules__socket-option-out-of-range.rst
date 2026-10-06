dynamic modules: fixed the socket option callbacks so a ``level``, ``name``, or ``value`` that does
not fit in an ``int`` is rejected instead of truncated and applied as a different option. This
affects the HTTP, network, and listener filter socket option callbacks.
