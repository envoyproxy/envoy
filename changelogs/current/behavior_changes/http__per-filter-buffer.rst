Added the runtime guard ``envoy.reloadable_features.filter_manager_per_filter_buffer``, disabled by
default. When enabled, the filter manager buffers the body data stopped at each filter in that
filter's own buffer, instead of in a single buffer shared by the whole stream, so a filter only ever
replays the data it holds itself. ``decodingBuffer()``, ``addDecodedData()`` and
``modifyDecodingBuffer()`` (and their encoding counterparts) then operate on the calling filter's
own buffer: ``decodingBuffer()`` no longer returns body data buffered by other filters in the chain.
A body added by several filters while the headers of a header-only stream pass through the chain is
still sent once, as a single body. The stream buffer limit applies to the total held by all buffers
in each direction. When enabled, the router also drops the request body it kept for retries or
internal redirects once it gives up on them because the body exceeds the retry buffer limit, so the
kept body no longer counts towards the stream buffer limit.
