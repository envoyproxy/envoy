Fixed the streaming JSON parser used by ``ai_protocol_manager`` not latching its error state. After
``feed()`` returned an error raised part-way through a chunk (handler error, nesting limit,
duplicate key, malformed input), the next call resumed with stale token offsets and could index past
the end of the new chunk, aborting the process. The first error is now sticky: every later call
returns the same status without re-entering the decoder.
