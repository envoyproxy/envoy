use log::trace;
use proxy_wasm::traits::{Context, StreamContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_stream_context(|context_id, _| -> Box<dyn StreamContext> {
        Box::new(TestStream { context_id })
    });
}}

struct TestStream {
    context_id: u32,
}

impl Context for TestStream {}

impl StreamContext for TestStream {
    fn on_new_connection(&mut self) -> Action {
        trace!("onNewConnection {}", self.context_id);
        Action::Continue
    }

    fn on_downstream_data(&mut self, data_size: usize, end_of_stream: bool) -> Action {
        if let Some(data) = self.get_downstream_data(0, data_size) {
            trace!(
                "onDownstreamData {} len={} end_stream={}\n{}",
                self.context_id,
                data_size,
                end_of_stream as u32,
                String::from_utf8(data).unwrap()
            );
        }
        self.set_downstream_data(0, data_size, b"write");
        Action::Continue
    }

    fn on_upstream_data(&mut self, data_size: usize, end_of_stream: bool) -> Action {
        if let Some(data) = self.get_upstream_data(0, data_size) {
            trace!(
                "onUpstreamData {} len={} end_stream={}\n{}",
                self.context_id,
                data_size,
                end_of_stream as u32,
                String::from_utf8(data).unwrap()
            );
        }
        Action::Continue
    }

    fn on_downstream_close(&mut self, peer_type: PeerType) {
        trace!(
            "onDownstreamConnectionClose {} {}",
            self.context_id,
            peer_type as u32,
        );
    }

    fn on_upstream_close(&mut self, peer_type: PeerType) {
        trace!(
            "onUpstreamConnectionClose {} {}",
            self.context_id,
            peer_type as u32,
        );
    }
}
