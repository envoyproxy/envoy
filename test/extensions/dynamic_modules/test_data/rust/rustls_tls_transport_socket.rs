// A Rustls-based TLS transport socket implementation.
// This module implements a full TLS transport socket using rustls as an alternative to BoringSSL,
// demonstrating TLS handshaking, encryption/decryption, and certificate validation.

use envoy_proxy_dynamic_modules_rust_sdk::transport_socket::*;
use envoy_proxy_dynamic_modules_rust_sdk::{abi, *};
use rustls::pki_types::{CertificateDer, DnsName, PrivateKeyDer, ServerName};
use rustls::{ClientConfig, ClientConnection, RootCertStore, ServerConfig, ServerConnection};
use std::fs;
use std::io::{Cursor, Read, Write};
use std::sync::Arc;

// Declare init functions using the SDK macro.
declare_transport_socket_init_functions!(init, new_rustls_tls_config);

/// Program initialization.
///
/// This is called once when the dynamic module is loaded.
fn init() -> bool {
  eprintln!("[RUSTLS] Module initialized");
  true
}

/// Create transport socket configuration.
///
/// This is called when a new transport socket configuration is created in Envoy.
fn new_rustls_tls_config<EC: EnvoyTransportSocketConfig, TS: EnvoyTransportSocket>(
  _envoy_socket_config: &mut EC,
  name: &str,
  config: &[u8],
  is_upstream: bool,
) -> Option<Box<dyn TransportSocketConfig<TS>>> {
  eprintln!(
    "[RUSTLS CONFIG] Creating config: name={}, is_upstream={}, config_len={}",
    name,
    is_upstream,
    config.len()
  );

  // Parse config JSON.
  let config_str = std::str::from_utf8(config).unwrap_or("{}");
  eprintln!("[RUSTLS CONFIG] Config string: {:?}", config_str);

  // The config might be wrapped in protobuf types, extract the inner value.
  let inner_config = extract_wrapped_config(config_str);
  eprintln!("[RUSTLS CONFIG] Inner config: {}", inner_config);

  // Extract paths from config JSON.
  let cert_path = extract_json_field(&inner_config, "cert_path");
  let key_path = extract_json_field(&inner_config, "key_path");
  let ca_path = extract_json_field(&inner_config, "ca_path");
  let sni = extract_json_field(&inner_config, "sni");

  eprintln!(
    "[RUSTLS CONFIG] Extracted paths - cert: {:?}, key: {:?}, ca: {:?}, sni: {:?}",
    cert_path, key_path, ca_path, sni
  );

  let alpn_protocols = vec!["h2".to_string(), "http/1.1".to_string()];

  // Create rustls configuration.
  let (client_config, server_config) = if is_upstream {
    // Create client configuration.
    let root_store = if let Some(ref ca_path) = ca_path {
      match load_ca_from_file(ca_path) {
        Ok(store) => {
          eprintln!("[RUSTLS CONFIG] Loaded CA from: {}", ca_path);
          store
        },
        Err(e) => {
          eprintln!(
            "[RUSTLS CONFIG] Failed to load CA: {}, using webpki-roots",
            e
          );
          let mut store = RootCertStore::empty();
          store.extend(webpki_roots::TLS_SERVER_ROOTS.iter().cloned());
          store
        },
      }
    } else {
      eprintln!("[RUSTLS CONFIG] No CA path provided, using webpki-roots");
      let mut store = RootCertStore::empty();
      store.extend(webpki_roots::TLS_SERVER_ROOTS.iter().cloned());
      store
    };

    let mut config = if let (Some(cert_path), Some(key_path)) = (&cert_path, &key_path) {
      match (load_cert_from_file(cert_path), load_key_from_file(key_path)) {
        (Ok(certs), Ok(key)) => {
          eprintln!("[RUSTLS CONFIG] Loaded client cert and key");
          ClientConfig::builder()
            .with_root_certificates(root_store.clone())
            .with_client_auth_cert(certs, key)
            .unwrap_or_else(|_| {
              eprintln!("[RUSTLS CONFIG] Client cert error, using no auth");
              ClientConfig::builder()
                .with_root_certificates(root_store.clone())
                .with_no_client_auth()
            })
        },
        _ => ClientConfig::builder()
          .with_root_certificates(root_store.clone())
          .with_no_client_auth(),
      }
    } else {
      ClientConfig::builder()
        .with_root_certificates(root_store)
        .with_no_client_auth()
    };

    // Add ALPN protocols.
    config.alpn_protocols = alpn_protocols
      .iter()
      .map(|p| p.as_bytes().to_vec())
      .collect();

    eprintln!(
      "[RUSTLS CONFIG] Created client config with ALPN: {:?}",
      alpn_protocols
    );
    (Some(Arc::new(config)), None)
  } else {
    // Create server configuration.
    if let (Some(cert_path), Some(key_path)) = (&cert_path, &key_path) {
      match (load_cert_from_file(cert_path), load_key_from_file(key_path)) {
        (Ok(certs), Ok(key)) => {
          match ServerConfig::builder()
            .with_no_client_auth()
            .with_single_cert(certs, key)
          {
            Ok(mut config) => {
              config.alpn_protocols = alpn_protocols
                .iter()
                .map(|p| p.as_bytes().to_vec())
                .collect();
              eprintln!(
                "[RUSTLS CONFIG] Created server config with ALPN: {:?}",
                alpn_protocols
              );
              (None, Some(Arc::new(config)))
            },
            Err(e) => {
              eprintln!("[RUSTLS CONFIG] Failed to create server config: {}", e);
              return None;
            },
          }
        },
        (Err(_e), _) | (_, Err(_e)) => {
          eprintln!("[RUSTLS CONFIG] Failed to load certificates");
          return None;
        },
      }
    } else {
      eprintln!("[RUSTLS CONFIG] Server requires cert_path and key_path");
      return None;
    }
  };

  Some(Box::new(RustlsTlsConfig {
    client_config,
    server_config,
    sni,
    alpn_protocols,
    is_upstream,
  }))
}

