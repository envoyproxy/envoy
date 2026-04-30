//! Tracer integration test module. Mirror of
//! test_data/go/tracer_integration_test/tracer_integration_test.go.

use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_tracer_init_functions!(init, new_tracer_config);

fn init() -> bool {
  true
}

fn new_tracer_config(
  _ctx: TracerConfigContext,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn TracerConfig>> {
  Some(Box::new(TestTracer {}))
}

struct TestTracer {}

impl TracerConfig for TestTracer {
  fn start_span(
    &self,
    _envoy_span: &dyn EnvoyTracerSpan,
    _operation_name: &str,
    _traced: bool,
    _reason: TraceReason,
  ) -> Option<Box<dyn TracerSpan>> {
    Some(Box::new(TestSpan {}))
  }
}

struct TestSpan {}

impl TracerSpan for TestSpan {
  fn set_operation(&mut self, _operation: &str) {}
  fn set_tag(&mut self, _key: &str, _value: &str) {}
  fn log(&mut self, _timestamp_ns: i64, _event: &str) {}
  fn finish(&mut self) {}
  fn inject_context(&mut self, _envoy_span: &dyn EnvoyTracerSpan) {}
  fn spawn_child(&mut self, _operation: &str, _start_time_ns: i64) -> Option<Box<dyn TracerSpan>> {
    None
  }
  fn set_sampled(&mut self, _sampled: bool) {}
}
