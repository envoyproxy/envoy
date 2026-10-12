dynamic modules: added an exception barrier to the C++ SDK so an exception thrown from a module hook
is caught at the ABI boundary and fails closed instead of aborting the process. This mirrors the Rust
SDK panic barrier.