/// Configuration for a Rustls-based TLS transport socket.
///
/// This structure holds the TLS configuration for either client or server connections,
/// including certificate chains, private keys, root CAs, and ALPN settings.
struct RustlsTlsConfig {
  /// Client-side TLS configuration (for upstream connections).
  client_config: Option<Arc<ClientConfig>>,
  /// Server-side TLS configuration (for downstream connections).
  server_config: Option<Arc<ServerConfig>>,
  /// Server Name Indication for TLS handshake.
  sni: Option<String>,
  /// Application-Layer Protocol Negotiation protocols.
  #[allow(dead_code)] // Kept for future use
  alpn_protocols: Vec<String>,
  /// True if this is an upstream (client) socket, false for downstream (server).
  is_upstream: bool,
}

impl Drop for RustlsTlsConfig {
  fn drop(&mut self) {
    // Use std::mem::take to move out the Arc and immediately drop it.
    // This ensures the Arc reference count is decremented immediately,
    // not deferred until after the struct is dropped.
    if let Some(config) = std::mem::take(&mut self.client_config) {
      drop(config);
    }
    if let Some(config) = std::mem::take(&mut self.server_config) {
      drop(config);
    }
    // Clear all string allocations to free memory.
    self.sni = None;
    self.alpn_protocols.clear();
    self.alpn_protocols.shrink_to_fit();
  }
}

