use log::error;
use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;

#[no_mangle]
pub fn _start() {
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> {
        Box::new(TestStream {
            test: None,
            body_chunks: 0,
        })
    });
}

struct TestStream {
    test: Option<String>,
    body_chunks: usize,
}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize) -> Action {
        self.test = self.get_http_request_header("x-test-operation");
        self.body_chunks = 0;
        Action::Continue
    }

    fn on_http_request_body(&mut self, body_size: usize, end_of_stream: bool) -> Action {
        match self.test.as_deref() {
            Some("ReadBody") => {
                let body = self.get_http_request_body(0, body_size).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                Action::Continue
            }
            Some("PrependAndAppendToBody") => {
                self.set_http_request_body(0, 0, b"prepend.");
                self.set_http_request_body(0xffffffff, 0, b".append");
                let body = self.get_http_request_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                Action::Continue
            }
            Some("ReplaceBody") => {
                self.set_http_request_body(0, 0xffffffff, b"replace");
                let body = self.get_http_request_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                Action::Continue
            }
            Some("RemoveBody") => {
                self.set_http_request_body(0, 0xffffffff, b"");
                if let Some(body) = self.get_http_request_body(0, 0xffffffff) {
                    error!("onBody {}", String::from_utf8(body).unwrap());
                } else {
                    error!("onBody ");
                }
                Action::Continue
            }
            Some("BufferBody") => {
                let body = self.get_http_request_body(0, body_size).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PrependAndAppendToBufferedBody") => {
                self.set_http_request_body(0, 0, b"prepend.");
                self.set_http_request_body(0xffffffff, 0, b".append");
                let body = self.get_http_request_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("ReplaceBufferedBody") => {
                self.set_http_request_body(0, 0xffffffff, b"replace");
                let body = self.get_http_request_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("RemoveBufferedBody") => {
                self.set_http_request_body(0, 0xffffffff, b"");
                if let Some(body) = self.get_http_request_body(0, 0xffffffff) {
                    error!("onBody {}", String::from_utf8(body).unwrap());
                } else {
                    error!("onBody ");
                }
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("BufferTwoBodies") => {
                if let Some(body) = self.get_http_request_body(0, body_size) {
                    error!("onBody {}", String::from_utf8(body).unwrap());
                }
                self.body_chunks += 1;
                if end_of_stream || self.body_chunks > 2 {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            _ => Action::Continue,
        }
    }

    fn on_http_response_headers(&mut self, _: usize) -> Action {
        self.test = self.get_http_response_header("x-test-operation");
        Action::Continue
    }

    fn on_http_response_body(&mut self, body_size: usize, end_of_stream: bool) -> Action {
        match self.test.as_deref() {
            Some("ReadBody") => {
                let body = self.get_http_response_body(0, body_size).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                Action::Continue
            }
            Some("PrependAndAppendToBody") => {
                self.set_http_response_body(0, 0, b"prepend.");
                self.set_http_response_body(0xffffffff, 0, b".append");
                let body = self.get_http_response_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                Action::Continue
            }
            Some("ReplaceBody") => {
                self.set_http_response_body(0, 0xffffffff, b"replace");
                let body = self.get_http_response_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                Action::Continue
            }
            Some("RemoveBody") => {
                self.set_http_response_body(0, 0xffffffff, b"");
                if let Some(body) = self.get_http_response_body(0, 0xffffffff) {
                    error!("onBody {}", String::from_utf8(body).unwrap());
                } else {
                    error!("onBody ");
                }
                Action::Continue
            }
            Some("BufferBody") => {
                let body = self.get_http_response_body(0, body_size).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PrependAndAppendToBufferedBody") => {
                self.set_http_response_body(0, 0, b"prepend.");
                self.set_http_response_body(0xffffffff, 0, b".append");
                let body = self.get_http_response_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("ReplaceBufferedBody") => {
                self.set_http_response_body(0, 0xffffffff, b"replace");
                let body = self.get_http_response_body(0, 0xffffffff).unwrap();
                error!("onBody {}", String::from_utf8(body).unwrap());
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("RemoveBufferedBody") => {
                self.set_http_response_body(0, 0xffffffff, b"");
                if let Some(body) = self.get_http_response_body(0, 0xffffffff) {
                    error!("onBody {}", String::from_utf8(body).unwrap());
                } else {
                    error!("onBody ");
                }
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("BufferTwoBodies") => {
                if let Some(body) = self.get_http_response_body(0, body_size) {
                    error!("onBody {}", String::from_utf8(body).unwrap());
                }
                self.body_chunks += 1;
                if end_of_stream || self.body_chunks > 2 {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            _ => Action::Continue,
        }
    }
}

impl Context for TestStream {}
