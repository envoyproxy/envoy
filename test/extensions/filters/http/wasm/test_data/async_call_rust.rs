use log::{debug, info, warn};
use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;
use std::time::Duration;

#[no_mangle]
pub fn _start() {
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> { Box::new(TestStream) });
}

struct TestStream;

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize) -> Action {
        self.dispatch_http_call(
            "cluster",
            vec![(":method", "POST"), (":path", "/"), (":authority", "foo")],
            Some(b"hello world"),
            vec![("trail", "cow")],
            Duration::from_secs(5),
        )
        .unwrap();
        info!("onRequestHeaders");
        Action::Pause
    }
}

impl Context for TestStream {
    fn on_http_call_response(&mut self, _: u32, _: usize, body_size: usize, _: usize) {
        if body_size == 0 {
            info!("async_call failed");
            return;
        }
        for (name, value) in &self.get_http_call_response_headers() {
            info!("{} -> {}", name, value);
        }
        if let Some(body) = self.get_http_call_response_body(0, body_size) {
            debug!("{}", String::from_utf8(body).unwrap());
        }
        for (name, value) in &self.get_http_call_response_trailers() {
            warn!("{} -> {}", name, value);
        }
    }
}
