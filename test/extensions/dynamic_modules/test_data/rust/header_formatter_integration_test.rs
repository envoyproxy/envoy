//! Integration test module for HTTP/1 header formatter dynamic modules.
//!
//! This module registers header formatter configurations through the `header_formatter:` arm of
//! `declare_all_init_functions!`. The `preserve_case` configuration remembers every key the peer
//! sent and restores that spelling on the way out, upper-casing keys it never saw so the test can
//! tell the `process_key` path from the `format` path. The `counting` configuration exercises the
//! `Send + Sync` requirement of a single configuration shared by every worker thread, and
//! `decline_formatter` never creates a formatter so the test can observe the fallback to Envoy's
//! default casing. An unknown name returns `None`, which makes Envoy reject the configuration.

use envoy_proxy_dynamic_modules_rust_sdk::header_formatter::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};

declare_all_init_functions!(init, header_formatter: new_header_formatter_config_fn);

fn init() -> bool {
  true
}

/// Header formatter factory: dispatches on `header_formatter_name`. Returning `None` for an
/// unknown name causes Envoy to reject the configuration at config load time.
fn new_header_formatter_config_fn(
  name: &str,
  config: &[u8],
) -> Option<Box<dyn HeaderFormatterConfig>> {
  match name {
    "preserve_case" => Some(Box::new(PreserveCaseConfig {
      extra_key: String::from_utf8_lossy(config).into_owned(),
    })),
    "counting" => Some(Box::new(CountingConfig {
      formatters: AtomicU64::new(0),
    })),
    "decline_formatter" => Some(Box::new(DeclineConfig)),
    _ => None,
  }
}

/// Shared by every worker thread, so it holds only immutable state.
struct PreserveCaseConfig {
  /// A key from the module configuration that is always upper-cased, even if the peer sent it in
  /// another casing. This makes the configuration bytes observable in the response.
  extra_key: String,
}

impl HeaderFormatterConfig for PreserveCaseConfig {
  fn create(&self) -> Option<Box<dyn HeaderFormatter>> {
    Some(Box::new(PreserveCaseFormatter {
      observed: HashMap::new(),
      extra_key: self.extra_key.clone(),
      formatted: String::new(),
    }))
  }
}

/// Per-message state: the keys this message's peer actually sent, indexed by their lower-cased
/// form. Envoy calls every method from the one worker thread owning the message, so a plain
/// `HashMap` and `&mut self` are enough.
struct PreserveCaseFormatter {
  observed: HashMap<String, String>,
  extra_key: String,
  /// Holds the value returned by the last format call. The ABI requires it to stay valid until the
  /// next call into the module, which storage owned by the formatter does.
  formatted: String,
}

impl HeaderFormatter for PreserveCaseFormatter {
  fn process_key(&mut self, key: &str, handle: &HeaderFormatterHandle) {
    // The handle is the module's window onto the host for this message. Only logging is exposed
    // today, and logging every key at trace level is how this module proves the handle works. The
    // enablement check keeps the formatting cost off the hot path when trace is off.
    if handle.is_log_level_enabled(abi::envoy_dynamic_module_type_log_level::Trace) {
      handle.log(
        abi::envoy_dynamic_module_type_log_level::Trace,
        &format!("header formatter observed key: {key}"),
      );
    }
    self.observed.insert(key.to_lowercase(), key.to_string());
  }

  fn format(&mut self, key: &str, handle: &HeaderFormatterHandle) -> Option<&str> {
    // Exercises the third handle accessor: a module can align its own verbosity with Envoy's.
    if handle.get_log_level() == abi::envoy_dynamic_module_type_log_level::Trace {
      handle.log(
        abi::envoy_dynamic_module_type_log_level::Trace,
        &format!("header formatter formatting key: {key}"),
      );
    }
    // The value is built into a local and then moved into the field the returned view borrows
    // from, which keeps the lookup's borrow of `observed` out of the way of that assignment.
    let formatted = if !self.extra_key.is_empty() && key.eq_ignore_ascii_case(&self.extra_key) {
      key.to_uppercase()
    } else if let Some(original) = self.observed.get(key) {
      original.clone()
    } else {
      // Never observed, so this is a header Envoy added itself. Upper-casing it makes the two
      // paths distinguishable in the test.
      key.to_uppercase()
    };
    self.formatted = formatted;
    Some(&self.formatted)
  }
}

/// Exercises the `Send + Sync` requirement: one instance is shared by every worker thread, so the
/// only mutable state it keeps is an atomic.
struct CountingConfig {
  formatters: AtomicU64,
}

impl HeaderFormatterConfig for CountingConfig {
  fn create(&self) -> Option<Box<dyn HeaderFormatter>> {
    let count = self.formatters.fetch_add(1, Ordering::Relaxed) + 1;
    Some(Box::new(CountingFormatter {
      count,
      formatted: String::new(),
    }))
  }
}

struct CountingFormatter {
  count: u64,
  formatted: String,
}

impl HeaderFormatter for CountingFormatter {
  fn format(&mut self, key: &str, _handle: &HeaderFormatterHandle) -> Option<&str> {
    // Report the formatter's ordinal in a header key so the test can see how many were created.
    if key != "x-formatter-count" {
      return None;
    }
    self.formatted = format!("x-formatter-count-{}", self.count);
    Some(&self.formatted)
  }
}

/// Never creates a formatter, which must leave Envoy using its default header casing rather than
/// failing the message.
struct DeclineConfig;

impl HeaderFormatterConfig for DeclineConfig {
  fn create(&self) -> Option<Box<dyn HeaderFormatter>> {
    None
  }
}
