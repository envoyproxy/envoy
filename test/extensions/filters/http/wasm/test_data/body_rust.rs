use log::error;
use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> {
        Box::new(TestStream {
            test: None,
            body_chunks: 0,
        })
    });
}}

struct TestStream {
    test: Option<String>,
    body_chunks: usize,
}

impl TestStream {
    fn log_body(&mut self, body: Option<Bytes>) {
        error!(
            "onBody {}",
            body.map_or(String::from(""), |b| String::from_utf8(b).unwrap())
        );
    }
}

impl HttpContext for TestStream {
    fn on_http_request_headers(&mut self, _: usize, _: bool) -> Action {
        self.test = self.get_http_request_header("x-test-operation");
        self.body_chunks = 0;
        Action::Continue
    }

    fn on_http_request_body(&mut self, body_size: usize, end_of_stream: bool) -> Action {
        match self.test.as_deref() {
            Some("ReadBody") => {
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                Action::Continue
            }
            Some("PrependAndAppendToBody") => {
                self.set_http_request_body(0, 0, b"prepend.");
                self.set_http_request_body(0xffffffff, 0, b".append");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                Action::Continue
            }
            Some("ReplaceBody") => {
                self.set_http_request_body(0, 0xffffffff, b"replace");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                Action::Continue
            }
            Some("PartialReplaceBody") => {
                self.set_http_request_body(0, 1, b"partial.replace.");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                Action::Continue
            }
            Some("RemoveBody") => {
                self.set_http_request_body(0, 0xffffffff, b"");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                Action::Continue
            }
            Some("PartialRemoveBody") => {
                self.set_http_request_body(0, 1, b"");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                Action::Continue
            }
            Some("BufferBody") => {
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PrependAndAppendToBufferedBody") => {
                self.set_http_request_body(0, 0, b"prepend.");
                self.set_http_request_body(0xffffffff, 0, b".append");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("ReplaceBufferedBody") => {
                self.set_http_request_body(0, 0xffffffff, b"replace");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PartialReplaceBufferedBody") => {
                self.set_http_request_body(0, 1, b"partial.replace.");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("RemoveBufferedBody") => {
                self.set_http_request_body(0, 0xffffffff, b"");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PartialRemoveBufferedBody") => {
                self.set_http_request_body(0, 1, b"");
                self.log_body(self.get_http_request_body(0, 0xffffffff));
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

    fn on_http_response_headers(&mut self, _: usize, _: bool) -> Action {
        self.test = self.get_http_response_header("x-test-operation");
        Action::Continue
    }

    fn on_http_response_body(&mut self, body_size: usize, end_of_stream: bool) -> Action {
        match self.test.as_deref() {
            Some("ReadBody") => {
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                Action::Continue
            }
            Some("PrependAndAppendToBody") => {
                self.set_http_response_body(0, 0, b"prepend.");
                self.set_http_response_body(0xffffffff, 0, b".append");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                Action::Continue
            }
            Some("ReplaceBody") => {
                self.set_http_response_body(0, 0xffffffff, b"replace");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                Action::Continue
            }
            Some("PartialReplaceBody") => {
                self.set_http_response_body(0, 1, b"partial.replace.");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                Action::Continue
            }
            Some("RemoveBody") => {
                self.set_http_response_body(0, 0xffffffff, b"");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                Action::Continue
            }
            Some("PartialRemoveBody") => {
                self.set_http_response_body(0, 1, b"");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                Action::Continue
            }
            Some("BufferBody") => {
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PrependAndAppendToBufferedBody") => {
                self.set_http_response_body(0, 0, b"prepend.");
                self.set_http_response_body(0xffffffff, 0, b".append");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("ReplaceBufferedBody") => {
                self.set_http_response_body(0, 0xffffffff, b"replace");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PartialReplaceBufferedBody") => {
                self.set_http_response_body(0, 1, b"partial.replace.");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("RemoveBufferedBody") => {
                self.set_http_response_body(0, 0xffffffff, b"");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
                if end_of_stream {
                    Action::Continue
                } else {
                    Action::Pause
                }
            }
            Some("PartialRemoveBufferedBody") => {
                self.set_http_response_body(0, 1, b"");
                self.log_body(self.get_http_response_body(0, 0xffffffff));
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
