Fixed the cache v2 filter inserting responses into the cache when the request contains
``Cache-Control: no-store``. The ``ignore_request_cache_control_header`` option continues to
allow these responses to be cached when enabled.
