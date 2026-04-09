//! Hickory DNS resolver dynamic module.
//!
//! This module implements a DNS resolver using the Hickory DNS library, a pure Rust DNS
//! implementation. It supports standard DNS (UDP/TCP), DNS-over-TLS, DNS-over-HTTPS, and
//! DNSSEC validation. The resolver runs on its own Tokio runtime, delivering results back
//! to Envoy's dispatcher thread via the dynamic module ABI.

use envoy_proxy_dynamic_modules_rust_sdk::*;
use std::fmt::Write;
use std::net::{SocketAddr, ToSocketAddrs};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

fn program_init() -> bool {
  true
}

fn new_dns_resolver_config(
  _name: &str,
  config: &[u8],
  _envoy_dns_resolver_config: Arc<dyn EnvoyDnsResolverConfig>,
) -> Option<Box<dyn DnsResolverConfig>> {
  let config_str = std::str::from_utf8(config).ok()?;
  let config: HickoryConfig = serde_json::from_str(config_str).ok()?;
  Some(Box::new(HickoryDnsResolverConfigImpl { config }))
}

declare_dns_resolver_init_functions!(program_init, new_dns_resolver_config);

#[derive(Debug, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct HickoryConfig {
  #[serde(default)]
  resolvers: Vec<ResolverAddress>,
  #[serde(default)]
  dns_over_tls: Option<DnsOverTlsJsonConfig>,
  #[serde(default)]
  dns_over_https: Option<DnsOverHttpsJsonConfig>,
  #[serde(default)]
  enable_dnssec: bool,
  #[serde(default)]
  cache_size: Option<u32>,
  #[serde(default)]
  num_resolver_threads: Option<u32>,
  #[serde(default)]
  use_system_config: Option<bool>,
  #[serde(default)]
  query_timeout: Option<String>,
  #[serde(default)]
  query_tries: Option<u32>,
}

#[derive(Debug, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct ResolverAddress {
  socket_address: Option<SocketAddressJson>,
}

#[derive(Debug, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct SocketAddressJson {
  address: String,
  port_value: Option<u32>,
}

#[derive(Debug, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct DnsOverTlsJsonConfig {
  #[serde(default)]
  servers: Vec<ResolverAddress>,
  #[serde(default)]
  tls_server_name: String,
}

#[derive(Debug, serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct DnsOverHttpsJsonConfig {
  #[serde(default)]
  server_urls: Vec<String>,
}

impl HickoryConfig {
  fn effective_cache_size(&self) -> usize {
    self.cache_size.unwrap_or(1024) as usize
  }

  fn effective_num_threads(&self) -> usize {
    self.num_resolver_threads.unwrap_or(2).clamp(1, 16) as usize
  }

  fn effective_query_timeout(&self) -> std::time::Duration {
    self
      .query_timeout
      .as_ref()
      .and_then(|s| parse_proto_duration(s))
      .unwrap_or(std::time::Duration::from_secs(5))
  }

  fn effective_query_tries(&self) -> usize {
    self.query_tries.unwrap_or(3).max(1) as usize
  }

  fn should_use_system_config(&self) -> bool {
    match self.use_system_config {
      Some(value) => value,
      None => {
        self.resolvers.is_empty() && self.dns_over_tls.is_none() && self.dns_over_https.is_none()
      },
    }
  }
}

/// Parse a protobuf Duration JSON string (e.g., "5s", "1.500s").
fn parse_proto_duration(s: &str) -> Option<std::time::Duration> {
  let s = s.trim();
  if let Some(stripped) = s.strip_suffix('s') {
    if let Some((whole, frac)) = stripped.split_once('.') {
      let secs: u64 = whole.parse().ok()?;
      let nanos: u32 = format!("{:0<9}", frac)[..9].parse().ok()?;
      Some(std::time::Duration::new(secs, nanos))
    } else {
      let secs: u64 = stripped.parse().ok()?;
      Some(std::time::Duration::from_secs(secs))
    }
  } else {
    None
  }
}

struct HickoryDnsResolverConfigImpl {
  config: HickoryConfig,
}

// SAFETY: The config is immutable after construction and contains only owned data.
unsafe impl Send for HickoryDnsResolverConfigImpl {}
unsafe impl Sync for HickoryDnsResolverConfigImpl {}

