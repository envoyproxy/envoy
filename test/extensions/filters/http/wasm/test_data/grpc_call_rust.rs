use log::{debug, error};
use protobuf::well_known_types::Value;
use protobuf::Message;
use proxy_wasm::traits::{Context, HttpContext, RootContext};
use proxy_wasm::types::*;
use std::cell::Cell;
use std::time::Duration;

thread_local! {
    // TODO(PiotrSikora): use child-to-parent reference once improved in the SDK.
    static CALLOUT_ID: Cell<Option<u32>> = Cell::new(None);
}

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_root_context(|_| -> Box<dyn RootContext> { Box::new(TestGrpcCallRoot) });
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> { Box::new(TestGrpcCall) });
}}

struct TestGrpcCallRoot;

impl RootContext for TestGrpcCallRoot {
    fn on_queue_ready(&mut self, _: u32) {
        CALLOUT_ID.with(|saved_id| {
            if let Some(callout_id) = saved_id.get() {
                self.cancel_grpc_call(callout_id);
            }
        });
    }
}

impl Context for TestGrpcCallRoot {}

struct TestGrpcCall;

impl HttpContext for TestGrpcCall {
    fn on_http_request_headers(&mut self, _: usize, end_of_stream: bool) -> Action {
        let mut value = Value::new();
        value.set_string_value(String::from("request"));
        let message = value.write_to_bytes().unwrap();

        match self.dispatch_grpc_call(
            "bogus grpc_service",
            "service",
            "method",
            vec![("source", b"grpc_call")],
            Some(&message),
            Duration::from_secs(1),
        ) {
            Ok(_) => error!("bogus grpc_service succeeded"),
            Err(_) => error!("bogus grpc_service rejected"),
        };

        if end_of_stream {
            match self.dispatch_grpc_call(
                "cluster",
                "service",
                "method",
                vec![("source", b"grpc_call")],
                Some(&message),
                Duration::from_secs(1),
            ) {
                Err(Status::InternalFailure) => error!("expected failure occurred"),
                _ => error!("unexpected cluster call result"),
            };
            Action::Continue
        } else {
            match self.dispatch_grpc_call(
                "cluster",
                "service",
                "method",
                vec![("source", b"grpc_call")],
                Some(&message),
                Duration::from_secs(1),
            ) {
                Ok(callout_id) => {
                    CALLOUT_ID.with(|saved_id| saved_id.set(Some(callout_id)));
                    error!("cluster call succeeded")
                }
                Err(_) => error!("cluster call rejected"),
            };
            Action::Pause
        }
    }
}

impl Context for TestGrpcCall {
    fn on_grpc_call_response(&mut self, _: u32, status_code: u32, response_size: usize) {
        if status_code != 0 {
            let (_, message) = self.get_grpc_status();
            debug!("failure {}", &message.as_deref().unwrap_or(""));
        } else if let Some(response_bytes) = self.get_grpc_call_response_body(0, response_size) {
            let value = Value::parse_from_bytes(&response_bytes).unwrap();
            debug!("{}", value.get_string_value());
        }
    }
}
