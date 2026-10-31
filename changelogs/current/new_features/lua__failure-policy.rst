Added :ref:`failure_policy <envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.failure_policy>`
to the Lua filter. Setting it to ``FAIL_CLOSED`` fails a stream whose script raises an uncaught
error with a ``503`` local reply, instead of continuing the filter chain. The default,
``FAIL_OPEN``, keeps the existing behavior.
