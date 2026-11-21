use super::*;
use crate::abi::{
  envoy_dynamic_module_type_socket_interface_address,
  envoy_dynamic_module_type_socket_interface_creation_options,
  envoy_dynamic_module_type_socket_interface_creation_result,
  envoy_dynamic_module_type_socket_interface_io_slice,
  envoy_dynamic_module_type_socket_interface_io_status,
  envoy_dynamic_module_type_socket_interface_socket_flag,
  envoy_dynamic_module_type_socket_interface_socket_type,
};
use crate::mod_test::{MOCK_SOCKET_IO_READY_CALLS, MOCK_SOCKET_IO_READY_DELETES};
use std::sync::{Arc, Mutex};

type Calls = Arc<Mutex<Vec<String>>>;

fn new_calls() -> Calls {
  Arc::new(Mutex::new(Vec::new()))
}

fn record(calls: &Calls, call: &str) {
  calls.lock().unwrap().push(call.to_string());
}

fn recorded(calls: &Calls, call: &str) -> bool {
  calls.lock().unwrap().iter().any(|c| c == call)
}

// A per-socket I/O object that records the calls Envoy makes and panics on demand.
struct TestIo {
  calls: Calls,
  panic_on: &'static str,
}

impl Drop for TestIo {
  fn drop(&mut self) {
    record(&self.calls, "io_drop");
    if self.panic_on == "drop" {
      panic!("drop panic");
    }
  }
}

impl SocketIo for TestIo {
  fn set_ready_handle(&mut self, handle: Box<dyn EnvoySocketIoReadyHandle>) {
    record(&self.calls, "set_ready_handle");
    if self.panic_on == "set_ready_handle" {
      panic!("set_ready_handle panic");
    }
    handle.ready(SocketIoEvents::READ | SocketIoEvents::WRITE);
    // Dropping the handle deletes the Envoy-side handle.
  }

  fn io_read(&mut self, slices: &mut [&mut [MaybeUninit<u8>]], peek: bool) -> SocketIoStatus {
    record(&self.calls, if peek { "io_read_peek" } else { "io_read" });
    match self.panic_on {
      "io_read" => panic!("io_read panic"),
      "read_again" => SocketIoStatus::Again,
      "read_error" => SocketIoStatus::Error,
      _ => {
        slices[0][0] = MaybeUninit::new(b'x');
        SocketIoStatus::Bytes(1)
      },
    }
  }

  fn io_write(&mut self, buffers: &[&[u8]]) -> SocketIoStatus {
    record(&self.calls, "io_write");
    match self.panic_on {
      "io_write" => panic!("io_write panic"),
      "write_again" => SocketIoStatus::Again,
      _ => SocketIoStatus::Bytes(buffers.iter().map(|b| b.len()).sum()),
    }
  }

  fn io_shutdown(&mut self, _how: i32) -> SocketShutdownStatus {
    record(&self.calls, "io_shutdown");
    match self.panic_on {
      "io_shutdown" => panic!("io_shutdown panic"),
      "shutdown_done" => SocketShutdownStatus::Done,
      "shutdown_error" => SocketShutdownStatus::Error,
      _ => SocketShutdownStatus::NotImplemented,
    }
  }

  fn duplicate(&mut self) -> Option<(i64, Box<dyn SocketIo>)> {
    record(&self.calls, "io_duplicate");
    match self.panic_on {
      "duplicate" => panic!("duplicate panic"),
      _ => Some((21, new_io(&self.calls, ""))),
    }
  }
}

// An I/O object that keeps the trait's default shutdown and duplicate.
struct DefaultIo;

impl SocketIo for DefaultIo {
  fn set_ready_handle(&mut self, _handle: Box<dyn EnvoySocketIoReadyHandle>) {}
  fn io_read(&mut self, _slices: &mut [&mut [MaybeUninit<u8>]], _peek: bool) -> SocketIoStatus {
    SocketIoStatus::Again
  }
  fn io_write(&mut self, _buffers: &[&[u8]]) -> SocketIoStatus {
    SocketIoStatus::Again
  }
}

