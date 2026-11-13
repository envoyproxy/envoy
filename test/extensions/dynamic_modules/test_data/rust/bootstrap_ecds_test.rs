use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::Mutex;

declare_bootstrap_init_functions!(program_init, new_config);

fn program_init() -> bool {
  true
}

fn new_config(
  envoy: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  let path = std::str::from_utf8(config).expect("test handler path must be UTF-8");
  assert!(envoy.register_admin_handler(path, "Test bootstrap ECDS.", true, true));
  envoy.signal_init_complete();
  Some(Box::new(EcdsConfig {
    config: Mutex::new(None),
  }))
}

struct EcdsConfig {
  config: Mutex<Option<String>>,
}

impl BootstrapExtensionConfig for EcdsConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(EcdsExtension)
  }

  fn on_admin_request(
    &self,
    envoy: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    path: &str,
    body: &[u8],
  ) -> (u32, String) {
    if path.ends_with("?subscribe")
      && !envoy.subscribe_extension_config(ExtensionConfigSource {
        name: "test-config",
        type_url: "type.googleapis.com/google.protobuf.StringValue",
        config_source_json: std::str::from_utf8(body).expect("test source must be UTF-8"),
      })
    {
      return (400, "subscription failed".into());
    }
    (
      200,
      self
        .config
        .lock()
        .unwrap()
        .clone()
        .unwrap_or_else(|| "removed".into()),
    )
  }

  fn on_config_update(
    &self,
    _envoy: &mut dyn EnvoyBootstrapExtensionConfig,
    config: Option<&[u8]>,
  ) -> bool {
    if config == Some(b"rejected".as_slice()) {
      return false;
    }
    *self.config.lock().unwrap() = config.map(|bytes| {
      std::str::from_utf8(bytes)
        .expect("test payload must be UTF-8")
        .to_owned()
    });
    true
  }
}

struct EcdsExtension;

impl BootstrapExtension for EcdsExtension {}
