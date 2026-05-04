//! End-to-end test for the reverse tunnel reporter ABI hooks against a
//! locally-built `envoy-static` binary.
//!
//! # Setup
//!
//! Two envoys on localhost share the same binary:
//!
//! - **Upstream** (acceptor): runs `reverse_tunnel.upstream_socket_interface`
//!   with `reporter_config` pointing at `DynamicModuleReverseTunnelReporter`,
//!   which loads `libreverse_tunnel_reporter_test.so` and dispatches lifecycle
//!   events through the five ABI hooks. Terminates tunnels on a
//!   `reverse_tunnel` network-filter listener.
//! - **Downstream** (initiator): runs
//!   `reverse_tunnel.downstream_socket_interface` and has a listener whose
//!   address is `rc://<node>:<cluster>:<tenant>@tunnel_cluster:1`, resolved
//!   via `envoy.resolvers.reverse_connection`.
//!
//! # Required env vars
//!
//! - `ENVOY_BIN` — path to the `envoy-static` binary.
//! - `ENVOY_DYNAMIC_MODULES_SEARCH_PATH` — directory containing
//!   `libreverse_tunnel_reporter_test.so`.
//!
//! Missing/invalid → tests skip with an informative message.
//!
//! # Assertions
//!
//! Handshake completion is observed via the upstream admin `/stats`
//! endpoint:
//!
//! - `reverse_tunnel.handshake.accepted >= --concurrency` (one
//!   handshake per worker)
//! - `reverse_tunnel_reporter_acceptor.nodes.<id> >= 1`
//! - `reverse_tunnel_reporter_acceptor.clusters.<id> >= 1`
//!
//! With handshakes confirmed by stats, the upstream stderr is scraped for
//! `.so` log lines that prove the factory delegated into our module:
//!
//! - `reverse_tunnel_reporter: on_reverse_tunnel_server_initialized`
//! - `reverse_tunnel_reporter: on_reverse_tunnel_connected node=N cluster=C tenant=T`
//! - `reverse_tunnel_reporter: on_reverse_tunnel_disconnected node=N cluster=C`

use std::io::Write;
use std::net::TcpListener;
use std::process::{Child, Command, Stdio};
use std::time::{Duration, Instant};

fn envoy_bin_or_skip() -> Option<(String, String)> {
  let bin = std::env::var("ENVOY_BIN").ok().filter(|p| !p.is_empty());
  let search = std::env::var("ENVOY_DYNAMIC_MODULES_SEARCH_PATH")
    .ok()
    .filter(|p| !p.is_empty());
  match (bin, search) {
    (Some(b), Some(s))
      if std::path::Path::new(&b).exists() && std::path::Path::new(&s).is_dir() =>
    {
      Some((b, s))
    },
    _ => {
      eprintln!("SKIP: set ENVOY_BIN + ENVOY_DYNAMIC_MODULES_SEARCH_PATH to run this e2e test");
      None
    },
  }
}

/// Bind-then-drop to reserve an ephemeral port.
fn free_port() -> u16 {
  let l = TcpListener::bind("127.0.0.1:0").expect("bind :0");
  let p = l.local_addr().unwrap().port();
  drop(l);
  p
}

fn wait_admin_ready(admin_port: u16, timeout: Duration) -> Result<(), String> {
  let deadline = Instant::now() + timeout;
  while Instant::now() < deadline {
    if let Ok(mut s) = std::net::TcpStream::connect(("127.0.0.1", admin_port)) {
      use std::io::Read;
      let req = "GET /ready HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
      if s.write_all(req.as_bytes()).is_ok() {
        s.set_read_timeout(Some(Duration::from_secs(2))).ok();
        let mut buf = String::new();
        let _ = s.read_to_string(&mut buf);
        if buf.contains("200 OK") && buf.contains("LIVE") {
          return Ok(());
        }
      }
    }
    std::thread::sleep(Duration::from_millis(100));
  }
  Err(format!(
    "admin port {admin_port} never returned LIVE within {timeout:?}"
  ))
}

