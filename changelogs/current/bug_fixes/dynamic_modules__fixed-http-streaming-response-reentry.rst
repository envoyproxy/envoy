Fixed a bug in the HTTP dynamic module filter where the streaming-response callbacks
``send_response_headers``, ``send_response_data`` and ``send_response_trailers`` re-entered the
module's own encode hooks for the response it was producing. The encode hooks are now suppressed
for these responses, matching the behavior of ``send_response``.
