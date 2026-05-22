Fixed Zipkin ``timestamp_trace_ids`` to encode Unix epoch seconds in the high 32 bits of generated
trace IDs, matching the documented ``[32-bit epoch seconds][32-bit random]`` format. Previously
Envoy used monotonic clock seconds, so the trace ID prefix did not correspond to wall-clock time.
