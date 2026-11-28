Fixed a bug where the ``ext_proc`` filter sent the request headers on to the rest of the filter
chain, and so the request upstream, after it had started a streamed local response
(``StreamedImmediateResponse``) for a request whose body was still being received in ``STREAMED``
or ``FULL_DUPLEX_STREAMED`` body mode.
