Fixed a bug where the ``ext_proc`` filter sent the request on to the rest of the filter chain,
and so upstream, after it had started a streamed local response (``StreamedImmediateResponse``):
the request headers, for a request whose body was still being received in ``STREAMED`` or
``FULL_DUPLEX_STREAMED`` body mode, and the request once its trailers arrived, if the processor had
closed its stream by then. A processor that does not finish a local response it started (it closes
its stream, or sends a spurious or error message) now has the request failed: the stream is reset
if the response headers have been sent downstream, and an error response is sent in their place if
an encoder filter is still holding them. This failing behavior can be reverted by setting the
runtime guard ``envoy.reloadable_features.ext_proc_fail_incomplete_local_response`` to ``false``
(the two fixes that stop the request being sent on are not guarded).
