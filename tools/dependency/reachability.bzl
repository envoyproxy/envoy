"""Envoy-specific dependency reachability rule.

``rule()`` may only be called during ``.bzl`` initialization (top-level
evaluation), so the constructed rule cannot live in ``BUILD``.  This module
exists solely to construct it once, here, and re-export the wrapping macro for
use from ``tools/dependency/BUILD``.

The wasm runtime is selected via the
``@proxy-wasm-cpp-host//bazel:engine`` build setting, with ``v8``, ``wamr``,
and ``wasmtime`` values passed by ``tools/dependency/BUILD``. The flag label is
resolved via ``str(Label(...))`` so it is interpreted in the consuming
repository rather than in ``@envoy_toolshed``, matching the toolshed guidance
for transition flags defined outside the rule's repository.

``flags``/``defines`` must be fixed at rule construction because transition
``outputs`` are static and cannot vary per target instantiation.  A single
constructed rule can be instantiated any number of times with different
``configs``.
"""

load(
    "@envoy_toolshed//dependency:reachability.bzl",
    "dependency_reachability_macro",
    "dependency_reachability_rule",
)

WASM_RUNTIME_FLAG = str(Label("@proxy-wasm-cpp-host//bazel:engine"))

_envoy_dependency_reachability_rule = dependency_reachability_rule(
    flags = [WASM_RUNTIME_FLAG],
)

envoy_dependency_reachability = dependency_reachability_macro(
    _envoy_dependency_reachability_rule,
)
