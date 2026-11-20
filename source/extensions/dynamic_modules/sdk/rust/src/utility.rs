use crate::buffer::EnvoyBuffer;
use crate::EnvoyHttpFilter;
use std::mem::MaybeUninit;

type AbiHeader = crate::abi::envoy_dynamic_module_type_envoy_http_header;

/// Fills `buf` with all the entries of a header map and returns them.
///
/// `size` reports the header count of the map and `fill` writes that many entries into the given
/// array. Both are called here, one right after the other, so the array handed to `fill` always has
/// room for the count that `size` reported. A caller only names the two callbacks and can never
/// pair a fill with a count taken from another header map or from an earlier state of the same
/// one.
///
/// `buf` is cleared first and only grows when it is too small, so a caller that reads headers
/// repeatedly can reuse it. Returns `None` when the header map is unavailable.
pub(crate) fn fill_headers<S, F>(
  buf: &mut Vec<MaybeUninit<AbiHeader>>,
  size: S,
  fill: F,
) -> Option<&[AbiHeader]>
where
  S: FnOnce() -> usize,
  F: FnOnce(*mut AbiHeader) -> bool,
{
  buf.clear();
  let count = size();
  buf.reserve(count);
  if !fill(buf.as_mut_ptr() as *mut AbiHeader) {
    return None;
  }
  // Safety: Envoy initialized count entries starting at index zero, and the buffer has room for
  // them.
  unsafe {
    buf.set_len(count);
    Some(std::slice::from_raw_parts(
      buf.as_ptr() as *const AbiHeader,
      count,
    ))
  }
}

/// Builds the key and value pair of a filled header entry field by field, so the Rust tuple is
/// never reinterpreted as the C struct. The buffers point into Envoy owned memory valid for the
/// current event hook.
fn header_pair<'a>(header: &AbiHeader) -> (EnvoyBuffer<'a>, EnvoyBuffer<'a>) {
  unsafe {
    (
      EnvoyBuffer::new_from_raw(header.key_ptr as *const u8, header.key_length),
      EnvoyBuffer::new_from_raw(header.value_ptr as *const u8, header.value_length),
    )
  }
}

/// Reads all the entries of a header map as key and value pairs through the given size and fill
/// callbacks. See [`fill_headers`]. Returns an empty vector when there are no headers or the header
/// map is unavailable.
pub(crate) fn collect_headers<'a, S, F>(size: S, fill: F) -> Vec<(EnvoyBuffer<'a>, EnvoyBuffer<'a>)>
where
  S: FnOnce() -> usize,
  F: FnOnce(*mut AbiHeader) -> bool,
{
  let mut buf = Vec::new();
  fill_headers(&mut buf, size, fill).map_or_else(Vec::new, |headers| {
    headers.iter().map(header_pair).collect()
  })
}

fn get_body_content<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF, request: bool) -> Vec<u8> {
  // If the received body is the same as the buffered body (a previous filter did StopAndBuffer
  // and resumed), skip the received body to avoid duplicating data.
  let is_buffered = if request {
    envoy_filter.received_buffered_request_body()
  } else {
    envoy_filter.received_buffered_response_body()
  };

  // The received size still uses a cheap size call so the result is allocated once at the exact
  // total. The buffered size is summed from its chunk lengths below to drop a redundant crossing.
  let received_size = if is_buffered {
    0
  } else if request {
    envoy_filter.get_received_request_body_size()
  } else {
    envoy_filter.get_received_response_body_size()
  };

  let buffered = if request {
    envoy_filter.get_buffered_request_body()
  } else {
    envoy_filter.get_buffered_response_body()
  };
  let buffered_size = buffered.as_ref().map_or(0, |chunks| {
    chunks
      .iter()
      .map(|chunk| chunk.as_slice().len())
      .sum::<usize>()
  });

  let mut result = Vec::with_capacity(buffered_size + received_size);
  if let Some(chunks) = buffered {
    for chunk in &chunks {
      result.extend_from_slice(chunk.as_slice());
    }
  }

  if !is_buffered {
    let received = if request {
      envoy_filter.get_received_request_body()
    } else {
      envoy_filter.get_received_response_body()
    };
    if let Some(chunks) = received {
      for chunk in &chunks {
        result.extend_from_slice(chunk.as_slice());
      }
    }
  }

  result
}

/// Reads the whole request body by combining the buffered body and the latest received body.
/// This will copy all request body content into a module owned `Vec<u8>`.
///
/// This should only be called after we see the end of the request, which means the
/// `end_of_stream` flag is true in the `on_request_body` callback or we are in the
/// `on_request_trailers` callback.
pub fn read_whole_request_body<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF) -> Vec<u8> {
  get_body_content(envoy_filter, true)
}

/// Reads the whole response body by combining the buffered body and the latest received body.
/// This will copy all response body content into a module owned `Vec<u8>`.
///
/// This should only be called after we see the end of the response, which means the
/// `end_of_stream` flag is true in the `on_response_body` callback or we are in the
/// `on_response_trailers` callback.
pub fn read_whole_response_body<EHF: EnvoyHttpFilter>(envoy_filter: &mut EHF) -> Vec<u8> {
  get_body_content(envoy_filter, false)
}

