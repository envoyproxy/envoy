use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> { Box::new(TestStream) });
}}

struct TestStream;

impl Context for TestStream {}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize, _: bool) -> Action {
        panic!("");
    }

    fn on_http_request_body(&mut self, _: usize, _: bool) -> Action {
        panic!("");
    }

    fn on_http_request_trailers(&mut self, _: usize) -> Action {
        panic!("");
    }

    fn on_http_response_headers(&mut self, _: usize, _: bool) -> Action {
        panic!("");
    }

    fn on_http_response_body(&mut self, _: usize, _: bool) -> Action {
        panic!("");
    }

    fn on_http_response_trailers(&mut self, _: usize) -> Action {
        panic!("");
    }
}