// A listen socket acceptor that hands over one scripted outcome per accept.
struct TestAcceptor {
  calls: Calls,
  outcome: &'static str,
}

impl SocketAcceptor for TestAcceptor {
  fn set_ready_handle(&mut self, handle: Box<dyn EnvoySocketIoReadyHandle>) {
    record(&self.calls, "acceptor_set_ready_handle");
    handle.ready(SocketIoEvents::READ);
  }

  fn accept(&mut self) -> SocketAcceptResult {
    record(&self.calls, "accept");
    match self.outcome {
      "accept" => panic!("accept panic"),
      "again" => SocketAcceptResult::Again,
      "error" => SocketAcceptResult::Error,
      "kernel" => SocketAcceptResult::Accepted {
        fd: 31,
        handling: SocketHandling::Kernel,
        local_address: None,
        peer_address: None,
      },
      _ => SocketAcceptResult::Accepted {
        fd: 32,
        handling: SocketHandling::DelegatedIo(new_io(&self.calls, "")),
        local_address: Some(SocketAddress {
          address: "10.0.0.1".to_string(),
          port: 443,
        }),
        peer_address: Some(SocketAddress {
          address: "10.0.0.2".to_string(),
          port: 5555,
        }),
      },
    }
  }

  fn duplicate(&mut self) -> Option<(i64, Box<dyn SocketAcceptor>)> {
    record(&self.calls, "acceptor_duplicate");
    Some((
      33,
      Box::new(TestAcceptor {
        calls: self.calls.clone(),
        outcome: self.outcome,
      }),
    ))
  }
}

// A socket interface config that creates sockets with a caller-supplied result.
struct TestConfig {
  calls: Calls,
  panic_on: &'static str,
  make_result: Box<dyn Fn(&Calls) -> SocketCreationResult + Send + Sync>,
}

impl Drop for TestConfig {
  fn drop(&mut self) {
    if self.panic_on == "config_drop" {
      panic!("config drop panic");
    }
  }
}

impl SocketInterfaceConfig for TestConfig {
  fn ip_family_supported(&self, domain: i32) -> bool {
    if self.panic_on == "ip_family_supported" {
      panic!("ip_family_supported panic");
    }
    domain == 2 // AF_INET
  }

  fn socket(&self, creation: &SocketCreation) -> SocketCreationResult {
    record(
      &self.calls,
      &format!("socket {}:{}", creation.address, creation.port),
    );
    if self.panic_on == "socket" {
      panic!("socket panic");
    }
    (self.make_result)(&self.calls)
  }
}

fn new_io(calls: &Calls, panic_on: &'static str) -> Box<dyn SocketIo> {
  Box::new(TestIo {
    calls: calls.clone(),
    panic_on,
  })
}

fn new_acceptor(calls: &Calls, outcome: &'static str) -> Box<dyn SocketAcceptor> {
  Box::new(TestAcceptor {
    calls: calls.clone(),
    outcome,
  })
}

fn new_config_ptr(
  calls: &Calls,
  panic_on: &'static str,
  make_result: Box<dyn Fn(&Calls) -> SocketCreationResult + Send + Sync>,
) -> *const c_void {
  Box::into_raw(Box::new(ModuleSocketInterface {
    config: Box::new(TestConfig {
      calls: calls.clone(),
      panic_on,
      make_result,
    }),
  })) as *const c_void
}

fn io_state(
  io: Box<dyn SocketIo>,
) -> abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr {
  Box::into_raw(Box::new(ModuleSocket::Io(io))) as *mut c_void
}

fn acceptor_state(
  acceptor: Box<dyn SocketAcceptor>,
) -> abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr {
  Box::into_raw(Box::new(ModuleSocket::acceptor(acceptor))) as *mut c_void
}