/// Returns whether `name` is a header name that Envoy accepts: a non-empty HTTP token, optionally
/// prefixed with `:` for a pseudo-header. Upper case letters are allowed.
///
/// Envoy does not check the header names that a module passes, so a module that builds a name
/// from request data or other untrusted input should check it with this first.
pub fn is_valid_header_name(name: impl AsRef<[u8]>) -> bool {
  let name = name.as_ref();
  let token = name.strip_prefix(b":").unwrap_or(name);
  !token.is_empty() && token.iter().all(|&c| TOKEN_CHARS[usize::from(c)])
}

/// Returns whether `value` is a header value that Envoy accepts: one without a control character
/// other than horizontal tab. Bytes above 0x7f are allowed.
///
/// Envoy does not check the header values that a module passes. A CR or LF in a value can end the
/// header and start another on an HTTP/1 connection, so a module that copies request data or
/// other untrusted input into a header should check it with this first.
pub fn is_valid_header_value(value: impl AsRef<[u8]>) -> bool {
  // A fold rather than all(): without an early exit, the compiler can vectorize the loop.
  value.as_ref().iter().fold(true, |valid, &c| {
    valid & ((c >= b' ' && c != 0x7f) | (c == b'\t'))
  })
}

/// The bytes of an HTTP token (RFC 9110), which is what a header name is.
static TOKEN_CHARS: [bool; 256] = {
  let mut table = [false; 256];
  let mut c = 0;
  while c < table.len() {
    table[c] = (c as u8).is_ascii_alphanumeric();
    c += 1;
  }
  let symbols = b"!#$%&'*+-.^_`|~";
  let mut i = 0;
  while i < symbols.len() {
    table[symbols[i] as usize] = true;
    i += 1;
  }
  table
};

pub(crate) struct HeaderPairSlice(
  pub(crate) *const crate::abi::envoy_dynamic_module_type_module_http_header,
  pub(crate) usize,
);

const _: () = {
  type HeaderPair<'a> = (&'a str, &'a [u8]);
  assert!(
    std::mem::size_of::<HeaderPair>()
      == std::mem::size_of::<crate::abi::envoy_dynamic_module_type_module_http_header>()
  );
  assert!(
    std::mem::align_of::<HeaderPair>()
      == std::mem::align_of::<crate::abi::envoy_dynamic_module_type_module_http_header>()
  );

  assert!(
    std::mem::offset_of!(HeaderPair, 0)
      == std::mem::offset_of!(
        crate::abi::envoy_dynamic_module_type_module_http_header,
        key_ptr
      )
  );
  assert!(
    std::mem::offset_of!(HeaderPair, 1)
      == std::mem::offset_of!(
        crate::abi::envoy_dynamic_module_type_module_http_header,
        value_ptr
      )
  );
};

impl<'a> From<&[(&'a str, &'a [u8])]> for HeaderPairSlice {
  fn from(headers: &[(&'a str, &'a [u8])]) -> Self {
    // Note: Casting a (&str, &[u8]) to an abi::envoy_dynamic_module_type_module_http_header works
    // not because of any formal layout guarantees but because:
    // 1) tuples _in practice_ are laid out packed and in order
    // 2) &str and &[u8] are fat pointers (pointers to DSTs), whose layouts _in practice_ are a
    //    pointer and length
    // If these assumptions change, this will break, so we assert on them here in debug builds.
    type HeaderPair<'a> = (&'a str, &'a [u8]);

    debug_assert!({
      let pair: HeaderPair<'_> = ("test", b"value");
      let constructed = crate::abi::envoy_dynamic_module_type_module_http_header {
        key_ptr: pair.0.as_ptr() as *const _,
        key_length: pair.0.len(),
        value_ptr: pair.1.as_ptr() as *const _,
        value_length: pair.1.len(),
      };
      let punned = unsafe {
        std::mem::transmute::<HeaderPair, crate::abi::envoy_dynamic_module_type_module_http_header>(
          pair,
        )
      };
      constructed == punned
    });

    let ptr = headers.as_ptr() as *const crate::abi::envoy_dynamic_module_type_module_http_header;
    HeaderPairSlice(ptr, headers.len())
  }
}

#[cfg(test)]
#[allow(static_mut_refs)]
mod tests {
  use super::*;
  use crate::{EnvoyMutBuffer, MockEnvoyHttpFilter};

