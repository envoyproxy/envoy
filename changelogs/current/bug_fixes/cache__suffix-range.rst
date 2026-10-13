Fixed an assertion failure in the cache_v2 filter caused by ``sendHeaders()`` calling
``firstBytePos()`` for suffix range requests such as ``Range: bytes=-2``.
