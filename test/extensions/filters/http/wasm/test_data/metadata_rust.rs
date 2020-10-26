use log::{debug, error, info, trace};
use proxy_wasm::traits::{Context, HttpContext, RootContext};
use proxy_wasm::types::*;
use std::convert::TryFrom;

#[no_mangle]
pub fn _start() {
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_root_context(|_| -> Box<dyn RootContext> { Box::new(TestRoot) });
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> { Box::new(TestStream) });
}

struct TestRoot;

impl Context for TestRoot {}

impl RootContext for TestRoot {
    fn on_tick(&mut self) {
        if let Some(value) = self.get_property(vec!["node", "metadata", "wasm_node_get_key"]) {
            debug!("onTick {}", String::from_utf8(value).unwrap());
        } else {
            debug!("missing node metadata");
        }
    }
}

struct TestStream;

impl Context for TestStream {}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize) -> Action {
        if self
            .get_property(vec!["node", "metadata", "wasm_node_get_key"])
            .is_none()
        {
            debug!("missing node metadata");
        }

        self.set_property(
            vec!["wasm_request_set_key"],
            Some(b"wasm_request_set_value"),
        );

        if let Some(path) = self.get_http_request_header(":path") {
            info!("header path {}", path);
        }
        self.set_http_request_header("newheader", Some("newheadervalue"));
        self.set_http_request_header("server", Some("envoy-wasm"));

        if let Some(value) = self.get_property(vec!["request", "duration"]) {
            info!(
                "duration is {}",
                u64::from_le_bytes(<[u8; 8]>::try_from(&value[0..8]).unwrap())
            );
        } else {
            error!("failed to get request duration");
        }
        Action::Continue
    }

    fn on_http_request_body(&mut self, _: usize, _: bool) -> Action {
        if let Some(value) = self.get_property(vec!["node", "metadata", "wasm_node_get_key"]) {
            error!("onBody {}", String::from_utf8(value).unwrap());
        } else {
            debug!("missing node metadata");
        }
        let key1 = self.get_property(vec![
            "metadata",
            "filter_metadata",
            "envoy.filters.http.wasm",
            "wasm_request_get_key",
        ]);
        if key1.is_none() {
            debug!("missing request metadata");
        }
        let key2 = self.get_property(vec![
            "metadata",
            "filter_metadata",
            "envoy.filters.http.wasm",
            "wasm_request_get_key",
        ]);
        if key2.is_none() {
            debug!("missing request metadata");
        }
        trace!(
            "Struct {} {}",
            String::from_utf8(key1.unwrap()).unwrap(),
            String::from_utf8(key2.unwrap()).unwrap()
        );
        Action::Continue
    }
}
