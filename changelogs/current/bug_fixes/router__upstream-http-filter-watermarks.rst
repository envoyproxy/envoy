Fixed watermark propagation in ``UpstreamFilterManager`` so upstream HTTP filters subscribed to
``UpstreamWatermarkCallbacks`` or ``DownstreamWatermarkCallbacks`` receive request- and
response-direction backpressure, and moved upstream ``readDisable()`` and flow-control stats
ownership exclusively to ``UpstreamCodecFilter``.
