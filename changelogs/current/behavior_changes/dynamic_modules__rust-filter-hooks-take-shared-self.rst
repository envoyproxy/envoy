The Rust dynamic-module SDK's ``HttpFilter`` hooks now take ``&self`` instead of ``&mut self``.
Existing filters must update their hook signatures and hold any mutable per-stream state behind
interior mutability (``Cell``/``RefCell``), and their ``Drop`` must not panic. This closes a
use-after-free: a filter hook that triggered a synchronous teardown of the filter chain (for example
``recreate_stream``) could return to a freed in-module filter. The filter is now reference-counted
for the duration of each hook, which requires shared borrows, since Envoy can re-enter the filter
synchronously while a hook is still on the stack and two aliasing ``&mut self`` would be undefined
behavior.
