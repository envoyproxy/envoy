use log::{debug, info, warn};
use proxy_wasm::traits::{Context, HttpContext, RootContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_root_context(|_| -> Box<dyn RootContext> {
        Box::new(TestRoot { queue_id: None })
    });
}}

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

    fn get_type(&self) -> Option<ContextType> {
        Some(ContextType::HttpContext)
    }

    fn create_http_context(&self, _: u32) -> Option<Box<dyn HttpContext>> {
        Some(Box::new(TestStream {
            queue_id: self.queue_id,
        }))
    }
}

struct TestStream {
    queue_id: Option<u32>,
}

impl Context for TestStream {}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize, _: bool) -> Action {
        if self.resolve_shared_queue("", "bad_shared_queue").is_none() {
            warn!("onRequestHeaders not found self/bad_shared_queue");
        }
        if self
            .resolve_shared_queue("vm_id", "bad_shared_queue")
            .is_none()
        {
            warn!("onRequestHeaders not found vm_id/bad_shared_queue");
        }
        if self
            .resolve_shared_queue("bad_vm_id", "bad_shared_queue")
            .is_none()
        {
            warn!("onRequestHeaders not found bad_vm_id/bad_shared_queue");
        }
        if Some(self.resolve_shared_queue("", "my_shared_queue")) == Some(self.queue_id) {
            warn!("onRequestHeaders found self/my_shared_queue");
        }
        if Some(self.resolve_shared_queue("vm_id", "my_shared_queue")) == Some(self.queue_id) {
            warn!("onRequestHeaders found vm_id/my_shared_queue");
        }
        self.enqueue_shared_queue(self.queue_id.unwrap(), Some(b"data1"))
            .unwrap();
        warn!("onRequestHeaders enqueue Ok");
        Action::Continue
    }
}
