use proxy_wasm::traits::{Context, StreamContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_stream_context(|_, _| -> Box<dyn StreamContext> { Box::new(TestStream) });
}}

struct TestStream;

impl Context for TestStream {}

impl StreamContext for TestStream {
    fn on_new_connection(&mut self) -> Action {
        panic!("");
    }

    fn on_downstream_data(&mut self, _: usize, _: bool) -> Action {
        panic!("");
    }

    fn on_upstream_data(&mut self, _: usize, _: bool) -> Action {
        panic!("");
    }
}
