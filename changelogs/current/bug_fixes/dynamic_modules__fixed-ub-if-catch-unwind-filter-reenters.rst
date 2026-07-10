Fixed undefined behavior in dynamic modules wrapped with CatchUnwind if the filter
was reentered during a callback (e.g., on_stream_complete being invoked inline when
responding with end-of-stream from on_scheduled). The filter is now borrowed instead
of taken when checking for poison.
