Added ``downstreamRequestHeaders()`` to the Lua HTTP filter's response handle
(``envoy_on_response``), allowing scripts to inspect the original downstream request headers
during response processing. The accessor is read-only and returns ``nil`` if request headers
are unavailable.
