use log::{debug, error, info, warn};
use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;

#[no_mangle]
pub fn _start() {
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_http_context(|context_id, _| -> Box<dyn HttpContext> {
        Box::new(TestStream { context_id })
    });
}

struct TestStream {
    context_id: u32,
}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize) -> Action {
        debug!("onRequestHeaders {} headers", self.context_id);
        if let Some(path) = self.get_http_request_header(":path") {
            info!("header path {}", path);
        }
        let action = match self.get_http_request_header("server").as_deref() {
            Some("envoy-wasm-pause") => Action::Pause,
            _ => Action::Continue,
        };
        self.set_http_request_header("newheader", Some("newheadervalue"));
        self.set_http_request_header("server", Some("envoy-wasm"));
        action
    }

    fn on_http_request_body(&mut self, body_size: usize, _: bool) -> Action {
        if let Some(body) = self.get_http_request_body(0, body_size) {
            error!("onBody {}", String::from_utf8(body).unwrap());
        }
        Action::Continue
    }

    fn on_http_response_trailers(&mut self, _: usize) -> Action {
        Action::Pause
    }

    fn on_log(&mut self) {
        if let Some(path) = self.get_http_request_header(":path") {
            warn!("onLog {} {}", self.context_id, path);
        }
    }
}

impl Context for TestStream {
    fn on_done(&mut self) -> bool {
        warn!("onDone {}", self.context_id);
        true
    }
}
