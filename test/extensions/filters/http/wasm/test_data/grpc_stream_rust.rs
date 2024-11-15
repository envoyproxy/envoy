use log::{debug, error};
use protobuf::well_known_types::Value;
use protobuf::Message;
use proxy_wasm::traits::{Context, HttpContext};
use proxy_wasm::types::*;

proxy_wasm::main! {{
    proxy_wasm::set_log_level(LogLevel::Trace);
    proxy_wasm::set_http_context(|_, _| -> Box<dyn HttpContext> { Box::new(TestGrpcStream) });
}}

struct TestGrpcStream;

impl HttpContext for TestGrpcStream {
    fn on_http_request_headers(&mut self, _: usize, _: bool) -> Action {
        match self.open_grpc_stream(
            "bogus service string",
            "service",
            "method",
            vec![("source", b"grpc_stream")],
        ) {
            Err(Status::ParseFailure) => error!("expected bogus service parse failure"),
            Ok(_) => error!("unexpected bogus service string OK"),
            Err(_) => error!("unexpected bogus service string error"),
        };

        match self.open_grpc_stream(
            "cluster",
            "service",
            "bad method",
            vec![("source", b"grpc_stream")],
        ) {
            Err(Status::InternalFailure) => error!("expected bogus method call failure"),
            Ok(_) => error!("unexpected bogus method call OK"),
            Err(_) => error!("unexpected bogus method call error"),
        };

        match self.open_grpc_stream(
            "cluster",
            "service",
            "method",
            vec![("source", b"grpc_stream")],
        ) {
            Ok(_) => error!("cluster call succeeded"),
            Err(_) => error!("cluster call rejected"),
        };

        Action::Pause
    }
}

impl Context for TestGrpcStream {
    fn on_grpc_stream_initial_metadata(&mut self, callout_id: u32, _: u32) {
        if self.get_grpc_stream_initial_metadata_value("test") == Some(b"reset".to_vec()) {
            self.cancel_grpc_stream(callout_id);
        }
    }

    fn on_grpc_stream_message(&mut self, callout_id: u32, message_size: usize) {
        if let Some(message_bytes) = self.get_grpc_call_response_body(0, message_size) {
            let response = Value::parse_from_bytes(&message_bytes).unwrap();
            let string = response.get_string_value();
            if string == String::from("close") {
                self.close_grpc_stream(callout_id);
            } else {
                let value = Value::new();
                let message = value.write_to_bytes().unwrap();
                self.send_grpc_stream_message(callout_id, Some(&message), false);
            }
            debug!("response {}", string);
        }
    }

    fn on_grpc_stream_trailing_metadata(&mut self, _: u32, _: u32) {
        let _ = self.get_grpc_stream_trailing_metadata_value("foo");
    }

    fn on_grpc_stream_close(&mut self, callout_id: u32, _: u32) {
        let (_, message) = self.get_grpc_status();
        debug!("close {}", &message.as_deref().unwrap_or(""));
        match message.as_deref() {
            Some("close") => self.close_grpc_stream(callout_id),
            Some("ok") => (),
            _ => self.cancel_grpc_stream(callout_id),
        };
    }
}