fn admin_get(admin_port: u16, path: &str) -> Result<String, String> {
  use std::io::Read;
  let mut s = std::net::TcpStream::connect(("127.0.0.1", admin_port))
    .map_err(|e| format!("connect admin {admin_port}: {e}"))?;
  let req = format!("GET {path} HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
  s.write_all(req.as_bytes())
    .map_err(|e| format!("write admin: {e}"))?;
  s.set_read_timeout(Some(Duration::from_secs(2))).ok();
  let mut buf = String::new();
  let _ = s.read_to_string(&mut buf);
  Ok(buf)
}

fn parse_stat(body: &str, name: &str) -> Option<u64> {
  body
    .lines()
    .find_map(|l| l.strip_prefix(&format!("{name}: ")))
    .and_then(|v| v.trim().parse::<u64>().ok())
}

/// Poll `/stats?filter=<name>` until the stat reaches `min_value` or
/// timeout. `hidden=include` is required for cross-worker gauges like
/// `<prefix>.nodes.<id>` and `<prefix>.clusters.<id>`, which envoy
/// registers with `Stats::Gauge::ImportMode::HiddenAccumulate`.
fn wait_for_stat_ge(
  admin_port: u16,
  name: &str,
  min_value: u64,
  timeout: Duration,
) -> Result<u64, String> {
  let deadline = Instant::now() + timeout;
  let mut last = 0u64;
  while Instant::now() < deadline {
    if let Ok(body) = admin_get(admin_port, &format!("/stats?hidden=include&filter={name}")) {
      if let Some(v) = parse_stat(&body, name) {
        last = v;
        if v >= min_value {
          return Ok(v);
        }
      }
    }
    std::thread::sleep(Duration::from_millis(100));
  }
  Err(format!(
    "stat {name} did not reach {min_value} within {timeout:?} (last seen: {last})"
  ))
}

fn wait_for_log_line(
  stderr_path: &std::path::Path,
  needle: &str,
  timeout: Duration,
) -> Result<String, String> {
  let deadline = Instant::now() + timeout;
  while Instant::now() < deadline {
    if let Ok(contents) = std::fs::read_to_string(stderr_path) {
      if contents.contains(needle) {
        return Ok(contents);
      }
    }
    std::thread::sleep(Duration::from_millis(100));
  }
  let captured = std::fs::read_to_string(stderr_path).unwrap_or_default();
  Err(format!(
    "did not observe '{needle}' within {timeout:?}; stderr tail:\n{}",
    captured
      .lines()
      .rev()
      .take(40)
      .collect::<Vec<_>>()
      .into_iter()
      .rev()
      .collect::<Vec<_>>()
      .join("\n")
  ))
}

struct EnvoyProcess {
  child: Child,
  stderr_path: std::path::PathBuf,
}

impl Drop for EnvoyProcess {
  fn drop(&mut self) {
    let _ = self.child.kill();
    let _ = self.child.wait();
  }
}

fn spawn_envoy(
  envoy_bin: &str,
  yaml: &str,
  admin_port: u16,
  label: &str,
  search_path: Option<&str>,
) -> EnvoyProcess {
  let yaml_path = std::env::temp_dir().join(format!("rt_e2e_{label}_{admin_port}.yaml"));
  std::fs::write(&yaml_path, yaml.as_bytes()).expect("write yaml");
  let stderr_path = std::env::temp_dir().join(format!("rt_e2e_{label}_{admin_port}.err"));
  let stderr_file = std::fs::File::create(&stderr_path).expect("create stderr");

  // Pin concurrency so per-worker tunnel count is deterministic.
  let mut cmd = Command::new(envoy_bin);
  cmd
    .arg("--config-path")
    .arg(&yaml_path)
    .arg("--log-level")
    .arg("info")
    .arg("--concurrency")
    .arg(ENVOY_CONCURRENCY.to_string())
    .arg("--base-id")
    .arg(admin_port.to_string())
    .stdout(Stdio::null())
    .stderr(stderr_file);
  if let Some(p) = search_path {
    cmd.env("ENVOY_DYNAMIC_MODULES_SEARCH_PATH", p);
  }
  let child = cmd.spawn().expect("spawn envoy");
  EnvoyProcess { child, stderr_path }
}

const ENVOY_CONCURRENCY: u64 = 3;

fn upstream_yaml(admin_port: u16, tunnel_port: u16) -> String {
  format!(
    r#"
admin:
  address:
    socket_address: {{ address: 127.0.0.1, port_value: {admin_port} }}

bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.upstream_socket_interface
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.upstream_socket_interface.v3.UpstreamReverseConnectionSocketInterface"
      stat_prefix: reverse_tunnel_reporter_acceptor
      enable_detailed_stats: true
      reporter_config:
        name: "envoy.extensions.reverse_tunnel.reporting_service.dynamic_modules"
        typed_config:
          "@type": "type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.reporter.dynamic_modules.v3.DynamicModuleReverseTunnelReporter"
          dynamic_module_config:
            name: reverse_tunnel_reporter_test

static_resources:
  listeners:
    - name: tunnel_listener
      address:
        socket_address: {{ address: 127.0.0.1, port_value: {tunnel_port} }}
      filter_chains:
        - filters:
            - name: envoy.filters.network.reverse_tunnel
              typed_config:
                "@type": "type.googleapis.com/envoy.extensions.filters.network.reverse_tunnel.v3.ReverseTunnel"
                ping_interval: 60s
                auto_close_connections: false
                request_path: /reverse_connections/request
                request_method: GET
"#
  )
}

fn downstream_yaml(
  admin_port: u16,
  tunnel_port: u16,
  node_id: &str,
  cluster_id: &str,
  tenant_id: &str,
) -> String {
  format!(
    r#"
admin:
  address:
    socket_address: {{ address: 127.0.0.1, port_value: {admin_port} }}

bootstrap_extensions:
  - name: envoy.bootstrap.reverse_tunnel.downstream_socket_interface
    typed_config:
      "@type": "type.googleapis.com/envoy.extensions.bootstrap.reverse_tunnel.downstream_socket_interface.v3.DownstreamReverseConnectionSocketInterface"
      stat_prefix: rt_initiator
      enable_detailed_stats: true

static_resources:
  listeners:
    - name: reverse_conn_listener
      listener_filters_timeout: 0s
      address:
        socket_address:
          address: "rc://{node_id}:{cluster_id}:{tenant_id}@tunnel_cluster:1"
          port_value: 0
          resolver_name: envoy.resolvers.reverse_connection
      filter_chains:
        - filters:
            - name: envoy.filters.network.tcp_proxy
              typed_config:
                "@type": "type.googleapis.com/envoy.extensions.filters.network.tcp_proxy.v3.TcpProxy"
                stat_prefix: initiator_tcp
                cluster: tunnel_cluster

  clusters:
    - name: tunnel_cluster
      type: STATIC
      connect_timeout: 5s
      load_assignment:
        cluster_name: tunnel_cluster
        endpoints:
          - lb_endpoints:
              - endpoint:
                  address:
                    socket_address: {{ address: 127.0.0.1, port_value: {tunnel_port} }}
"#
  )
}

#[test]
fn two_envoy_reverse_tunnel_reporter_delegation() {
  let Some((envoy, search)) = envoy_bin_or_skip() else {
    return;
  };

  let up_admin = free_port();
  let down_admin = free_port();
  let tunnel_port = free_port();

  let upstream = spawn_envoy(
    &envoy,
    &upstream_yaml(up_admin, tunnel_port),
    up_admin,
    "upstream",
    Some(&search),
  );
  wait_admin_ready(up_admin, Duration::from_secs(20)).expect("upstream ready");

  // server_initialized fires during onServerInitialized — before any
  // socket activity — so it's the cheapest proof the .so is wired.
  wait_for_log_line(
    &upstream.stderr_path,
    "reverse_tunnel_reporter: on_reverse_tunnel_server_initialized",
    Duration::from_secs(10),
  )
  .expect("server_initialized hook did not fire on upstream");

  let downstream = spawn_envoy(
    &envoy,
    &downstream_yaml(
      down_admin,
      tunnel_port,
      "edge-node-1",
      "edge-cluster-1",
      "tenant-1",
    ),
    down_admin,
    "downstream",
    None,
  );
  wait_admin_ready(down_admin, Duration::from_secs(20)).expect("downstream ready");

  // Stats-gated handshake assertion.
  wait_for_stat_ge(
    up_admin,
    "reverse_tunnel.handshake.accepted",
    ENVOY_CONCURRENCY,
    Duration::from_secs(30),
  )
  .expect("handshake.accepted never reached concurrency on upstream");
  wait_for_stat_ge(
    up_admin,
    "reverse_tunnel_reporter_acceptor.nodes.edge-node-1",
    1,
    Duration::from_secs(10),
  )
  .expect("nodes.edge-node-1 gauge never appeared on upstream");
  wait_for_stat_ge(
    up_admin,
    "reverse_tunnel_reporter_acceptor.clusters.edge-cluster-1",
    1,
    Duration::from_secs(10),
  )
  .expect("clusters.edge-cluster-1 gauge never appeared on upstream");

  // .so log lines: prove the factory delegated into our module.
  let connected_log = wait_for_log_line(
    &upstream.stderr_path,
    "reverse_tunnel_reporter: on_reverse_tunnel_connected node=edge-node-1",
    Duration::from_secs(5),
  )
  .expect("connected hook never fired on upstream");
  assert!(
    connected_log.contains("cluster=edge-cluster-1") && connected_log.contains("tenant=tenant-1"),
    "connected log missing cluster/tenant id:\n{}",
    connected_log
      .lines()
      .rev()
      .take(5)
      .collect::<Vec<_>>()
      .join("\n")
  );

  // Tear down downstream; acceptor should fire on_disconnected.
  drop(downstream);

  wait_for_log_line(
    &upstream.stderr_path,
    "reverse_tunnel_reporter: on_reverse_tunnel_disconnected node=edge-node-1",
    Duration::from_secs(30),
  )
  .expect("disconnected hook never fired on upstream");

  drop(upstream);
}