impl<TS: EnvoyTransportSocket> TransportSocketConfig<TS> for RustlsTlsConfig {
  /// Create a new transport socket instance for each connection.
  fn new_transport_socket(&mut self, _envoy: &mut TS) -> Box<dyn TransportSocket<TS>> {
    let connection = if self.is_upstream {
      // Create client connection.
      if let Some(ref client_config) = self.client_config {
        // Use SNI from config, defaulting to "foo.lyft.com" for test certs.
        let sni_str = self.sni.as_deref().unwrap_or("foo.lyft.com");
        let server_name = match DnsName::try_from(sni_str) {
          Ok(dns_name) => ServerName::DnsName(dns_name.to_owned()),
          Err(_) => {
            // Fallback to default SNI
            let fallback = DnsName::try_from("foo.lyft.com").unwrap();
            ServerName::DnsName(fallback.to_owned())
          },
        };

        match ClientConnection::new(client_config.clone(), server_name) {
          Ok(conn) => {
            eprintln!(
              "[RUSTLS] Created client connection with SNI: {:?}",
              self.sni
            );
            TlsConnection::Client(conn)
          },
          Err(e) => {
            eprintln!("[RUSTLS] Failed to create client connection: {}", e);
            TlsConnection::Closed
          },
        }
      } else {
        eprintln!("[RUSTLS] Client config not available");
        TlsConnection::Closed
      }
    } else {
      // Create server connection.
      if let Some(ref server_config) = self.server_config {
        match ServerConnection::new(server_config.clone()) {
          Ok(conn) => {
            eprintln!("[RUSTLS] Created server connection");
            TlsConnection::Server(conn)
          },
          Err(e) => {
            eprintln!("[RUSTLS] Failed to create server connection: {}", e);
            TlsConnection::Closed
          },
        }
      } else {
        eprintln!("[RUSTLS] Server config not available");
        TlsConnection::Closed
      }
    };

    Box::new(RustlsTlsSocket {
      connection,
      handshake_complete: false,
      ssl_info: None,
      sni: self.sni.clone(),
      write_buffer: Vec::new(),
    })
  }
}

/// TLS connection state wrapper.
///
/// This enum wraps the underlying Rustls connection state.
enum TlsConnection {
  /// Client-side TLS connection (upstream).
  Client(ClientConnection),
  /// Server-side TLS connection (downstream).
  Server(ServerConnection),
  /// Connection has been closed.
  Closed,
}

/// Per-connection TLS socket instance.
///
/// This structure represents a single TLS connection instance and maintains the
/// Rustls connection state, handshake status, and negotiated parameters.
struct RustlsTlsSocket {
  /// The underlying Rustls connection state.
  connection: TlsConnection,
  /// Whether the TLS handshake has completed successfully.
  handshake_complete: bool,
  /// SSL connection information (populated after handshake).
  ssl_info: Option<SslConnectionInfo>,
  /// SNI configured for this socket.
  sni: Option<String>,
  /// Buffer for writes that occur before handshake completion.
  write_buffer: Vec<u8>,
}

impl Drop for RustlsTlsSocket {
  fn drop(&mut self) {
    // First, send close_notify if possible.
    match &mut self.connection {
      TlsConnection::Client(conn) => {
        let _ = conn.send_close_notify();
      },
      TlsConnection::Server(conn) => {
        let _ = conn.send_close_notify();
      },
      TlsConnection::Closed => {},
    }

    // Use std::mem::replace to immediately drop the old connection.
    let old_conn = std::mem::replace(&mut self.connection, TlsConnection::Closed);
    drop(old_conn); // Explicitly drop to ensure immediate cleanup

    // Clear and shrink the write buffer
    self.write_buffer.clear();
    self.write_buffer.shrink_to_fit();
    let _ = std::mem::take(&mut self.write_buffer); // Force deallocation

    // Clear SSL info if present - force immediate deallocation
    if let Some(mut info) = std::mem::take(&mut self.ssl_info) {
      // Clear the peer certificates vector to free memory
      info.peer_certificates.clear();
      info.peer_certificates.shrink_to_fit();
      drop(info);
    }
    self.sni = None;
  }
}

