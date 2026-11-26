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
  config: Mutex<Option<AppliedConfig>>,
}

struct AppliedConfig {
  value: String,
  version: String,
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
      && !envoy.subscribe_ecds(EcdsSubscription {
        name: "test-config",
        source_json: std::str::from_utf8(body).expect("test source must be UTF-8"),
      })
    {
      return (400, "subscription failed".into());
    }
    let config = self.config.lock().unwrap();
    let value = config.as_ref().map(|config| {
      if path.ends_with("?version") {
        config.version.as_str()
      } else {
        config.value.as_str()
      }
    });
    (200, value.unwrap_or("removed").to_owned())
  }

  fn on_ecds_update(
    &self,
    envoy: &mut dyn EnvoyBootstrapExtensionConfig,
    update: EcdsUpdate<'_>,
  ) -> bool {
    assert!(envoy
      .active_resource_names(ActiveResourceKind::Cluster)
      .iter()
      .any(|name| name == "cluster_0"));
    let next = match update {
      EcdsUpdate::Config {
        name,
        version,
        type_url,
        encoding,
        config,
      } => {
        assert_eq!(name, "test-config");
        assert_eq!(type_url, "type.googleapis.com/google.protobuf.StringValue");
        assert_eq!(encoding, EcdsConfigEncoding::Bytes);
        if config == b"rejected" {
          return false;
        }
        Some(AppliedConfig {
          value: std::str::from_utf8(config)
            .expect("test payload must be UTF-8")
            .to_owned(),
          version: version.to_owned(),
        })
      },
      EcdsUpdate::Removed { name } => {
        assert_eq!(name, "test-config");
        None
      },
    };
    let mut config = self.config.lock().unwrap();
    if next.is_none()
      && config
        .as_ref()
        .is_some_and(|config| config.value == "keep-on-removal")
    {
      return false;
    }
    *config = next;
    true
  }
}

struct EcdsExtension;

impl BootstrapExtension for EcdsExtension {}
