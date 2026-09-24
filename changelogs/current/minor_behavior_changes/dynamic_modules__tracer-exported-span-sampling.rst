The dynamic modules tracer now returns the real sampling decision from ``exportedSpan`` instead of
always reporting the span as exported. When a span sampling decision is set to ``false`` through
``setSampled``, the span is now skipped at finalization, so module finalize callbacks such as
``envoy_dynamic_module_on_tracer_span_set_tag`` no longer run for it. A span still defaults to
exported until ``setSampled`` reports otherwise.