impl<TS: EnvoyTransportSocket> TransportSocket<TS> for RustlsTlsSocket {
  /// Called when the connection is established.
  fn on_connected(&mut self, envoy: &mut TS) {
    // Exercise various log levels for ABI coverage.
    envoy.log(LogLevel::Trace, "[RUSTLS] Trace: Connection established");
    envoy.log(
      LogLevel::Debug,
      "[RUSTLS] Debug: Socket connected, initiating handshake",
    );
    envoy.log(LogLevel::Info, "[RUSTLS] Info: Starting TLS handshake");
    envoy.log(LogLevel::Warn, "[RUSTLS] Warn: Test warning message");
    envoy.log(LogLevel::Error, "[RUSTLS] Error: Test error message");
    envoy.log(
      LogLevel::Critical,
      "[RUSTLS] Critical: Test critical message",
    );

    // Exercise get_fd for ABI coverage.
    let fd = envoy.get_fd();
    if fd >= 0 {
      envoy.log(LogLevel::Debug, "[RUSTLS] Got valid file descriptor");
    }

    // Start TLS handshake and write Client/ServerHello if needed.
    self.flush_tls_writes(envoy);
    eprintln!("[RUSTLS] onConnected complete, handshake initiated");
  }

  /// Read data from the connection.
  ///
  /// This function performs the complete TLS read flow:
  /// 1. Reads encrypted TLS records from the network
  /// 2. Feeds the encrypted data to Rustls
  /// 3. Processes TLS records (performs decryption and handshake state machine)
  /// 4. Extracts decrypted application data
  /// 5. Handles handshake completion and parameter negotiation
  fn do_read(&mut self, envoy: &mut TS, buffer: &mut [u8]) -> (u64, bool, bool) {
    // Exercise should_drain_read_buffer for ABI coverage.
    let should_drain = envoy.should_drain_read_buffer();
    if should_drain {
      envoy.log(LogLevel::Trace, "[RUSTLS] Buffer should be drained");
    }

    // Read encrypted data from network.
    let mut tls_buffer = vec![0u8; 16384];
    match envoy.read_from_io_handle(&mut tls_buffer) {
      Ok(bytes_read) if bytes_read > 0 => {
        // Exercise set_readable for ABI coverage after successful read.
        envoy.set_readable();

        // Feed encrypted data to rustls.
        let mut cursor = Cursor::new(&tls_buffer[.. bytes_read as usize]);
        let tls_read_result = match &mut self.connection {
          TlsConnection::Client(conn) => conn.read_tls(&mut cursor),
          TlsConnection::Server(conn) => conn.read_tls(&mut cursor),
          TlsConnection::Closed => {
            return (0, true, false);
          },
        };

        if tls_read_result.is_ok() {
          // Process TLS packets (decrypt).
          let process_result = match &mut self.connection {
            TlsConnection::Client(conn) => conn.process_new_packets(),
            TlsConnection::Server(conn) => conn.process_new_packets(),
            TlsConnection::Closed => {
              return (0, true, false);
            },
          };

          if process_result.is_err() {
            envoy.log(LogLevel::Error, "[RUSTLS] TLS processing failed");
            eprintln!("[RUSTLS] TLS processing failed");
            return (0, true, false);
          }

          // Check if handshake completed.
          let is_handshaking = match &self.connection {
            TlsConnection::Client(conn) => conn.is_handshaking(),
            TlsConnection::Server(conn) => conn.is_handshaking(),
            TlsConnection::Closed => false,
          };

          if !is_handshaking && !self.handshake_complete {
            self.handshake_complete = true;
            envoy.log(LogLevel::Info, "[RUSTLS] Handshake complete");
            eprintln!("[RUSTLS] Handshake complete");

            // Extract negotiated parameters and create SSL info.
            self.extract_ssl_info();

            // Flush any pending handshake responses.
            self.flush_tls_writes(envoy);

            // Flush any buffered application writes.
            if !self.write_buffer.is_empty() {
              eprintln!(
                "[RUSTLS] Flushing {} buffered bytes after handshake",
                self.write_buffer.len()
              );
              let buffered_data = std::mem::take(&mut self.write_buffer);
              match &mut self.connection {
                TlsConnection::Client(conn) => {
                  let _ = conn.writer().write(&buffered_data);
                },
                TlsConnection::Server(conn) => {
                  let _ = conn.writer().write(&buffered_data);
                },
                TlsConnection::Closed => {},
              }
              self.flush_tls_writes(envoy);
            }
          }

          // Flush any TLS data that was generated during processing.
          self.flush_tls_writes(envoy);

          // Read decrypted application data.
          let app_data_read = match &mut self.connection {
            TlsConnection::Client(conn) => conn.reader().read(buffer).unwrap_or(0),
            TlsConnection::Server(conn) => conn.reader().read(buffer).unwrap_or(0),
            TlsConnection::Closed => 0,
          };

          (app_data_read as u64, false, false)
        } else {
          envoy.log(LogLevel::Warn, "[RUSTLS] TLS read failed");
          (0, true, false)
        }
      },
      Ok(_) => (0, false, false), // No data available.
      Err(IoError::Again) => (0, false, false),
      Err(IoError::Error(code)) => {
        envoy.log(
          LogLevel::Error,
          &format!("[RUSTLS] Read error: code={}", code),
        );
        (0, true, false)
      },
    }
  }

