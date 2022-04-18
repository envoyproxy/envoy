use log::{info, trace};
use proxy_wasm::traits::{Context, StreamContext};
use proxy_wasm::types::*;
use std::time::Duration;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_stream_context(|context_id, _| -> Box<dyn StreamContext> {
        Box::new(TestStream {
            context_id,
            downstream_callout: None,
            upstream_callout: None,
        })
    });
}}

struct TestStream {
    context_id: u32,
    downstream_callout: Option<u32>,
    upstream_callout: Option<u32>,
}

impl StreamContext for TestStream {
    fn on_downstream_data(&mut self, _: usize, _: bool) -> Action {
        self.downstream_callout = self
            .dispatch_http_call(
                "cluster",
                vec![(":method", "POST"), (":path", "/"), (":authority", "foo")],
                Some(b"resume"),
                vec![],
                Duration::from_secs(1),
            )
            .ok();
        trace!("onDownstreamData {}", self.context_id);
        Action::Pause
    }

    fn on_upstream_data(&mut self, _: usize, _: bool) -> Action {
        self.upstream_callout = self
            .dispatch_http_call(
                "cluster",
                vec![(":method", "POST"), (":path", "/"), (":authority", "foo")],
                Some(b"resume"),
                vec![],
                Duration::from_secs(1),
            )
            .ok();
        trace!("onUpstreamData {}", self.context_id);
        Action::Pause
    }
}

impl Context for TestStream {
    fn on_http_call_response(&mut self, callout_id: u32, _: usize, _: usize, _: usize) {
        if Some(callout_id) == self.downstream_callout {
            self.resume_downstream();
            info!("continueDownstream");
        }
    }
}
