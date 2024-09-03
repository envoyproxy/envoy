## Dynamic Modules SDKs

This directory contains the SDKs for the Dynamic Modules feature. Each SDK passes the same set of tests and
is guaranteed to provide the same functionality.

Each SDK has a hard copy of the ABI header file in order for them to be able to compile off-tree. Rust and Go build system cannot handle symlinks.