  /// Write data to the connection.
  ///
  /// This function performs the complete TLS write flow:
  /// 1. Accepts plaintext application data
  /// 2. Feeds it to Rustls for encryption
  /// 3. Writes encrypted TLS records to the network
  ///
  /// If the handshake is not yet complete, the data is buffered and will be
  /// written after the handshake completes.
  fn do_write(&mut self, envoy: &mut TS, data: &[u8], end_stream: bool) -> (u64, bool) {
    if !self.handshake_complete {
      envoy.log(
        LogLevel::Trace,
        "[RUSTLS] Write before handshake, buffering",
      );
      eprintln!(
        "[RUSTLS] Write called before handshake complete, buffering {} bytes",
        data.len()
      );
      self.write_buffer.extend_from_slice(data);
      return (data.len() as u64, false);
    }

    // Write application data to rustls (will be encrypted).
    let written = match &mut self.connection {
      TlsConnection::Client(conn) => conn.writer().write(data).unwrap_or(0),
      TlsConnection::Server(conn) => conn.writer().write(data).unwrap_or(0),
      TlsConnection::Closed => 0,
    };

    if written > 0 {
      envoy.log(LogLevel::Trace, "[RUSTLS] Wrote application data");
      // Exercise flush_write_buffer for ABI coverage.
      envoy.flush_write_buffer();
    }

    // Flush encrypted TLS records to network.
    self.flush_tls_writes(envoy);

    (written as u64, end_stream)
  }

  /// Called when the socket is closed.
  fn on_close(&mut self, envoy: &mut TS, _event: abi::envoy_dynamic_module_type_connection_event) {
    // Exercise different log levels for ABI coverage.
    envoy.log(LogLevel::Info, "[RUSTLS] Connection closing");
    envoy.log(LogLevel::Debug, "[RUSTLS] Socket closing");
    // Clear write buffer to ensure no memory leaks.
    self.write_buffer.clear();
    self.connection = TlsConnection::Closed;
  }

  /// Returns the protocol string for this socket.
  fn protocol(&self) -> Option<&str> {
    self
      .ssl_info
      .as_ref()
      .map(|info| info.alpn_protocol.as_str())
  }

  /// Returns whether the socket can be flushed and closed.
  fn can_flush_close(&self) -> bool {
    self.handshake_complete
  }

  /// Returns SSL connection information.
  fn get_ssl_info(&self) -> Option<&SslConnectionInfo> {
    self.ssl_info.as_ref()
  }

