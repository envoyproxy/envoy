Fixed a crash (SEGFAULT) in the ``grpc_http1_reverse_bridge`` filter when ``withhold_grpc_frames``
is enabled without ``response_size_header`` and the upstream response body exceeds the downstream
HTTP/2 stream flow control window. The filter now uses the upstream ``Content-Length`` header to
stream the response incrementally instead of buffering and releasing it all at once.
