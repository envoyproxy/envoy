use proxy_wasm::traits::{Context, StreamContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_stream_context(|_, _| -> Box<dyn StreamContext> { Box::new(TestStream) });
}}

struct TestStream;

impl Context for TestStream {}

impl StreamContext for TestStream {
    fn on_downstream_data(&mut self, _: usize, _: bool) -> Action {
        self.close_downstream();
        Action::Continue
    }

    fn on_upstream_data(&mut self, _: usize, _: bool) -> Action {
        self.close_upstream();
        Action::Continue
    }
}
