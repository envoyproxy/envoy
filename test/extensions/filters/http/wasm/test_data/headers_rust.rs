use log::{debug, error, info, trace, warn};
use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_http_context(|context_id, _| -> Box<dyn HttpContext> {
        Box::new(TestStream { context_id })
    });
}}

struct TestStream {
    context_id: u32,
}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize, _: bool) -> Action {
        let mut msg = String::new();
        if let Ok(value) = std::env::var("ENVOY_HTTP_WASM_TEST_HEADERS_HOST_ENV") {
            msg.push_str("ENVOY_HTTP_WASM_TEST_HEADERS_HOST_ENV: ");
            msg.push_str(&value);
        }
        if let Ok(value) = std::env::var("ENVOY_HTTP_WASM_TEST_HEADERS_KEY_VALUE_ENV") {
            msg.push_str("\nENVOY_HTTP_WASM_TEST_HEADERS_KEY_VALUE_ENV: ");
            msg.push_str(&value);
        }
        if !msg.is_empty() {
            trace!("{}", msg);
        }
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

    fn on_http_request_body(&mut self, body_size: usize, end_of_stream: bool) -> Action {
        if let Some(body) = self.get_http_request_body(0, body_size) {
            error!("onBody {}", String::from_utf8(body).unwrap());
        }
        if end_of_stream {
            self.add_http_request_trailer("newtrailer", "request");
        }
        Action::Continue
    }

    fn on_http_response_headers(&mut self, _: usize, _: bool) -> Action {
        self.set_http_response_header("test-status", Some("OK"));
        Action::Continue
    }

    fn on_http_response_body(&mut self, _: usize, end_of_stream: bool) -> Action {
        if end_of_stream {
            self.add_http_response_trailer("newtrailer", "response");
        }
        Action::Continue
    }

    fn on_http_response_trailers(&mut self, _: usize) -> Action {
        Action::Pause
    }

    fn on_log(&mut self) {
        let path = self
            .get_http_request_header(":path")
            .unwrap_or(String::from(""));
        let status = self
            .get_http_response_header(":status")
            .unwrap_or(String::from(""));
        warn!("onLog {} {} {}", self.context_id, path, status);
    }
}

impl Context for TestStream {
    fn on_done(&mut self) -> bool {
        warn!("onDone {}", self.context_id);
        true
    }
}
