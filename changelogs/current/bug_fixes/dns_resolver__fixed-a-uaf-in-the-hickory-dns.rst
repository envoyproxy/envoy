Fixed a UAF in the Hickory DNS resolver where the dispatcher lambda posted by the
Rust-thread completion callback captured a raw pointer to the resolver. If the resolver was
destroyed before the dispatcher drained the lambda, dereferencing the pointer was undefined
behavior. The lambda now captures a ``std::weak_ptr`` and locks it before touching the resolver.