impl DnsResolverConfig for HickoryDnsResolverConfigImpl {
  fn new_resolver(
    &self,
    envoy_callback: Arc<dyn EnvoyDnsResolverCallback>,
  ) -> Box<dyn DnsResolverInstance> {
    let resolver = HickoryDnsResolverImpl::new(&self.config, envoy_callback);
    Box::new(resolver)
  }
}

type TokioResolver =
  hickory_resolver::Resolver<hickory_resolver::name_server::TokioConnectionProvider>;

// Compile-time verification that TokioResolver implements Send + Sync.
// This lets the compiler auto-derive Send + Sync for SharedResolverState and
// HickoryDnsResolverImpl, avoiding the need for unsafe impl blocks.
const _: () = {
  fn _assert_send_sync<T: Send + Sync>() {}
  fn _check() {
    _assert_send_sync::<TokioResolver>();
  }
};

/// Shared state accessed by both the resolver and spawned Tokio tasks. Bundled
/// into a single Arc so that each `resolve()` call performs one atomic
/// increment instead of three.
struct SharedResolverState {
  resolver: TokioResolver,
  envoy_callback: Arc<dyn EnvoyDnsResolverCallback>,
  shutting_down: AtomicBool,
}

struct HickoryDnsResolverImpl {
  runtime: Option<tokio::runtime::Runtime>,
  shared: Arc<SharedResolverState>,
}

impl Drop for HickoryDnsResolverImpl {
  fn drop(&mut self) {
    // Signal all spawned tasks to skip the callback. This must happen before
    // shutting down the runtime so that tasks completing during shutdown do not
    // attempt to call into the C++ resolver which is being destroyed.
    self.shared.shutting_down.store(true, Ordering::Release);

    if let Some(rt) = self.runtime.take() {
      rt.shutdown_timeout(std::time::Duration::from_secs(5));
    }
  }
}

impl HickoryDnsResolverImpl {
  fn new(config: &HickoryConfig, envoy_callback: Arc<dyn EnvoyDnsResolverCallback>) -> Self {
    let runtime = tokio::runtime::Builder::new_multi_thread()
      .worker_threads(config.effective_num_threads())
      .thread_name("hickory-dns")
      .enable_all()
      .build()
      .expect("failed to create Tokio runtime for Hickory DNS");

    let resolver = runtime.block_on(async { build_resolver(config) });

    HickoryDnsResolverImpl {
      runtime: Some(runtime),
      shared: Arc::new(SharedResolverState {
        resolver,
        envoy_callback,
        shutting_down: AtomicBool::new(false),
      }),
    }
  }
}

fn build_resolver(config: &HickoryConfig) -> TokioResolver {
  use hickory_resolver::config::*;
  use hickory_resolver::name_server::TokioConnectionProvider;
  use hickory_resolver::proto::xfer::Protocol;

  let mut resolver_config = if config.should_use_system_config() {
    let (sys_config, _) = hickory_resolver::system_conf::read_system_conf()
      .unwrap_or_else(|_| (ResolverConfig::default(), ResolverOpts::default()));
    sys_config
  } else {
    ResolverConfig::new()
  };

  for resolver_addr in &config.resolvers {
    if let Some(ref sa) = resolver_addr.socket_address {
      let port = sa.port_value.unwrap_or(53) as u16;
      if let Ok(ip) = sa.address.parse::<std::net::IpAddr>() {
        let socket_addr = SocketAddr::new(ip, port);
        resolver_config.add_name_server(NameServerConfig::new(socket_addr, Protocol::Udp));
        resolver_config.add_name_server(NameServerConfig::new(socket_addr, Protocol::Tcp));
      }
    }
  }

  if let Some(ref dot_config) = config.dns_over_tls {
    for server in &dot_config.servers {
      if let Some(ref sa) = server.socket_address {
        let port = sa.port_value.unwrap_or(853) as u16;
        if let Ok(ip) = sa.address.parse::<std::net::IpAddr>() {
          let socket_addr = SocketAddr::new(ip, port);
          let mut ns = NameServerConfig::new(socket_addr, Protocol::Tls);
          ns.tls_dns_name = Some(dot_config.tls_server_name.clone());
          resolver_config.add_name_server(ns);
        }
      }
    }
  }

  if let Some(ref doh_config) = config.dns_over_https {
    for url_str in &doh_config.server_urls {
      if let Ok(parsed_url) = url::Url::parse(url_str) {
        let host = match parsed_url.host_str() {
          Some(h) => h,
          None => continue,
        };
        let port = parsed_url.port().unwrap_or(443);
        let path = parsed_url.path();
        let ip = if let Ok(ip) = host.parse::<std::net::IpAddr>() {
          ip
        } else {
          // Resolve the hostname to an IP via a blocking system DNS lookup.
          // This is acceptable during initialization.
          match (host, port).to_socket_addrs() {
            Ok(mut addrs) => match addrs.next() {
              Some(addr) => addr.ip(),
              None => continue,
            },
            Err(_) => continue,
          }
        };
        let socket_addr = SocketAddr::new(ip, port);
        let mut ns = NameServerConfig::new(socket_addr, Protocol::Https);
        ns.tls_dns_name = Some(host.to_string());
        if path != "/" && !path.is_empty() {
          ns.http_endpoint = Some(path.to_string());
        }
        resolver_config.add_name_server(ns);
      }
    }
  }

  let mut opts = ResolverOpts::default();
  opts.timeout = config.effective_query_timeout();
  opts.attempts = config.effective_query_tries();
  opts.cache_size = config.effective_cache_size();
  opts.validate = config.enable_dnssec;

  let provider = TokioConnectionProvider::default();
  let mut builder = hickory_resolver::Resolver::builder_with_config(resolver_config, provider);
  *builder.options_mut() = opts;
  builder.build()
}

