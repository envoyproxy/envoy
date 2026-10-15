//! Integration test module for formatter dynamic modules.
//!
//! This module registers a formatter command parser through the `formatter:` arm of
//! `declare_all_init_functions!`. A single config recognizes several custom `%COMMAND%` operators
//! that exercise the full formatter context ABI surface: request, response, and trailer headers
//! (single value, indexed value, count, and bulk iteration), stream info attributes (string, int,
//! and bool), the request ID, dynamic metadata, the local reply body, and the access log type.
//! Unknown commands return `None` so other command parsers can handle them.

use envoy_proxy_dynamic_modules_rust_sdk::formatter::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_all_init_functions!(init, formatter: new_formatter_config_fn);

fn init() -> bool {
  true
}

/// Formatter factory function: dispatches to the config implementation based on `formatter_name`.
/// Returning `None` for an unknown name causes Envoy to reject the formatter configuration at
/// config load time.
fn new_formatter_config_fn(name: &str, _config: &[u8]) -> Option<Box<dyn FormatterConfig>> {
  match name {
    "test_formatter" => Some(Box::new(TestFormatterConfig {})),
    _ => None,
  }
}

struct TestFormatterConfig {}

impl FormatterConfig for TestFormatterConfig {
  fn parse(
    &self,
    command: &str,
    command_arg: &str,
    max_length: Option<usize>,
  ) -> Option<Box<dyn FormatterProvider>> {
    let kind = match command {
      "DYNAMIC_MODULE_REQ" => {
        ProviderKind::Header(HeaderType::RequestHeader, command_arg.to_string(), 0)
      },
      "DYNAMIC_MODULE_REQ_AT" => {
        let (name, index) = command_arg.split_once(',')?;
        ProviderKind::Header(
          HeaderType::RequestHeader,
          name.to_string(),
          index.parse().ok()?,
        )
      },
      "DYNAMIC_MODULE_RESP" => {
        ProviderKind::Header(HeaderType::ResponseHeader, command_arg.to_string(), 0)
      },
      "DYNAMIC_MODULE_TRAILER" => {
        ProviderKind::Header(HeaderType::ResponseTrailer, command_arg.to_string(), 0)
      },
      "DYNAMIC_MODULE_RESP_CODE" => ProviderKind::ResponseCode,
      "DYNAMIC_MODULE_PROTOCOL" => ProviderKind::Protocol,
      "DYNAMIC_MODULE_MTLS" => ProviderKind::Mtls,
      "DYNAMIC_MODULE_META" => ProviderKind::DynamicMetadata(command_arg.to_string()),
      "DYNAMIC_MODULE_HEADER_COUNT" => ProviderKind::HeaderCount(HeaderType::RequestHeader),
      "DYNAMIC_MODULE_RESP_COUNT" => ProviderKind::HeaderCount(HeaderType::ResponseHeader),
      "DYNAMIC_MODULE_REQ_KEYS" => ProviderKind::HeaderKeys(HeaderType::RequestHeader),
      "DYNAMIC_MODULE_RESP_KEYS" => ProviderKind::HeaderKeys(HeaderType::ResponseHeader),
      "DYNAMIC_MODULE_TRAILER_KEYS" => ProviderKind::HeaderKeys(HeaderType::ResponseTrailer),
      "DYNAMIC_MODULE_REQUEST_ID" => ProviderKind::RequestId,
      "DYNAMIC_MODULE_EMPTY" => ProviderKind::Empty,
      "DYNAMIC_MODULE_LOCAL_REPLY" => ProviderKind::LocalReplyBody,
      "DYNAMIC_MODULE_LOG_TYPE" => ProviderKind::LogType,
      "DYNAMIC_MODULE_CONST" => ProviderKind::Const,
      _ => return None,
    };
    Some(Box::new(TestProvider { kind, max_length }))
  }
}

fn buffer_to_string(buffer: EnvoyBuffer) -> String {
  String::from_utf8_lossy(buffer.as_slice()).into_owned()
}

type HeaderType = abi::envoy_dynamic_module_type_http_header_type;

enum ProviderKind {
  Header(HeaderType, String, usize),
  ResponseCode,
  Protocol,
  Mtls,
  DynamicMetadata(String),
  HeaderCount(HeaderType),
  HeaderKeys(HeaderType),
  RequestId,
  Empty,
  LocalReplyBody,
  LogType,
  Const,
}

struct TestProvider {
  kind: ProviderKind,
  max_length: Option<usize>,
}

impl FormatterProvider for TestProvider {
  fn format(&self, ctx: &FormatterContext) -> Option<String> {
    let value = match &self.kind {
      ProviderKind::Header(header_type, name, index) => {
        // Index 0 exercises the type-specific single-value wrappers, while a non-zero index
        // exercises the general indexed lookup used for multi-value headers.
        let buffer = if *index == 0 {
          match header_type {
            HeaderType::RequestHeader => ctx.get_request_header(name),
            HeaderType::ResponseHeader => ctx.get_response_header(name),
            HeaderType::ResponseTrailer => ctx.get_response_trailer(name),
            _ => None,
          }
        } else {
          ctx.get_header_value(*header_type, name, *index)
        };
        buffer_to_string(buffer?)
      },
      ProviderKind::ResponseCode => ctx.response_code()?.to_string(),
      ProviderKind::Protocol => buffer_to_string(ctx.protocol()?),
      ProviderKind::Mtls => ctx
        .get_attribute_bool(abi::envoy_dynamic_module_type_attribute_id::ConnectionMtls)?
        .to_string(),
      ProviderKind::DynamicMetadata(arg) => {
        let (namespace, key) = arg.split_once(':')?;
        buffer_to_string(ctx.get_dynamic_metadata(namespace, key)?)
      },
      ProviderKind::HeaderCount(header_type) => ctx.get_headers_count(*header_type).to_string(),
      ProviderKind::HeaderKeys(header_type) => ctx
        .get_all_headers(*header_type)
        .iter()
        .map(|(key, _)| String::from_utf8_lossy(key.as_slice()).into_owned())
        .collect::<Vec<_>>()
        .join(","),
      ProviderKind::RequestId => buffer_to_string(ctx.request_id()?),
      ProviderKind::Empty => String::new(),
      ProviderKind::LocalReplyBody => buffer_to_string(ctx.local_reply_body()?),
      ProviderKind::LogType => ctx.access_log_type().as_str().to_string(),
      ProviderKind::Const => "constant".to_string(),
    };
    match self.max_length {
      Some(max_length) if value.len() > max_length => Some(value[..max_length].to_string()),
      _ => Some(value),
    }
  }
}