fn close_state(state: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr) {
  unsafe { envoy_dynamic_module_on_socket_interface_socket_close(ptr::null(), state) };
}

fn envoy_buffer(bytes: &[u8]) -> abi::envoy_dynamic_module_type_envoy_buffer {
  abi::envoy_dynamic_module_type_envoy_buffer {
    ptr: bytes.as_ptr() as *const _,
    length: bytes.len(),
  }
}

struct SocketCall {
  result: envoy_dynamic_module_type_socket_interface_creation_result,
  fd: i64,
  flags: u32,
  state: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
}

fn call_socket(config_ptr: *const c_void) -> SocketCall {
  let options = envoy_dynamic_module_type_socket_interface_creation_options {
    mptcp_enabled: false,
    max_addresses_cache_size: 0,
  };
  let mut fd: i64 = -1;
  let mut flags: u32 = 0;
  let mut state: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr =
    ptr::null_mut();
  let result = unsafe {
    envoy_dynamic_module_on_socket_interface_socket(
      config_ptr,
      envoy_dynamic_module_type_socket_interface_socket_type::Stream,
      envoy_buffer(b"127.0.0.1"),
      8080,
      2, // AF_INET
      false,
      &options,
      &mut fd,
      &mut flags,
      &mut state,
    )
  };
  SocketCall {
    result,
    fd,
    flags,
    state,
  }
}

const DELEGATE_IO: u32 = envoy_dynamic_module_type_socket_interface_socket_flag::DelegateIo as u32;
const HANDS_OVER: u32 =
  envoy_dynamic_module_type_socket_interface_socket_flag::HandsOverConnections as u32;

fn read_one(
  state: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
  peek: bool,
) -> (
  envoy_dynamic_module_type_socket_interface_io_status,
  u64,
  u8,
) {
  let mut buf = [0u8; 4];
  let mut slice = envoy_dynamic_module_type_socket_interface_io_slice {
    ptr: buf.as_mut_ptr() as *mut _,
    length: buf.len(),
  };
  let mut transferred: u64 = 0;
  let status = unsafe {
    envoy_dynamic_module_on_socket_interface_io_read(
      ptr::null(),
      state,
      &mut slice,
      1,
      peek,
      &mut transferred,
    )
  };
  (status, transferred, buf[0])
}

fn write_abc(
  state: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
) -> (envoy_dynamic_module_type_socket_interface_io_status, u64) {
  let buffers = [envoy_buffer(b"ab"), envoy_buffer(b""), envoy_buffer(b"c")];
  let mut transferred: u64 = 0;
  let status = unsafe {
    envoy_dynamic_module_on_socket_interface_io_write(
      ptr::null(),
      state,
      buffers.as_ptr(),
      buffers.len(),
      &mut transferred,
    )
  };
  (status, transferred)
}

struct AcceptCall {
  status: envoy_dynamic_module_type_socket_interface_io_status,
  fd: i64,
  flags: u32,
  state: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
  local: envoy_dynamic_module_type_socket_interface_address,
  peer: envoy_dynamic_module_type_socket_interface_address,
}

fn empty_address() -> envoy_dynamic_module_type_socket_interface_address {
  envoy_dynamic_module_type_socket_interface_address {
    address: abi::envoy_dynamic_module_type_module_buffer {
      ptr: ptr::null(),
      length: 0,
    },
    port: 0,
  }
}

fn call_accept(
  state: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr,
) -> AcceptCall {
  let mut call = AcceptCall {
    status: envoy_dynamic_module_type_socket_interface_io_status::Error,
    fd: -1,
    flags: 0,
    state: ptr::null_mut(),
    local: empty_address(),
    peer: empty_address(),
  };
  call.status = unsafe {
    envoy_dynamic_module_on_socket_interface_accept(
      ptr::null(),
      state,
      &mut call.fd,
      &mut call.flags,
      &mut call.state,
      &mut call.local,
      &mut call.peer,
    )
  };
  call
}

