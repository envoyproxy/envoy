Fixed the Go HTTP filter forwarding buffered body data with ``end_stream`` set when the filter
resumed processing the trailers, for example after an asynchronous ``StopAndBuffer`` returned once
the trailers had already arrived. The trailers were then sent after the stream had ended, which
asserted in debug builds.
