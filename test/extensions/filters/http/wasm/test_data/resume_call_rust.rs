use log::info;
use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;
use std::time::Duration;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> { Box::new(TestStream) });
}}

struct TestStream;

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize, _: bool) -> Action {
        self.dispatch_http_call(
            "cluster",
            vec![(":method", "POST"), (":path", "/"), (":authority", "foo")],
            Some(b"resume"),
            vec![],
            Duration::from_secs(1),
        )
        .unwrap();
        info!("onRequestHeaders");
        Action::Pause
    }

    fn on_http_request_body(&mut self, _: usize, _: bool) -> Action {
        info!("onRequestBody");
        Action::Continue
    }
}

impl Context for TestStream {
    fn on_http_call_response(&mut self, _: u32, _: usize, _: usize, _: usize) {
        info!("continueRequest");
        self.resume_http_request();
    }
}
