//! Test dynamic module implementing a TLS certificate selector.

use envoy_proxy_dynamic_modules_rust_sdk::tls_certificate_selector::*;
use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::sync::Arc;

declare_all_init_functions!(
  init,
  tls_certificate_selector: new_tls_certificate_selector_config
);

fn init() -> bool {
  true
}

fn new_tls_certificate_selector_config(
  name: &str,
  config: &[u8],
  is_upstream: bool,
  envoy_config: Arc<dyn EnvoyTlsCertificateSelectorConfig>,
) -> Option<Box<dyn TlsCertificateSelectorConfig>> {
  match (name, is_upstream) {
    ("downstream", false) | ("upstream", true) => Some(Box::new(ServerNameSelectorConfig {
      server_name: config.to_vec(),
    })),
    ("filter_state", true) => Some(Box::new(FilterStateSelectorConfig {
      tenant: config.to_vec(),
    })),
    ("fail", _) => Some(Box::new(FailSelectorConfig)),
    ("dynamic", _) => Some(Box::new(DynamicSelectorConfig::new(
      std::str::from_utf8(config).ok()?,
      envoy_config,
    )?)),
    _ => None,
  }
}

/// Returns whether the certificate is a whole DER encoded SEQUENCE with a two byte length, which
/// every test certificate is.
fn is_der_certificate(certificate: &[u8]) -> bool {
  certificate.len() > 4
    && certificate[..2] == [0x30, 0x82]
    && usize::from(u16::from_be_bytes([certificate[2], certificate[3]])) + 4 == certificate.len()
}

/// Selects the last certificate for the configured server name, and the first one otherwise.
struct ServerNameSelectorConfig {
  server_name: Vec<u8>,
}

impl TlsCertificateSelectorConfig for ServerNameSelectorConfig {
  fn new_selector(&self, certificates: &[&[u8]]) -> Option<Box<dyn TlsCertificateSelector>> {
    if !certificates
      .iter()
      .all(|certificate| is_der_certificate(certificate))
    {
      return None;
    }
    Some(Box::new(ServerNameSelector {
      server_name: self.server_name.clone(),
      last: certificates.len().checked_sub(1)?,
    }))
  }
}

struct ServerNameSelector {
  server_name: Vec<u8>,
  last: usize,
}

impl TlsCertificateSelector for ServerNameSelector {
  fn select(&self, context: &TlsCertificateSelectionContext) -> TlsCertificateSelection {
    match context.get_requested_server_name() {
      Some(server_name) if server_name.as_slice() == self.server_name.as_slice() => {
        TlsCertificateSelection::Configured(self.last)
      },
      _ => TlsCertificateSelection::Configured(0),
    }
  }
}

/// Selects the second certificate when the filter state "tenant" of the connection is the
/// configured tenant, and the first one otherwise.
struct FilterStateSelectorConfig {
  tenant: Vec<u8>,
}

impl TlsCertificateSelectorConfig for FilterStateSelectorConfig {
  fn new_selector(&self, certificates: &[&[u8]]) -> Option<Box<dyn TlsCertificateSelector>> {
    if certificates.len() != 2 {
      return None;
    }
    Some(Box::new(FilterStateSelector {
      tenant: self.tenant.clone(),
    }))
  }
}

struct FilterStateSelector {
  tenant: Vec<u8>,
}

impl TlsCertificateSelector for FilterStateSelector {
  fn select(&self, context: &TlsCertificateSelectionContext) -> TlsCertificateSelection {
    match context.get_filter_state("tenant") {
      Some(tenant) if tenant.as_slice() == self.tenant.as_slice() => {
        TlsCertificateSelection::Configured(1)
      },
      _ => TlsCertificateSelection::Configured(0),
    }
  }
}

/// Fails every selection.
struct FailSelectorConfig;

impl TlsCertificateSelectorConfig for FailSelectorConfig {
  fn new_selector(&self, _certificates: &[&[u8]]) -> Option<Box<dyn TlsCertificateSelector>> {
    Some(Box::new(FailSelector))
  }
}

struct FailSelector;

impl TlsCertificateSelector for FailSelector {
  fn select(&self, _context: &TlsCertificateSelectionContext) -> TlsCertificateSelection {
    TlsCertificateSelection::Failed
  }
}

/// A dynamic certificate that the module knows.
struct Certificate {
  name: String,
  certificate_chain: Vec<u8>,
  private_key: Vec<u8>,
  ocsp_staple: Vec<u8>,
}

impl Certificate {
  fn provide(&self, envoy_config: &dyn EnvoyTlsCertificateSelectorConfig) -> bool {
    envoy_config.set_certificate(
      &self.name,
      &self.certificate_chain,
      &self.private_key,
      &self.ocsp_staple,
    )
  }
}