  /// Configures initial congestion window (not applicable for TLS).
  fn configure_initial_congestion_window(
    &mut self,
    _bandwidth_bits_per_sec: u64,
    _rtt_microseconds: u64,
  ) {
    // Not applicable for TLS transport socket.
  }

  /// Starts secure transport (always returns true for TLS).
  fn start_secure_transport(&mut self, _envoy: &mut TS) -> bool {
    true // TLS is always secure.
  }
}

impl RustlsTlsSocket {
  /// Helper to flush TLS writes to the network.
  fn flush_tls_writes(&mut self, envoy: &mut impl EnvoyTransportSocket) {
    let mut write_buffer = vec![0u8; 16384];
    loop {
      let wants_write = match &self.connection {
        TlsConnection::Client(conn) => conn.wants_write(),
        TlsConnection::Server(conn) => conn.wants_write(),
        TlsConnection::Closed => false,
      };

      if !wants_write {
        break;
      }

      let written = match &mut self.connection {
        TlsConnection::Client(conn) => {
          let mut cursor = Cursor::new(&mut write_buffer[..]);
          conn.write_tls(&mut cursor).unwrap_or(0)
        },
        TlsConnection::Server(conn) => {
          let mut cursor = Cursor::new(&mut write_buffer[..]);
          conn.write_tls(&mut cursor).unwrap_or(0)
        },
        TlsConnection::Closed => 0,
      };

      if written == 0 {
        break;
      }

      // Write to network.
      match envoy.write_to_io_handle(&write_buffer[.. written]) {
        Ok(_) => {},
        Err(IoError::Again) => break,
        Err(IoError::Error(code)) => {
          eprintln!("[RUSTLS] Failed to write TLS data: code={}", code);
          break;
        },
      }
    }
  }

  /// Extract SSL connection information after handshake.
  fn extract_ssl_info(&mut self) {
    // Extract negotiated ALPN protocol.
    let alpn_protocol = match &self.connection {
      TlsConnection::Client(conn) => conn
        .alpn_protocol()
        .map(|p| String::from_utf8_lossy(p).to_string()),
      TlsConnection::Server(conn) => conn
        .alpn_protocol()
        .map(|p| String::from_utf8_lossy(p).to_string()),
      TlsConnection::Closed => None,
    }
    .unwrap_or_default();

    // Extract TLS version.
    let tls_version = match &self.connection {
      TlsConnection::Client(conn) => conn.protocol_version().map(|v| format!("{:?}", v)),
      TlsConnection::Server(conn) => conn.protocol_version().map(|v| format!("{:?}", v)),
      TlsConnection::Closed => None,
    }
    .unwrap_or_default();

    // Extract cipher suite.
    let cipher_suite = match &self.connection {
      TlsConnection::Client(conn) => conn
        .negotiated_cipher_suite()
        .map(|cs| format!("{:?}", cs.suite())),
      TlsConnection::Server(conn) => conn
        .negotiated_cipher_suite()
        .map(|cs| format!("{:?}", cs.suite())),
      TlsConnection::Closed => None,
    }
    .unwrap_or_default();

    // Extract peer certificates.
    let peer_certificates: Vec<Vec<u8>> = match &self.connection {
      TlsConnection::Client(conn) => conn
        .peer_certificates()
        .map(|certs| certs.iter().map(|c| c.to_vec()).collect::<Vec<Vec<u8>>>()),
      TlsConnection::Server(conn) => conn
        .peer_certificates()
        .map(|certs| certs.iter().map(|c| c.to_vec()).collect::<Vec<Vec<u8>>>()),
      TlsConnection::Closed => None,
    }
    .unwrap_or_default();

    self.ssl_info = Some(SslConnectionInfo {
      tls_version,
      cipher_suite,
      alpn_protocol,
      sni: self.sni.clone().unwrap_or_default(),
      peer_certificates: peer_certificates.clone(),
      peer_certificate_validated: !peer_certificates.is_empty(),
    });

    eprintln!(
      "[RUSTLS] SSL info extracted - TLS: {}, Cipher: {}, ALPN: {}, SNI: {}",
      self.ssl_info.as_ref().unwrap().tls_version,
      self.ssl_info.as_ref().unwrap().cipher_suite,
      self.ssl_info.as_ref().unwrap().alpn_protocol,
      self.ssl_info.as_ref().unwrap().sni
    );
  }
}

