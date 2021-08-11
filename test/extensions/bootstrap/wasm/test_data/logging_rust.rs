use log::{debug, error, info, trace, warn};
use proxy_wasm::traits::{Context, RootContext};
use proxy_wasm::types::LogLevel;

extern "C" {
    fn __wasilibc_initialize_environ();
}

#[no_mangle]
pub fn _start() {
    unsafe {
        __wasilibc_initialize_environ();
    }
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_root_context(|_| -> Box<dyn RootContext> { Box::new(TestRoot) });
}

struct TestRoot;

impl RootContext for TestRoot {
    fn on_vm_start(&mut self, _: usize) -> bool {
        true
    }

    fn on_configure(&mut self, _: usize) -> bool {
        trace!("ON_CONFIGURE: {}", std::env::var("ON_CONFIGURE").unwrap());
        trace!("test trace logging");
        debug!("test debug logging");
        error!("test error logging");
        if let Some(value) = self.get_plugin_configuration() {
            warn!("warn {}", String::from_utf8(value).unwrap());
        }
        true
    }

    fn on_tick(&mut self) {
        trace!("ON_TICK: {}", std::env::var("ON_TICK").unwrap());
        if let Some(value) = self.get_property(vec!["plugin_root_id"]) {
            info!("test tick logging{}", String::from_utf8(value).unwrap());
        } else {
            info!("test tick logging");
        }
        self.done();
    }
}

impl Context for TestRoot {
    fn on_done(&mut self) -> bool {
        info!("onDone logging");
        false
    }
}

impl Drop for TestRoot {
    fn drop(&mut self) {
        info!("onDelete logging");
    }
}