fn address_string(address: &envoy_dynamic_module_type_socket_interface_address) -> String {
  let bytes =
    unsafe { std::slice::from_raw_parts(address.address.ptr as *const u8, address.address.length) };
  String::from_utf8(bytes.to_vec()).unwrap()
}

#[test]
fn test_config_new_impl_round_trip() {
  fn factory(_name: &str, _config: &[u8]) -> Option<Box<dyn SocketInterfaceConfig>> {
    Some(Box::new(TestConfig {
      calls: new_calls(),
      panic_on: "",
      make_result: Box::new(|_calls| SocketCreationResult::UseDefault),
    }))
  }
  let config_ptr = envoy_dynamic_module_on_socket_interface_config_new_impl(
    "name",
    b"config",
    Some(&(factory as NewSocketInterfaceConfigFunction)),
  );
  assert!(!config_ptr.is_null());
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };

  fn rejecting(_name: &str, _config: &[u8]) -> Option<Box<dyn SocketInterfaceConfig>> {
    None
  }
  assert!(envoy_dynamic_module_on_socket_interface_config_new_impl(
    "name",
    b"config",
    Some(&(rejecting as NewSocketInterfaceConfigFunction)),
  )
  .is_null());
  assert!(
    envoy_dynamic_module_on_socket_interface_config_new_impl("name", b"config", None).is_null()
  );
}

#[test]
fn test_ip_family_supported() {
  let calls = new_calls();
  let config_ptr = new_config_ptr(&calls, "", Box::new(|_| SocketCreationResult::UseDefault));
  assert!(unsafe { envoy_dynamic_module_on_socket_interface_ip_family_supported(config_ptr, 2) });
  assert!(!unsafe { envoy_dynamic_module_on_socket_interface_ip_family_supported(config_ptr, 10) });
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };
}

#[test]
fn test_socket_results_map_to_the_abi() {
  let calls = new_calls();

  // UseDefault and Error leave the out-params untouched.
  for (result, expected) in [
    (
      "default",
      envoy_dynamic_module_type_socket_interface_creation_result::UseDefault,
    ),
    (
      "error",
      envoy_dynamic_module_type_socket_interface_creation_result::Error,
    ),
  ] {
    let config_ptr = new_config_ptr(
      &calls,
      "",
      Box::new(move |_| {
        if result == "default" {
          SocketCreationResult::UseDefault
        } else {
          SocketCreationResult::Error
        }
      }),
    );
    let call = call_socket(config_ptr);
    assert_eq!(call.result, expected);
    assert_eq!(call.fd, -1);
    assert_eq!(call.flags, 0);
    assert!(call.state.is_null());
    unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };
  }
  assert!(recorded(&calls, "socket 127.0.0.1:8080"));

  // A kernel socket carries no flag or per-socket state.
  let config_ptr = new_config_ptr(
    &calls,
    "",
    Box::new(|_| SocketCreationResult::Created {
      fd: 7,
      handling: SocketHandling::Kernel,
    }),
  );
  let call = call_socket(config_ptr);
  assert_eq!(
    call.result,
    envoy_dynamic_module_type_socket_interface_creation_result::Created
  );
  assert_eq!((call.fd, call.flags), (7, 0));
  assert!(call.state.is_null());
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };

  // A delegated socket and a listen socket that hands over connections carry their flag and
  // state.
  let config_ptr = new_config_ptr(
    &calls,
    "",
    Box::new(|calls| SocketCreationResult::AlreadyConnected {
      fd: 8,
      handling: SocketHandling::DelegatedIo(new_io(calls, "")),
    }),
  );
  let call = call_socket(config_ptr);
  assert_eq!(
    call.result,
    envoy_dynamic_module_type_socket_interface_creation_result::AlreadyConnected
  );
  assert_eq!((call.fd, call.flags), (8, DELEGATE_IO));
  assert!(!call.state.is_null());
  close_state(call.state);
  assert!(recorded(&calls, "io_drop"));
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };

  let config_ptr = new_config_ptr(
    &calls,
    "",
    Box::new(|calls| SocketCreationResult::Created {
      fd: 9,
      handling: SocketHandling::HandsOverConnections(new_acceptor(calls, "again")),
    }),
  );
  let call = call_socket(config_ptr);
  assert_eq!((call.fd, call.flags), (9, HANDS_OVER));
  assert!(!call.state.is_null());
  close_state(call.state);
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };
}

