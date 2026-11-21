Fixed how the HTTP dynamic module filter tracks a stopped stream for the continue calls, Rust
``continue_decoding`` and ``continue_encoding``, C++ ``continueRequest`` and ``continueResponse``,
and Go ``ContinueRequest`` and ``ContinueResponse``. A ``continue_encoding`` call before the
response headers reached the filter was passed to the filter manager, which resumed response
headers that did not exist yet. A body hook that stopped again while a continue replayed the
buffered body lost its stop, so the module's next continue was dropped and the stream stalled.
``ContinueAndDontEndStream`` and metadata frames also left the continue state out of step. A
continue while its direction is iterating is now ignored and logged at debug level by the
``dynamic_modules`` logger. A module that continues from inside the hook that then stops, or after
``ContinueAndDontEndStream``, no longer resumes the stream. Such a stream ends at the stream idle
timeout with the ``SI`` response flag when one is set, or at the route timeout with ``UT``.
