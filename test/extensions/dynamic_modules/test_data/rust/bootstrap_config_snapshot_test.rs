use envoy_proxy_dynamic_modules_rust_sdk::*;

declare_bootstrap_init_functions!(program_init, new_config);

fn program_init() -> bool {
  true
}

fn new_config(
  envoy: &mut dyn EnvoyBootstrapExtensionConfig,
  _name: &str,
  _config: &[u8],
) -> Option<Box<dyn BootstrapExtensionConfig>> {
  assert!(envoy.register_admin_handler("/snapshot", "Collect configuration.", true, false));
  envoy.signal_init_complete();
  Some(Box::new(SnapshotConfig))
}

struct SnapshotConfig;

impl BootstrapExtensionConfig for SnapshotConfig {
  fn new_bootstrap_extension(
    &self,
    _envoy: &mut dyn EnvoyBootstrapExtension,
  ) -> Box<dyn BootstrapExtension> {
    Box::new(SnapshotExtension)
  }

  fn on_admin_request(
    &self,
    envoy: &mut dyn EnvoyBootstrapExtensionConfig,
    _method: &str,
    path: &str,
    _body: &[u8],
  ) -> (u32, String) {
    let name_regex = path.ends_with("?filtered").then_some("cluster_0");
    let snapshot = envoy
      .get_config_snapshot(
        &[
          ConfigSnapshotProvider::Listeners,
          ConfigSnapshotProvider::Clusters,
          ConfigSnapshotProvider::Endpoints,
        ],
        name_regex,
      )
      .expect("configuration collection should succeed");
    let json = std::thread::spawn(move || snapshot.into_json())
      .join()
      .unwrap()
      .expect("configuration encoding should succeed");
    (200, String::from_utf8(json).unwrap())
  }
}

struct SnapshotExtension;

impl BootstrapExtension for SnapshotExtension {}