#[test]
fn test_set_socket_handle_reaches_io_and_acceptor() {
  let calls = new_calls();
  let io = io_state(new_io(&calls, ""));
  let io_handle =
    0x2000usize as abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr;
  unsafe { envoy_dynamic_module_on_socket_interface_set_socket_handle(ptr::null(), io, io_handle) };
  assert!(recorded(&calls, "set_ready_handle"));

  let acceptor = acceptor_state(new_acceptor(&calls, "again"));
  let acceptor_handle =
    0x2001usize as abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr;
  unsafe {
    envoy_dynamic_module_on_socket_interface_set_socket_handle(
      ptr::null(),
      acceptor,
      acceptor_handle,
    )
  };
  assert!(recorded(&calls, "acceptor_set_ready_handle"));

  // Both handles reported readiness and were deleted when dropped.
  let ready = MOCK_SOCKET_IO_READY_CALLS.lock().unwrap().clone();
  assert!(ready.contains(&(0x2000, (SocketIoEvents::READ | SocketIoEvents::WRITE).0)));
  assert!(ready.contains(&(0x2001, SocketIoEvents::READ.0)));
  let deletes = MOCK_SOCKET_IO_READY_DELETES.lock().unwrap().clone();
  assert!(deletes.contains(&0x2000) && deletes.contains(&0x2001));
  close_state(io);
  close_state(acceptor);
}

