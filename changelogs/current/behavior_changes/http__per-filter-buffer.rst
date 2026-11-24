Added the runtime guard ``envoy.reloadable_features.filter_manager_per_filter_buffer``, disabled by
default. When enabled, the filter manager buffers the body data stopped at each filter in that
filter's own buffer, instead of in a single buffer shared by the whole stream, so a filter only ever
replays the data it holds itself. ``decodingBuffer()``, ``addDecodedData()`` and
``modifyDecodingBuffer()`` (and their encoding counterparts) then operate on the calling filter's
own buffer: ``decodingBuffer()`` no longer returns body data buffered by other filters in the chain.
A filter returning ``StopIterationNoBuffer`` consumes the frame: whatever it leaves in a frame
replayed from an earlier filter's buffer is dropped. A body added by several filters while the
headers pass through the chain is still sent once, as a single body. The stream buffer limit applies
to the total held by all buffers in each direction, and the router drops the request body it kept
for retries or internal redirects once it gives up on them, so the kept body no longer counts
towards that limit. This fixes a filter resuming iteration ending the stream early, dropping the
remaining body, when another filter earlier in the chain was still holding data that it later
injected (for example, three ext_proc filters processing the body in ``STREAMED`` mode), and a
filter resuming iteration sending on body data still held by an earlier filter before that filter
had finished with it, or sending it twice to a filter that keeps it, such as the router with a retry
policy. When enabled, the runtime guards
``envoy.reloadable_features.ext_proc_inject_data_with_state_update`` and
``envoy.reloadable_features.filter_manager_forward_added_data_on_continue`` have no effect.