impl DnsResolverInstance for HickoryDnsResolverImpl {
  fn resolve(
    &self,
    dns_name: &str,
    lookup_family: DnsLookupFamily,
    query_id: u64,
  ) -> Option<Box<dyn DnsActiveQuery>> {
    let cancelled = Arc::new(AtomicBool::new(false));
    let shared = Arc::clone(&self.shared);
    let dns_name_owned = dns_name.to_string();
    let cancelled_clone = Arc::clone(&cancelled);

    // The runtime is always available during normal operation. It is only taken
    // during Drop, after which resolve() cannot be called.
    let runtime = self.runtime.as_ref().expect("runtime unavailable");

    runtime.spawn(async move {
      let result = perform_lookup(&shared.resolver, &dns_name_owned, lookup_family).await;

      // Check both per-query cancellation and resolver-level shutdown. The shutdown
      // flag prevents calling back into the C++ resolver during destruction.
      if cancelled_clone.load(Ordering::Acquire) || shared.shutting_down.load(Ordering::Acquire) {
        return;
      }

      match result {
        Ok(addresses) => {
          shared.envoy_callback.resolve_complete(
            query_id,
            DnsResolutionStatus::Completed,
            "resolved",
            &addresses,
          );
        },
        Err(details) => {
          shared.envoy_callback.resolve_complete(
            query_id,
            DnsResolutionStatus::Failure,
            &details,
            &[],
          );
        },
      }
    });

    Some(Box::new(HickoryActiveQuery { cancelled }))
  }

  fn reset_networking(&self) {
    self.shared.resolver.clear_cache();
  }
}

async fn perform_lookup(
  resolver: &TokioResolver,
  dns_name: &str,
  lookup_family: DnsLookupFamily,
) -> Result<Vec<DnsAddress>, String> {
  use hickory_resolver::proto::rr::RecordType;

  // If the input is already an IP address, return it directly without DNS lookup.
  // This matches the behavior of getaddrinfo and c-ares resolvers.
  if let Ok(ip) = dns_name.parse::<std::net::IpAddr>() {
    return Ok(resolve_ip_address_directly(ip, lookup_family));
  }

  let need_a = matches!(
    lookup_family,
    DnsLookupFamily::V4Only
      | DnsLookupFamily::Auto
      | DnsLookupFamily::V4Preferred
      | DnsLookupFamily::All
  );
  let need_aaaa = matches!(
    lookup_family,
    DnsLookupFamily::V6Only
      | DnsLookupFamily::Auto
      | DnsLookupFamily::V4Preferred
      | DnsLookupFamily::All
  );

  let mut addresses = Vec::with_capacity(4);
  let mut error_msg: Option<String> = None;

  if need_a && need_aaaa {
    // Issue both lookups concurrently to halve dual-stack query latency.
    let (a_result, aaaa_result) = tokio::join!(
      resolver.lookup(dns_name, RecordType::A),
      resolver.lookup(dns_name, RecordType::AAAA)
    );
    collect_a_records(a_result, &mut addresses, &mut error_msg);
    collect_aaaa_records(aaaa_result, &mut addresses, &mut error_msg);
  } else if need_a {
    collect_a_records(
      resolver.lookup(dns_name, RecordType::A).await,
      &mut addresses,
      &mut error_msg,
    );
  } else {
    collect_aaaa_records(
      resolver.lookup(dns_name, RecordType::AAAA).await,
      &mut addresses,
      &mut error_msg,
    );
  }

  if lookup_family == DnsLookupFamily::V4Preferred {
    addresses.sort_by_key(|a| u8::from(a.address.starts_with('[')));
  }

  if addresses.is_empty() {
    if let Some(err) = error_msg {
      return Err(err);
    }
  }

  Ok(addresses)
}

