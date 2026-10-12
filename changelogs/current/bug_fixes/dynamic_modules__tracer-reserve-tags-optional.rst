dynamic modules: fixed a compatibility regression where a tracer module built against an SDK that
predates the ``reserve_tags`` hook failed to load, because the host resolved that hook as
mandatory. The ``reserve_tags`` hook is now resolved optionally, and the host skips the call when
the module does not define it.
