dynamic modules: fixed a bug where the Rust SDK cluster ``add_hosts`` methods passed weights,
localities, and metadata to the host without checking their lengths against the address count. A
module could trigger an out of bounds read in the host by passing mismatched or ragged slices. The
SDK now returns ``None`` before crossing the ABI when the slice lengths are inconsistent.
