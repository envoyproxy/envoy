use log::{debug, info, warn};
use proxy_wasm::traits::{Context, HttpContext, RootContext};
use proxy_wasm::types::*;

#[no_mangle]
pub fn _start() {
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_root_context(|_| -> Box<dyn RootContext> {
        Box::new(TestRoot { queue_id: None })
    });
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> { Box::new(TestStream) });
}

struct TestRoot {
    queue_id: Option<u32>,
}

impl Context for TestRoot {}

impl RootContext for TestRoot {
    fn on_vm_start(&mut self, _: usize) -> bool {
        self.queue_id = Some(self.register_shared_queue("my_shared_queue"));
        true
    }

    fn on_queue_ready(&mut self, queue_id: u32) {
        if Some(queue_id) == self.queue_id {
            info!("onQueueReady");
            match self.dequeue_shared_queue(9999999 /* bad queue_id */) {
                Err(Status::NotFound) => warn!("onQueueReady bad token not found"),
                _ => (),
            }
            if let Some(value) = self.dequeue_shared_queue(queue_id).unwrap() {
                debug!("data {} Ok", String::from_utf8(value).unwrap());
            }
            if self.dequeue_shared_queue(queue_id).unwrap().is_none() {
                warn!("onQueueReady extra data not found");
            }
        }
    }
}

struct TestStream;

impl Context for TestStream {}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize) -> Action {
        if self
            .resolve_shared_queue("vm_id", "bad_shared_queue")
            .is_none()
        {
            warn!("onRequestHeaders not found bad_shared_queue");
        }
        if let Some(queue_id) = self.resolve_shared_queue("vm_id", "my_shared_queue") {
            self.enqueue_shared_queue(queue_id, Some(b"data1")).unwrap();
            warn!("onRequestHeaders enqueue Ok");
        }
        Action::Continue
    }
}
