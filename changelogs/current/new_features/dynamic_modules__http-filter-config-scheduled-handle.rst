Added the filter config handle to ``HttpFilterConfig::on_scheduled`` in the Rust SDK, so that a module can emit
the metrics defined on its config from an event scheduled to the main thread, with no stream involved.
