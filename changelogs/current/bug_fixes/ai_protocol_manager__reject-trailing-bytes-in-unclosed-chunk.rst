Fixed the streaming JSON parser used by ``ai_protocol_manager`` silently ignoring bytes that followed
the root JSON value when they arrived in a non-final body chunk. A request such as
``{"valid":1} {"smuggled":"data"}`` split across data frames was accepted after parsing only the first
document, while the raw body (including the second document) was still forwarded upstream. Any
non-whitespace byte after the root value is now rejected as soon as it is seen.
