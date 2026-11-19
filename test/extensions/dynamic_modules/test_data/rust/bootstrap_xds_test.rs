use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::fmt::Write;
use std::sync::Mutex;

declare_bootstrap_init_functions!(program_init, new_config);

fn program_init() -> bool {
  true
}

fn new_config(
  envoy: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  assert!(envoy.register_admin_handler("/xds", "Test xDS notifications.", true, true));
  envoy.signal_init_complete();
  Some(Box::new(XdsConfig {
    updates: Mutex::new(String::new()),
  }))
}

struct XdsConfig {
  updates: Mutex<String>,
}

impl BootstrapExtensionConfig for XdsConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(XdsExtension)
  }

  fn on_admin_request(
    &self,
    envoy: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    path: &str,
    _body: &[u8],
  ) -> (u32, String) {
    if path.ends_with("?enable") {
      assert!(envoy.set_xds_notifications(true));
    } else if path.ends_with("?disable") {
      assert!(envoy.set_xds_notifications(false));
    }
    (200, self.updates.lock().unwrap().clone())
  }

  fn on_xds_update(
    &self,
    envoy: &mut dyn EnvoyBootstrapExtensionConfig,
    type_url: &str,
    status: XdsUpdateStatus,
    error_details: &str,
  ) {
    if status == XdsUpdateStatus::Rejected {
      assert!(envoy.set_xds_notifications(false));
    }
    if status == XdsUpdateStatus::Unsubscribed {
      assert!(!envoy
        .active_resource_names(ActiveResourceKind::Cluster)
        .iter()
        .any(|name| name == "eds_cluster"));
    }
    writeln!(
      self.updates.lock().unwrap(),
      "{status:?}: {type_url}: {error_details}"
    )
    .unwrap();
  }
}

struct XdsExtension;

impl BootstrapExtension for XdsExtension {}
