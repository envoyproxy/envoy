use crate::buffer::*;

#[test]
fn test_envoy_buffer_as_mut_slice() {
  let mut envoy_buffer = EnvoyBuffer::default();
  envoy_buffer
    .expect_as_mut_slice()
    .returning(|| (&mut [1, 2, 3]).to_vec());
  assert_eq!(envoy_buffer.as_mut_slice(), [1, 2, 3]);
}

#[test]
fn test_envoy_buffer_as_slice() {
  let mut envoy_buffer = EnvoyBuffer::default();
  envoy_buffer
    .expect_as_slice()
    .return_const((&[1, 2, 3]).to_vec());
  assert_eq!(envoy_buffer.as_slice(), [1, 2, 3]);
}
