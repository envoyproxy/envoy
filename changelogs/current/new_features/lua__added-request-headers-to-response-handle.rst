Added ``requestHeaders()`` to the Lua HTTP filter's response handle (``envoy_on_response``),
allowing scripts to read the request headers during response processing. Returns ``nil`` if the
request headers are not available.
