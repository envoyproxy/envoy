Fixed a bug where a filter that stopped the request or response headers and continued them sent
on, as the end of the stream, a body that an earlier filter was holding and that had not reached
it, so that the body the earlier filter sent on afterwards was dropped. This could happen with a
filter that sends the headers on and holds the body, such as ``ext_proc`` in ``STREAMED`` or
``FULL_DUPLEX_STREAMED`` body mode, followed by a filter that stops the headers to make a call
out. This change can be temporarily reverted by setting the runtime guard
``envoy.reloadable_features.filter_manager_continue_without_held_data`` to ``false``.
