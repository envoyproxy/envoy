Fixed a bug where the ``ext_proc`` filter sent the request on to the rest of the filter chain,
and so upstream, after it had started a streamed local response (``StreamedImmediateResponse``):
the request headers, for a request whose body was still being received in ``STREAMED`` or
``FULL_DUPLEX_STREAMED`` body mode, and the request once its trailers arrived, if the processor had
closed its stream by then. A processor that closes its stream before it has finished a local
response now has the request failed: the stream is reset if the response headers have been sent
downstream, and an error response is sent in their place if an encoder filter is still holding
them.
