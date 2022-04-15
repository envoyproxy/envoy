use log::{debug, info, warn};
use proxy_wasm::traits::{Context, RootContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_root_context(|_| -> Box<dyn RootContext> { Box::new(TestRoot) });
}}

struct TestRoot;

impl Context for TestRoot {}

impl RootContext for TestRoot {
    fn on_tick(&mut self) {
        if self.get_shared_data("shared_data_key_bad") == (None, None) {
            debug!("get of bad key not found");
        }
        self.set_shared_data("shared_data_key1", Some(b"shared_data_value0"), None)
            .unwrap();
        self.set_shared_data("shared_data_key1", Some(b"shared_data_value1"), None)
            .unwrap();
        self.set_shared_data("shared_data_key2", Some(b"shared_data_value2"), None)
            .unwrap();
        if let (_, Some(cas)) = self.get_shared_data("shared_data_key2") {
            match self.set_shared_data(
                "shared_data_key2",
                Some(b"shared_data_value3"),
                Some(cas + 1),
            ) {
                Err(Status::CasMismatch) => info!("set CasMismatch"),
                _ => panic!(),
            };
        }
    }

    fn on_queue_ready(&mut self, _: u32) {
        if self.get_shared_data("shared_data_key_bad") == (None, None) {
            debug!("second get of bad key not found");
        }
        if let (Some(value), _) = self.get_shared_data("shared_data_key1") {
            debug!("get 1 {}", String::from_utf8(value).unwrap());
        }
        if let (Some(value), _) = self.get_shared_data("shared_data_key2") {
            warn!("get 2 {}", String::from_utf8(value).unwrap());
        }
    }
}