#[test]
fn test_io_hooks_map_to_the_abi() {
  let calls = new_calls();
  let io = io_state(new_io(&calls, ""));

  let (status, transferred, first) = read_one(io, false);
  assert_eq!(
    status,
    envoy_dynamic_module_type_socket_interface_io_status::Success
  );
  assert_eq!((transferred, first), (1, b'x'));
  let (..) = read_one(io, true);
  assert!(recorded(&calls, "io_read_peek"));

  // Empty buffers are passed through, and the byte count covers the others.
  let (status, transferred) = write_abc(io);
  assert_eq!(
    status,
    envoy_dynamic_module_type_socket_interface_io_status::Success
  );
  assert_eq!(transferred, 3);
  close_state(io);

  for (panic_on, read, write, shutdown) in [
    (
      "read_again",
      envoy_dynamic_module_type_socket_interface_io_status::Again,
      envoy_dynamic_module_type_socket_interface_io_status::Success,
      envoy_dynamic_module_type_socket_interface_io_status::NotImplemented,
    ),
    (
      "read_error",
      envoy_dynamic_module_type_socket_interface_io_status::Error,
      envoy_dynamic_module_type_socket_interface_io_status::Success,
      envoy_dynamic_module_type_socket_interface_io_status::NotImplemented,
    ),
    (
      "write_again",
      envoy_dynamic_module_type_socket_interface_io_status::Success,
      envoy_dynamic_module_type_socket_interface_io_status::Again,
      envoy_dynamic_module_type_socket_interface_io_status::NotImplemented,
    ),
    (
      "shutdown_done",
      envoy_dynamic_module_type_socket_interface_io_status::Success,
      envoy_dynamic_module_type_socket_interface_io_status::Success,
      envoy_dynamic_module_type_socket_interface_io_status::Success,
    ),
    (
      "shutdown_error",
      envoy_dynamic_module_type_socket_interface_io_status::Success,
      envoy_dynamic_module_type_socket_interface_io_status::Success,
      envoy_dynamic_module_type_socket_interface_io_status::Error,
    ),
  ] {
    let io = io_state(new_io(&calls, panic_on));
    assert_eq!(read_one(io, false).0, read, "{panic_on}");
    assert_eq!(write_abc(io).0, write, "{panic_on}");
    assert_eq!(
      unsafe { envoy_dynamic_module_on_socket_interface_io_shutdown(ptr::null(), io, 1) },
      shutdown,
      "{panic_on}"
    );
    close_state(io);
  }

  // The I/O hooks reject a listen socket's state.
  let acceptor = acceptor_state(new_acceptor(&calls, "again"));
  assert_eq!(
    read_one(acceptor, false).0,
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  assert_eq!(
    write_abc(acceptor).0,
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  assert_eq!(
    unsafe { envoy_dynamic_module_on_socket_interface_io_shutdown(ptr::null(), acceptor, 1) },
    envoy_dynamic_module_type_socket_interface_io_status::NotImplemented
  );
  close_state(acceptor);
}

#[test]
fn test_default_io_shutdown_and_duplicate() {
  let io = io_state(Box::new(DefaultIo));
  assert_eq!(
    unsafe { envoy_dynamic_module_on_socket_interface_io_shutdown(ptr::null(), io, 1) },
    envoy_dynamic_module_type_socket_interface_io_status::NotImplemented
  );
  let mut fd: i64 = -1;
  let mut copy: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr = ptr::null_mut();
  assert_eq!(
    unsafe {
      envoy_dynamic_module_on_socket_interface_socket_duplicate(ptr::null(), io, &mut fd, &mut copy)
    },
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  assert!(copy.is_null());
  close_state(io);
}

#[test]
fn test_accept_maps_to_the_abi() {
  let calls = new_calls();

  // A delegated connection carries its flag, state and addresses.
  let acceptor = acceptor_state(new_acceptor(&calls, "delegated"));
  let call = call_accept(acceptor);
  assert_eq!(
    call.status,
    envoy_dynamic_module_type_socket_interface_io_status::Success
  );
  assert_eq!((call.fd, call.flags), (32, DELEGATE_IO));
  assert!(!call.state.is_null());
  assert_eq!(address_string(&call.local), "10.0.0.1");
  assert_eq!(call.local.port, 443);
  assert_eq!(address_string(&call.peer), "10.0.0.2");
  assert_eq!(call.peer.port, 5555);
  close_state(call.state);
  close_state(acceptor);

  // A kernel connection without addresses leaves them empty.
  let acceptor = acceptor_state(new_acceptor(&calls, "kernel"));
  let call = call_accept(acceptor);
  assert_eq!((call.fd, call.flags), (31, 0));
  assert!(call.state.is_null());
  assert_eq!(call.local.address.length, 0);
  assert_eq!(call.peer.address.length, 0);
  close_state(acceptor);

  for (outcome, expected) in [
    (
      "again",
      envoy_dynamic_module_type_socket_interface_io_status::Again,
    ),
    (
      "error",
      envoy_dynamic_module_type_socket_interface_io_status::Error,
    ),
  ] {
    let acceptor = acceptor_state(new_acceptor(&calls, outcome));
    assert_eq!(call_accept(acceptor).status, expected);
    close_state(acceptor);
  }

  // Accept rejects a delegated socket's state.
  let io = io_state(new_io(&calls, ""));
  assert_eq!(
    call_accept(io).status,
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  close_state(io);
}

#[test]
fn test_duplicate_maps_to_the_abi() {
  let calls = new_calls();
  let mut fd: i64 = -1;
  let mut copy: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr = ptr::null_mut();

  let io = io_state(new_io(&calls, ""));
  assert_eq!(
    unsafe {
      envoy_dynamic_module_on_socket_interface_socket_duplicate(ptr::null(), io, &mut fd, &mut copy)
    },
    envoy_dynamic_module_type_socket_interface_io_status::Success
  );
  assert_eq!(fd, 21);
  assert!(!copy.is_null());
  close_state(copy);
  close_state(io);

  let acceptor = acceptor_state(new_acceptor(&calls, "again"));
  assert_eq!(
    unsafe {
      envoy_dynamic_module_on_socket_interface_socket_duplicate(
        ptr::null(),
        acceptor,
        &mut fd,
        &mut copy,
      )
    },
    envoy_dynamic_module_type_socket_interface_io_status::Success
  );
  assert_eq!(fd, 33);
  // The copy is a listen socket of its own.
  assert_eq!(
    call_accept(copy).status,
    envoy_dynamic_module_type_socket_interface_io_status::Again
  );
  close_state(copy);
  close_state(acceptor);
}

#[test]
fn test_every_hook_recovers_from_panics() {
  // config_new: the registered factory panics for this name only, so other tests that never set
  // the factory are unaffected.
  fn panicking_factory(name: &str, _config: &[u8]) -> Option<Box<dyn SocketInterfaceConfig>> {
    if name == "panic" {
      panic!("config_new panic");
    }
    None
  }
  let _ = crate::NEW_SOCKET_INTERFACE_CONFIG_FUNCTION
    .set(panicking_factory as NewSocketInterfaceConfigFunction);
  assert!(unsafe {
    envoy_dynamic_module_on_socket_interface_config_new(
      ptr::null_mut(),
      envoy_buffer(b"panic"),
      envoy_buffer(b""),
    )
  }
  .is_null());

  let calls = new_calls();

  // ip_family_supported and socket return their failure values.
  let config_ptr = new_config_ptr(
    &calls,
    "ip_family_supported",
    Box::new(|_| SocketCreationResult::UseDefault),
  );
  assert!(!unsafe { envoy_dynamic_module_on_socket_interface_ip_family_supported(config_ptr, 2) });
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };
  let config_ptr = new_config_ptr(
    &calls,
    "socket",
    Box::new(|_| SocketCreationResult::UseDefault),
  );
  assert_eq!(
    call_socket(config_ptr).result,
    envoy_dynamic_module_type_socket_interface_creation_result::Error
  );
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };

  // config_destroy returns despite the panic.
  let config_ptr = new_config_ptr(
    &calls,
    "config_drop",
    Box::new(|_| SocketCreationResult::UseDefault),
  );
  unsafe { envoy_dynamic_module_on_socket_interface_config_destroy(config_ptr) };

  // The per-socket hooks that return a status return Error.
  let io = io_state(new_io(&calls, "io_read"));
  assert_eq!(
    read_one(io, false).0,
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  close_state(io);
  let io = io_state(new_io(&calls, "io_write"));
  assert_eq!(
    write_abc(io).0,
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  close_state(io);
  let io = io_state(new_io(&calls, "io_shutdown"));
  assert_eq!(
    unsafe { envoy_dynamic_module_on_socket_interface_io_shutdown(ptr::null(), io, 1) },
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  close_state(io);
  let io = io_state(new_io(&calls, "duplicate"));
  let mut fd: i64 = -1;
  let mut copy: abi::envoy_dynamic_module_type_socket_interface_socket_module_ptr = ptr::null_mut();
  assert_eq!(
    unsafe {
      envoy_dynamic_module_on_socket_interface_socket_duplicate(ptr::null(), io, &mut fd, &mut copy)
    },
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  close_state(io);
  let acceptor = acceptor_state(new_acceptor(&calls, "accept"));
  assert_eq!(
    call_accept(acceptor).status,
    envoy_dynamic_module_type_socket_interface_io_status::Error
  );
  close_state(acceptor);

  // The void hooks return despite the panic.
  let io = io_state(new_io(&calls, "set_ready_handle"));
  let handle =
    0x2002usize as abi::envoy_dynamic_module_type_socket_interface_io_ready_handle_module_ptr;
  unsafe { envoy_dynamic_module_on_socket_interface_set_socket_handle(ptr::null(), io, handle) };
  close_state(io);
  close_state(io_state(new_io(&calls, "drop")));
}