// Helper functions

/// Parse certificates from a PEM file.
fn load_cert_from_file(path: &str) -> Result<Vec<CertificateDer<'static>>, String> {
  let pem_data = fs::read(path).map_err(|e| format!("Failed to read cert file {}: {}", path, e))?;
  let mut reader = Cursor::new(pem_data);
  rustls_pemfile::certs(&mut reader)
    .collect::<Result<Vec<_>, _>>()
    .map_err(|e| format!("Failed to parse certificates: {}", e))
}

/// Parse a private key from a PEM file.
fn load_key_from_file(path: &str) -> Result<PrivateKeyDer<'static>, String> {
  let pem_data = fs::read(path).map_err(|e| format!("Failed to read key file {}: {}", path, e))?;
  let mut reader = Cursor::new(pem_data);
  rustls_pemfile::private_key(&mut reader)
    .map_err(|e| format!("Failed to parse private key: {}", e))?
    .ok_or_else(|| "No private key found in file".to_string())
}

/// Parse CA certificates from a PEM file and build a root certificate store.
fn load_ca_from_file(path: &str) -> Result<RootCertStore, String> {
  let pem_data = fs::read(path).map_err(|e| format!("Failed to read CA file {}: {}", path, e))?;
  let mut reader = Cursor::new(pem_data);
  let certs = rustls_pemfile::certs(&mut reader)
    .collect::<Result<Vec<_>, _>>()
    .map_err(|e| format!("Failed to parse CA certificates: {}", e))?;

  let mut root_store = RootCertStore::empty();
  for cert in certs {
    root_store
      .add(cert)
      .map_err(|e| format!("Failed to add CA cert: {}", e))?;
  }
  Ok(root_store)
}

/// Extract wrapped configuration from protobuf-style JSON.
fn extract_wrapped_config(config_str: &str) -> String {
  if config_str.contains("inline_string") {
    // Extract inline_string field value.
    if let Some(start) = config_str.find("\"inline_string\":\"") {
      let value_start = start + "\"inline_string\":\"".len();
      let remaining = &config_str[value_start ..];
      // Find the closing quote, handling escaped quotes.
      let mut result = String::new();
      let mut chars = remaining.chars();
      let mut escaped = false;
      while let Some(ch) = chars.next() {
        if escaped {
          result.push(ch);
          escaped = false;
        } else if ch == '\\' {
          escaped = true;
        } else if ch == '"' {
          break;
        } else {
          result.push(ch);
        }
      }
      eprintln!("[RUSTLS CONFIG] Extracted inline_string: {}", result);
      return result;
    }
  } else if config_str.contains("\"value\"") {
    if let Some(extracted) = extract_json_field(config_str, "value") {
      return extracted.replace("\\\"", "\"").replace("\\\\", "\\");
    }
  }
  config_str.to_string()
}

/// Simple JSON field extractor.
fn extract_json_field(json: &str, field: &str) -> Option<String> {
  let pattern = format!("\"{}\":", field);
  if let Some(start_idx) = json.find(&pattern) {
    let value_start = start_idx + pattern.len();
    let remaining = &json[value_start ..];
    let trimmed = remaining.trim_start();

    if let Some(quote_start) = trimmed.find('"') {
      let after_quote = &trimmed[quote_start + 1 ..];
      if let Some(quote_end) = after_quote.find('"') {
        return Some(after_quote[.. quote_end].to_string());
      }
    }
  }
  None
}