/// Extract A records from a lookup result into the addresses vector.
fn collect_a_records(
  result: Result<hickory_resolver::lookup::Lookup, hickory_resolver::ResolveError>,
  addresses: &mut Vec<DnsAddress>,
  error_msg: &mut Option<String>,
) {
  match result {
    Ok(response) => {
      for record in response.records() {
        if let Some(a) = record.data().as_a() {
          addresses.push(DnsAddress {
            address: format_ipv4_address(a.0),
            ttl_seconds: record.ttl(),
          });
        }
      }
    },
    Err(e) => append_lookup_error(error_msg, "A", &e),
  }
}

/// Extract AAAA records from a lookup result into the addresses vector.
fn collect_aaaa_records(
  result: Result<hickory_resolver::lookup::Lookup, hickory_resolver::ResolveError>,
  addresses: &mut Vec<DnsAddress>,
  error_msg: &mut Option<String>,
) {
  match result {
    Ok(response) => {
      for record in response.records() {
        if let Some(aaaa) = record.data().as_aaaa() {
          addresses.push(DnsAddress {
            address: format_ipv6_address(aaaa.0),
            ttl_seconds: record.ttl(),
          });
        }
      }
    },
    Err(e) => append_lookup_error(error_msg, "AAAA", &e),
  }
}

fn append_lookup_error(
  error_msg: &mut Option<String>,
  record_type: &str,
  error: &hickory_resolver::ResolveError,
) {
  let msg = format!("{record_type} lookup failed: {error}");
  match error_msg {
    Some(existing) => {
      existing.push_str("; ");
      existing.push_str(&msg);
    },
    None => *error_msg = Some(msg),
  }
}

fn format_ipv4_address(addr: std::net::Ipv4Addr) -> String {
  // Max IPv4 "255.255.255.255:0" = 17 bytes.
  let mut buf = String::with_capacity(21);
  write!(buf, "{addr}:0").unwrap();
  buf
}

fn format_ipv6_address(addr: std::net::Ipv6Addr) -> String {
  // Max IPv6 "[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:0" = 43 bytes.
  let mut buf = String::with_capacity(47);
  write!(buf, "[{addr}]:0").unwrap();
  buf
}

/// Handles the case where the DNS name is already an IP address by returning it
/// directly, respecting the requested lookup family.
fn resolve_ip_address_directly(
  ip: std::net::IpAddr,
  lookup_family: DnsLookupFamily,
) -> Vec<DnsAddress> {
  const SYNTHETIC_TTL: u32 = 60;
  if matches!(
    (&ip, lookup_family),
    (std::net::IpAddr::V4(_), DnsLookupFamily::V6Only)
      | (std::net::IpAddr::V6(_), DnsLookupFamily::V4Only)
  ) {
    return Vec::new();
  }
  let address = match ip {
    std::net::IpAddr::V4(v4) => format_ipv4_address(v4),
    std::net::IpAddr::V6(v6) => format_ipv6_address(v6),
  };
  vec![DnsAddress {
    address,
    ttl_seconds: SYNTHETIC_TTL,
  }]
}

struct HickoryActiveQuery {
  cancelled: Arc<AtomicBool>,
}

impl DnsActiveQuery for HickoryActiveQuery {
  fn cancel(&mut self) {
    self.cancelled.store(true, Ordering::Release);
  }
}