/// Uses the dynamic certificate named by the server name, and lets Envoy select the certificate
/// for a server name with the "www." prefix. Each line of the config is a mode, a name, a
/// certificate path, a private key path, and an optional OCSP response path. A "prefetch"
/// certificate is provided when the config is created, a "sync" certificate is provided when it is
/// requested, and a "fetch" certificate is provided from another thread when it is requested. A
/// "hold" certificate is not provided when it is requested, but when a name with the "release."
/// prefix is requested, which is then declined. Other names are declined.
struct DynamicSelectorConfig {
  envoy_config: Arc<dyn EnvoyTlsCertificateSelectorConfig>,
  scheduler: Arc<dyn EnvoyTlsCertificateSelectorConfigScheduler>,
  synced: Vec<Certificate>,
  fetched: Vec<Certificate>,
  held: Vec<Certificate>,
  selected: EnvoyCounterVecId,
  requested: EnvoyCounterId,
}

impl DynamicSelectorConfig {
  fn new(config: &str, envoy_config: Arc<dyn EnvoyTlsCertificateSelectorConfig>) -> Option<Self> {
    let mut synced = Vec::new();
    let mut fetched = Vec::new();
    let mut held = Vec::new();
    for line in config.lines() {
      let fields: Vec<&str> = line.split_whitespace().collect();
      let [mode, name, certificate_path, private_key_path, ocsp_staple_path @ ..] =
        fields.as_slice()
      else {
        return None;
      };
      let ocsp_staple = match ocsp_staple_path {
        [] => Vec::new(),
        [path] => std::fs::read(path).ok()?,
        _ => return None,
      };
      let certificate = Certificate {
        name: name.to_string(),
        certificate_chain: std::fs::read(certificate_path).ok()?,
        private_key: std::fs::read(private_key_path).ok()?,
        ocsp_staple,
      };
      match *mode {
        "prefetch" => {
          if !certificate.provide(envoy_config.as_ref()) {
            return None;
          }
        },
        "sync" => synced.push(certificate),
        "fetch" => fetched.push(certificate),
        "hold" => held.push(certificate),
        _ => return None,
      }
    }
    Some(Self {
      selected: envoy_config
        .define_counter_vec("selected", &["outcome"])
        .ok()?,
      requested: envoy_config.define_counter("requested").ok()?,
      scheduler: Arc::from(envoy_config.new_scheduler()),
      envoy_config,
      synced,
      fetched,
      held,
    })
  }
}

impl TlsCertificateSelectorConfig for DynamicSelectorConfig {
  fn new_selector(&self, _certificates: &[&[u8]]) -> Option<Box<dyn TlsCertificateSelector>> {
    Some(Box::new(DynamicSelector {
      envoy_config: self.envoy_config.clone(),
      selected: self.selected,
    }))
  }

  fn on_certificate_requested(
    &self,
    envoy_config: &dyn EnvoyTlsCertificateSelectorConfig,
    name: &str,
  ) {
    let _ = self.envoy_config.increment_counter(self.requested, 1);
    let is_named = |certificate: &Certificate| certificate.name == name;
    if name.starts_with("release.") {
      for certificate in &self.held {
        certificate.provide(envoy_config);
      }
      envoy_config.remove_certificate(name);
    } else if let Some(certificate) = self.synced.iter().find(|c| is_named(c)) {
      certificate.provide(envoy_config);
    } else if let Some(index) = self.fetched.iter().position(is_named) {
      // Fetch the certificate off the main thread, and provide it once back on the main thread.
      let scheduler = self.scheduler.clone();
      std::thread::spawn(move || scheduler.commit(index as u64));
    } else if !self.held.iter().any(is_named) {
      envoy_config.remove_certificate(name);
    }
  }

  fn on_scheduled(&self, envoy_config: &dyn EnvoyTlsCertificateSelectorConfig, event_id: u64) {
    self.fetched[event_id as usize].provide(envoy_config);
  }
}

struct DynamicSelector {
  envoy_config: Arc<dyn EnvoyTlsCertificateSelectorConfig>,
  selected: EnvoyCounterVecId,
}

impl TlsCertificateSelector for DynamicSelector {
  fn select(&self, context: &TlsCertificateSelectionContext) -> TlsCertificateSelection {
    let server_name = context
      .get_requested_server_name()
      .map(|server_name| String::from_utf8_lossy(server_name.as_slice()).into_owned());
    let (selection, outcome) = match server_name {
      Some(server_name) if server_name.starts_with("www.") => {
        (TlsCertificateSelection::Default, "default")
      },
      Some(server_name) => (TlsCertificateSelection::Dynamic(server_name), "dynamic"),
      None => (TlsCertificateSelection::Failed, "failed"),
    };
    let _ = self
      .envoy_config
      .increment_counter_vec(self.selected, &[outcome], 1);
    selection
  }
}
