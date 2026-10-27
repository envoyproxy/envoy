Added :ref:`track_execution_time
<envoy_v3_api_field_extensions.filters.http.lua.v3.Lua.track_execution_time>` to the Lua filter,
which records a ``stream_execution_time`` histogram of how long a stream's scripts ran on the
worker thread, summed over both handlers and every resume and excluding the time a script spends
yielded on an ``httpCall()``, a body or trailers. It is off by default.