  #[test]
  fn test_read_whole_request_body_received_is_buffered() {
    // When received_buffered_request_body() returns true (previous filter did StopAndBuffer and
    // resumed), only the buffered body should be read to avoid duplicating data.
    static mut BUFFER: [u8; 11] = *b"hello world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_request_body()
      .times(1)
      .returning(|| true);
    mock
      .expect_get_buffered_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFER) }]));
    // get_received_request_body_size and get_received_request_body should NOT be called.

    assert_eq!(read_whole_request_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_request_body_different_chunks() {
    // When received_buffered_request_body() returns false, both buffered and received are combined.
    static mut BUFFERED: [u8; 6] = *b"hello ";
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_request_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_received_request_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFERED) }]));
    mock
      .expect_get_received_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_request_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_request_body_empty_buffered() {
    // When buffered body is empty (None) and not the same, result equals the received body.
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_request_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_received_request_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_request_body()
      .times(1)
      .returning(|| None);
    mock
      .expect_get_received_request_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_request_body(&mut mock), b"world");
  }

  #[test]
  fn test_read_whole_request_body_multi_chunk_buffered() {
    // A multi-chunk buffered body is summed from its chunk lengths and concatenated in order.
    static mut CHUNK_ONE: [u8; 6] = *b"hello ";
    static mut CHUNK_TWO: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_request_body()
      .times(1)
      .returning(|| true);
    mock
      .expect_get_buffered_request_body()
      .times(1)
      .returning(|| {
        Some(vec![
          unsafe { EnvoyMutBuffer::new(&raw mut CHUNK_ONE) },
          unsafe { EnvoyMutBuffer::new(&raw mut CHUNK_TWO) },
        ])
      });

    assert_eq!(read_whole_request_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_response_body_received_is_buffered() {
    // When received_buffered_response_body() returns true, only the buffered body should be read.
    static mut BUFFER: [u8; 11] = *b"hello world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_response_body()
      .times(1)
      .returning(|| true);
    mock
      .expect_get_buffered_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFER) }]));
    // get_received_response_body_size and get_received_response_body should NOT be called.

    assert_eq!(read_whole_response_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_response_body_different_chunks() {
    // When received_buffered_response_body() returns false, both buffered and received are
    // combined.
    static mut BUFFERED: [u8; 6] = *b"hello ";
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_response_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_received_response_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut BUFFERED) }]));
    mock
      .expect_get_received_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_response_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_read_whole_response_body_empty_buffered() {
    // When buffered body is empty (None) and not the same, result equals the received body.
    static mut RECEIVED: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_response_body()
      .times(1)
      .returning(|| false);
    mock
      .expect_get_received_response_body_size()
      .times(1)
      .returning(|| 5);
    mock
      .expect_get_buffered_response_body()
      .times(1)
      .returning(|| None);
    mock
      .expect_get_received_response_body()
      .times(1)
      .returning(|| Some(vec![unsafe { EnvoyMutBuffer::new(&raw mut RECEIVED) }]));

    assert_eq!(read_whole_response_body(&mut mock), b"world");
  }

  #[test]
  fn test_read_whole_response_body_multi_chunk_buffered() {
    // A multi-chunk buffered body is summed from its chunk lengths and concatenated in order.
    static mut CHUNK_ONE: [u8; 6] = *b"hello ";
    static mut CHUNK_TWO: [u8; 5] = *b"world";
    let mut mock = MockEnvoyHttpFilter::default();
    mock
      .expect_received_buffered_response_body()
      .times(1)
      .returning(|| true);
    mock
      .expect_get_buffered_response_body()
      .times(1)
      .returning(|| {
        Some(vec![
          unsafe { EnvoyMutBuffer::new(&raw mut CHUNK_ONE) },
          unsafe { EnvoyMutBuffer::new(&raw mut CHUNK_TWO) },
        ])
      });

    assert_eq!(read_whole_response_body(&mut mock), b"hello world");
  }

  #[test]
  fn test_is_valid_header_name() {
    for name in ["x-custom", "X-Mixed-Case", ":path", "a!#$%&'*+-.^_`|~0"] {
      assert!(is_valid_header_name(name), "{name:?}");
    }
    assert!(is_valid_header_name(b"x-bytes".as_slice()));
    for name in [
      "",
      ":",
      "::path",
      "x:y",
      "bad name",
      "x-bad\r",
      "x-bad\n",
      "x-bad\0",
      "caf\u{e9}",
    ] {
      assert!(!is_valid_header_name(name), "{name:?}");
    }
  }

  #[test]
  fn test_is_valid_header_value() {
    for value in [&b""[..], b"value", b"a\tb c", b"\x80\xff"] {
      assert!(is_valid_header_value(value), "{value:?}");
    }
    for value in [
      &b"a\rb"[..],
      b"a\nb",
      b"a\0b",
      b"bell\x07",
      b"del\x7f",
      b"value\r\nx-injected: 1",
    ] {
      assert!(!is_valid_header_value(value), "{value:?}");
    }
  }

  #[test]
  fn test_header_checks_every_byte() {
    for c in 0 ..= u8::MAX {
      let token = c.is_ascii_alphanumeric() || b"!#$%&'*+-.^_`|~".contains(&c);
      assert_eq!(is_valid_header_name([c]), token, "{c:#x}");
      // In the middle and at the end of a value long enough for the compiler to vectorize.
      let allowed = c == b'\t' || (c >= b' ' && c != 0x7f);
      for position in [40, 66] {
        let mut value = [b'v'; 67];
        value[position] = c;
        assert_eq!(
          is_valid_header_value(value),
          allowed,
          "{c:#x} at {position}"
        );
      }
    }
  }
}
