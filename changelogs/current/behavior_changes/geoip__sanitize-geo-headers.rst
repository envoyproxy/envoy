The :ref:`HTTP geoip filter <config_http_filters_geoip>` now removes every configured geolocation header
from the incoming request before the lookup, so a header a client sent is never forwarded upstream as if the
lookup had set it. Previously a client-supplied header was kept whenever the lookup had no value for it: an
address absent from the database, a database error, a field the database lacks for that address, or a request
whose lookup was skipped because the downstream address was not an IP address. Geolocation providers report the
headers they are configured to write through the new ``Driver::geoHeaderNames()`` method, which defaults to
empty for providers that do not implement it. This behavior can be reverted by setting the runtime guard
``envoy.reloadable_features.geoip_sanitize_geo_headers`` to ``false``.
