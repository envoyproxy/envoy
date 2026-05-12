#!/usr/bin/env python3
"""narrow_test_mocks.py — Detect and fix over-broad test-mock includes.

Scans test source files for over-broad ``#include`` directives from the
``test/mocks/server/`` family (and, in future, other mock families) and
replaces them with the narrowest header(s) that still cover all referenced
``Mock*`` symbols.  The matching Bazel BUILD file's ``deps`` list is updated
to match.

Usage
-----
Check mode (default) — report candidates, exit 1 if any found::

    bazel run //tools/code:narrow_test_mocks -- --mode check

Fix mode — apply rewrites in place::

    bazel run //tools/code:narrow_test_mocks -- --mode fix

The rules table is intentionally kept separate from the scanning / rewriting
logic so that adding a new mock family (e.g. ``test/mocks/network/``) only
requires extending the ``FAMILY_RULES`` dict at the top of the file.
"""

import argparse
import os
import re
import sys
from typing import Dict, FrozenSet, List, Optional, Set, Tuple

# ---------------------------------------------------------------------------
# Rules table
# ---------------------------------------------------------------------------
# Each family entry is a dict with the following keys:
#
#   symbol_to_header_dep
#       Maps every Mock* symbol name to the (narrow_header_path, bazel_dep)
#       tuple that is the *narrowest* place the symbol is defined.
#
#   broad_headers
#       Maps over-broad header paths to (bazel_dep, frozenset_of_own_symbols).
#       "own symbols" = Mock* classes defined *directly* in that header
#       (not just re-exported via transitive #includes).
#
#   dep_dominates
#       Maps a Bazel dep D to the set of deps that D already pulls in
#       transitively.  Used to remove redundant entries from the dep list.
#
#   include_dominates
#       Maps a C++ header H to the set of test/mocks/… headers that H already
#       pulls in via transitive #includes.  Used to remove redundant entries
#       from the include list.

# ---------------------------------------------------------------------------
# Safety flag
# ---------------------------------------------------------------------------
# When False (the default), _analyze_file() will skip any test file that
# references *no* server Mock* symbol at all.  Such files may be silently
# relying on the server-mock dep for its transitive includes (e.g.
# Singleton::ManagerImpl, Api::createApiForTest, various TLS/HTTP context
# libs, etc.).  Fully removing the dep in that case causes IWYU-style build
# failures.  Set to True only when you explicitly want to allow full removal.
ALLOW_FULL_REMOVAL: bool = False

# ---------------------------------------------------------------------------
# Transitive-include safety guard
# ---------------------------------------------------------------------------
# When the script narrows a broad server-mock include (e.g. instance.h →
# admin.h), symbols that the file relied upon via the *broader* header's
# transitive include chain can become invisible to the compiler.  This table
# maps regex patterns for such "at-risk" non-server symbols to the direct
# #include that must already be present in the file for the narrowing to be
# safe.
#
# Each entry is a tuple:
#   (symbol_pattern, required_direct_include, bazel_dep)
#
# If the source file contains a token matching *symbol_pattern* and does NOT
# already have a direct `#include "required_direct_include"` line, the
# narrowing is skipped (default safe mode) to avoid breaking the build.
#
# Extend this table when new cross-subsystem transitive-include classes are
# discovered.
TRANSITIVE_GUARD: List[Tuple[str, str, str]] = [
    # HTTP mocks — Http::MockStreamDecoderFilterCallbacks, etc.
    (
        r"\bHttp::Mock\w+",
        "test/mocks/http/mocks.h",
        "//test/mocks/http:http_mocks",
    ),
    # HTTP test map types — Http::TestRequestHeaderMapImpl, etc.
    (
        r"\bHttp::Test\w+(?:Header|Trailer)Map\w*",
        "test/test_common/utility.h",
        "//test/test_common:utility_lib",
    ),
    # API mocks — Api::MockApi, etc.
    (
        r"\bApi::Mock\w+",
        "test/mocks/api/mocks.h",
        "//test/mocks/api:api_mocks",
    ),
    # createApiForTest helper
    (
        r"\bApi::createApiForTest\b",
        "test/test_common/utility.h",
        "//test/test_common:utility_lib",
    ),
    # Network mocks — Network::MockConnection, etc.
    (
        r"\bNetwork::Mock\w+",
        "test/mocks/network/mocks.h",
        "//test/mocks/network:network_mocks",
    ),
    # Upstream mocks — Upstream::MockClusterManager, etc.
    (
        r"\bUpstream::Mock\w+",
        "test/mocks/upstream/mocks.h",
        "//test/mocks/upstream:upstream_mocks",
    ),
    # Stats mocks — Stats::MockStore, etc.
    (
        r"\bStats::Mock\w+",
        "test/mocks/stats/mocks.h",
        "//test/mocks/stats:stats_mocks",
    ),
    # Runtime mocks — Runtime::MockLoader, etc.
    (
        r"\bRuntime::Mock\w+",
        "test/mocks/runtime/mocks.h",
        "//test/mocks/runtime:runtime_mocks",
    ),
    # Tracing mocks — Tracing::MockTracer, etc.
    (
        r"\bTracing::Mock\w+",
        "test/mocks/tracing/mocks.h",
        "//test/mocks/tracing:tracing_mocks",
    ),
    # Singleton::ManagerImpl
    (
        r"\bSingleton::ManagerImpl\b",
        "source/common/singleton/manager_impl.h",
        "//source/common/singleton:manager_impl_lib",
    ),
    # TestUtility / TestEnvironment helpers
    (
        r"\bTestUtility::\w+",
        "test/test_common/utility.h",
        "//test/test_common:utility_lib",
    ),
    (
        r"\bTestEnvironment::\w+",
        "test/test_common/utility.h",
        "//test/test_common:utility_lib",
    ),
]

FAMILY_RULES: Dict = {
    "server": {
        "symbol_to_header_dep": {
            # server_factory_context.h
            "MockServerFactoryContext": (
                "test/mocks/server/server_factory_context.h",
                "//test/mocks/server:server_factory_context_mocks",
            ),
            "MockStatsConfig": (
                "test/mocks/server/server_factory_context.h",
                "//test/mocks/server:server_factory_context_mocks",
            ),
            "MockGenericFactoryContext": (
                "test/mocks/server/server_factory_context.h",
                "//test/mocks/server:server_factory_context_mocks",
            ),
            # instance.h
            "MockInstance": (
                "test/mocks/server/instance.h",
                "//test/mocks/server:instance_mocks",
            ),
            # factory_context.h
            "MockFactoryContext": (
                "test/mocks/server/factory_context.h",
                "//test/mocks/server:factory_context_mocks",
            ),
            "MockUpstreamFactoryContext": (
                "test/mocks/server/factory_context.h",
                "//test/mocks/server:factory_context_mocks",
            ),
            # listener_factory_context.h
            "MockListenerFactoryContext": (
                "test/mocks/server/listener_factory_context.h",
                "//test/mocks/server:listener_factory_context_mocks",
            ),
            # filter_chain_factory_context.h
            "MockFilterChainFactoryContext": (
                "test/mocks/server/filter_chain_factory_context.h",
                "//test/mocks/server:filter_chain_factory_context_mocks",
            ),
            # health_checker_factory_context.h
            "MockHealthCheckerFactoryContext": (
                "test/mocks/server/health_checker_factory_context.h",
                "//test/mocks/server:health_checker_factory_context_mocks",
            ),
            # tracer_factory_context.h / tracer_factory.h
            "MockTracerFactoryContext": (
                "test/mocks/server/tracer_factory_context.h",
                "//test/mocks/server:tracer_factory_context_mocks",
            ),
            "MockTracerFactory": (
                "test/mocks/server/tracer_factory.h",
                "//test/mocks/server:tracer_factory_mocks",
            ),
            # Individual narrow targets
            "MockAdmin": (
                "test/mocks/server/admin.h",
                "//test/mocks/server:admin_mocks",
            ),
            "MockAdminStream": (
                "test/mocks/server/admin_stream.h",
                "//test/mocks/server:admin_stream_mocks",
            ),
            "MockBootstrapExtension": (
                "test/mocks/server/bootstrap_extension_factory.h",
                "//test/mocks/server:bootstrap_extension_factory_mocks",
            ),
            "MockBootstrapExtensionFactory": (
                "test/mocks/server/bootstrap_extension_factory.h",
                "//test/mocks/server:bootstrap_extension_factory_mocks",
            ),
            "MockConfigTracker": (
                "test/mocks/server/config_tracker.h",
                "//test/mocks/server:config_tracker_mocks",
            ),
            "MockDrainManager": (
                "test/mocks/server/drain_manager.h",
                "//test/mocks/server:drain_manager_mocks",
            ),
            "MockFatalActionFactory": (
                "test/mocks/server/fatal_action_factory.h",
                "//test/mocks/server:fatal_action_factory_mocks",
            ),
            "MockGuardDog": (
                "test/mocks/server/guard_dog.h",
                "//test/mocks/server:guard_dog_mocks",
            ),
            "MockHotRestart": (
                "test/mocks/server/hot_restart.h",
                "//test/mocks/server:hot_restart_mocks",
            ),
            "MockListenerComponentFactory": (
                "test/mocks/server/listener_component_factory.h",
                "//test/mocks/server:listener_component_factory_mocks",
            ),
            "MockListenerManager": (
                "test/mocks/server/listener_manager.h",
                "//test/mocks/server:listener_manager_mocks",
            ),
            "MockListenerUpdateCallbacks": (
                "test/mocks/server/listener_update_callbacks.h",
                "//test/mocks/server:listener_update_callbacks_mocks",
            ),
            "MockListenerUpdateCallbacksHandle": (
                "test/mocks/server/listener_update_callbacks_handle.h",
                "//test/mocks/server:listener_update_callbacks_handle_mocks",
            ),
            "MockMain": (
                "test/mocks/server/main.h",
                "//test/mocks/server:main_mocks",
            ),
            "MockOptions": (
                "test/mocks/server/options.h",
                "//test/mocks/server:options_mocks",
            ),
            "MockOverloadManager": (
                "test/mocks/server/overload_manager.h",
                "//test/mocks/server:overload_manager_mocks",
            ),
            "MockThreadLocalOverloadState": (
                "test/mocks/server/overload_manager.h",
                "//test/mocks/server:overload_manager_mocks",
            ),
            "MockLoadShedPoint": (
                "test/mocks/server/overload_manager.h",
                "//test/mocks/server:overload_manager_mocks",
            ),
            "MockServerLifecycleNotifier": (
                "test/mocks/server/server_lifecycle_notifier.h",
                "//test/mocks/server:server_lifecycle_notifier_mocks",
            ),
            "MockWatchDog": (
                "test/mocks/server/watch_dog.h",
                "//test/mocks/server:watch_dog_mocks",
            ),
            "MockWatchdog": (
                "test/mocks/server/watchdog_config.h",
                "//test/mocks/server:watchdog_config_mocks",
            ),
            "MockWorker": (
                "test/mocks/server/worker.h",
                "//test/mocks/server:worker_mocks",
            ),
            "MockWorkerFactory": (
                "test/mocks/server/worker_factory.h",
                "//test/mocks/server:worker_factory_mocks",
            ),
        },
        # Broad headers: header_path -> (bazel_dep, own_symbols)
        # "own symbols" = Mock classes *defined* directly in this header.
        "broad_headers": {
            "test/mocks/server/mocks.h": (
                "//test/mocks/server:server_mocks",
                frozenset(),
            ),
            "test/mocks/server/instance.h": (
                "//test/mocks/server:instance_mocks",
                frozenset({"MockInstance"}),
            ),
            "test/mocks/server/factory_context.h": (
                "//test/mocks/server:factory_context_mocks",
                frozenset({"MockFactoryContext", "MockUpstreamFactoryContext"}),
            ),
            "test/mocks/server/listener_factory_context.h": (
                "//test/mocks/server:listener_factory_context_mocks",
                frozenset({"MockListenerFactoryContext"}),
            ),
            "test/mocks/server/filter_chain_factory_context.h": (
                "//test/mocks/server:filter_chain_factory_context_mocks",
                frozenset({"MockFilterChainFactoryContext"}),
            ),
            "test/mocks/server/health_checker_factory_context.h": (
                "//test/mocks/server:health_checker_factory_context_mocks",
                frozenset({"MockHealthCheckerFactoryContext"}),
            ),
            "test/mocks/server/tracer_factory_context.h": (
                "//test/mocks/server:tracer_factory_context_mocks",
                frozenset({"MockTracerFactoryContext"}),
            ),
        },
        # dep_dominates[D] = set of deps that D already pulls in transitively.
        # If both D and some d in dep_dominates[D] appear in the dep list,
        # the smaller d is redundant and can be removed.
        "dep_dominates": {
            "//test/mocks/server:server_mocks": {
                "//test/mocks/server:instance_mocks",
                "//test/mocks/server:factory_context_mocks",
                "//test/mocks/server:listener_factory_context_mocks",
                "//test/mocks/server:filter_chain_factory_context_mocks",
                "//test/mocks/server:health_checker_factory_context_mocks",
                "//test/mocks/server:tracer_factory_context_mocks",
                "//test/mocks/server:server_factory_context_mocks",
                "//test/mocks/server:admin_mocks",
                "//test/mocks/server:admin_stream_mocks",
                "//test/mocks/server:bootstrap_extension_factory_mocks",
                "//test/mocks/server:config_tracker_mocks",
                "//test/mocks/server:drain_manager_mocks",
                "//test/mocks/server:fatal_action_factory_mocks",
                "//test/mocks/server:guard_dog_mocks",
                "//test/mocks/server:hot_restart_mocks",
                "//test/mocks/server:listener_component_factory_mocks",
                "//test/mocks/server:listener_manager_mocks",
                "//test/mocks/server:listener_update_callbacks_mocks",
                "//test/mocks/server:listener_update_callbacks_handle_mocks",
                "//test/mocks/server:main_mocks",
                "//test/mocks/server:options_mocks",
                "//test/mocks/server:overload_manager_mocks",
                "//test/mocks/server:server_lifecycle_notifier_mocks",
                "//test/mocks/server:tracer_factory_mocks",
                "//test/mocks/server:watch_dog_mocks",
                "//test/mocks/server:watchdog_config_mocks",
                "//test/mocks/server:worker_mocks",
                "//test/mocks/server:worker_factory_mocks",
            },
            "//test/mocks/server:listener_factory_context_mocks": {
                "//test/mocks/server:factory_context_mocks",
                "//test/mocks/server:instance_mocks",
                "//test/mocks/server:server_factory_context_mocks",
            },
            "//test/mocks/server:filter_chain_factory_context_mocks": {
                "//test/mocks/server:factory_context_mocks",
                "//test/mocks/server:instance_mocks",
                "//test/mocks/server:server_factory_context_mocks",
            },
            "//test/mocks/server:health_checker_factory_context_mocks": {
                "//test/mocks/server:factory_context_mocks",
                "//test/mocks/server:instance_mocks",
                "//test/mocks/server:server_factory_context_mocks",
            },
            "//test/mocks/server:factory_context_mocks": {
                "//test/mocks/server:instance_mocks",
                "//test/mocks/server:server_factory_context_mocks",
            },
            "//test/mocks/server:tracer_factory_context_mocks": {
                "//test/mocks/server:instance_mocks",
                "//test/mocks/server:server_factory_context_mocks",
            },
            "//test/mocks/server:instance_mocks": {
                "//test/mocks/server:server_factory_context_mocks",
            },
        },
        # include_dominates[H] = set of server-mock headers already pulled in
        # by H via transitive #includes.  Used to drop redundant explicit
        # includes after computing the minimum replacement set.
        "include_dominates": {
            "test/mocks/server/mocks.h": {
                "test/mocks/server/instance.h",
                "test/mocks/server/factory_context.h",
                "test/mocks/server/listener_factory_context.h",
                "test/mocks/server/filter_chain_factory_context.h",
                "test/mocks/server/health_checker_factory_context.h",
                "test/mocks/server/tracer_factory_context.h",
                "test/mocks/server/server_factory_context.h",
                "test/mocks/server/admin.h",
                "test/mocks/server/admin_stream.h",
                "test/mocks/server/bootstrap_extension_factory.h",
                "test/mocks/server/config_tracker.h",
                "test/mocks/server/drain_manager.h",
                "test/mocks/server/fatal_action_factory.h",
                "test/mocks/server/guard_dog.h",
                "test/mocks/server/hot_restart.h",
                "test/mocks/server/listener_component_factory.h",
                "test/mocks/server/listener_manager.h",
                "test/mocks/server/listener_update_callbacks.h",
                "test/mocks/server/listener_update_callbacks_handle.h",
                "test/mocks/server/main.h",
                "test/mocks/server/options.h",
                "test/mocks/server/overload_manager.h",
                "test/mocks/server/server_lifecycle_notifier.h",
                "test/mocks/server/tracer_factory.h",
                "test/mocks/server/watch_dog.h",
                "test/mocks/server/watchdog_config.h",
                "test/mocks/server/worker.h",
                "test/mocks/server/worker_factory.h",
            },
            # factory_context.h includes instance.h (which includes server_factory_context.h)
            "test/mocks/server/factory_context.h": {
                "test/mocks/server/instance.h",
                "test/mocks/server/server_factory_context.h",
            },
            # listener_factory_context.h includes instance.h directly
            "test/mocks/server/listener_factory_context.h": {
                "test/mocks/server/instance.h",
                "test/mocks/server/server_factory_context.h",
            },
            # filter_chain_factory_context.h includes factory_context.h (relative)
            "test/mocks/server/filter_chain_factory_context.h": {
                "test/mocks/server/factory_context.h",
                "test/mocks/server/instance.h",
                "test/mocks/server/server_factory_context.h",
            },
            # health_checker_factory_context.h includes factory_context.h
            "test/mocks/server/health_checker_factory_context.h": {
                "test/mocks/server/factory_context.h",
                "test/mocks/server/instance.h",
                "test/mocks/server/server_factory_context.h",
            },
            # tracer_factory_context.h includes instance.h
            "test/mocks/server/tracer_factory_context.h": {
                "test/mocks/server/instance.h",
                "test/mocks/server/server_factory_context.h",
            },
            # instance.h includes server_factory_context.h
            "test/mocks/server/instance.h": {
                "test/mocks/server/server_factory_context.h",
            },
        },
    },
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)

# Matches any word that starts with Mock followed by an upper-case letter,
# capturing only the identifier (no namespace prefix required here).
_SYMBOL_RE = re.compile(r'\bMock[A-Z]\w*\b')

# Matches 'using namespace …' lines that could create ambiguity.
_USING_NS_RE = re.compile(r'\busing\s+namespace\b')


def _get_includes(content: str) -> List[str]:
    """Return all #include paths from *content*."""
    return _INCLUDE_RE.findall(content)


def _get_symbols(content: str) -> Set[str]:
    """Return all Mock* identifiers referenced in *content*."""
    return set(_SYMBOL_RE.findall(content))


def _has_using_namespace(content: str) -> bool:
    return bool(_USING_NS_RE.search(content))


def _check_transitive_guard(
    content: str,
    all_includes: Set[str],
    verbose: bool = False,
    filepath: str = "",
) -> bool:
    """Return False (skip) if narrowing would break a transitive-include chain.

    Scans *content* for non-server symbols listed in TRANSITIVE_GUARD.  For
    each matching symbol, checks whether the required direct ``#include`` is
    already present in the file's explicit include list.  If a symbol is used
    but its required include is missing, the narrowing would silently drop that
    header from the compilation unit — so we skip the file.
    """
    for pattern, required_header, _dep in TRANSITIVE_GUARD:
        if re.search(pattern, content) and required_header not in all_includes:
            if verbose:
                print(
                    f"  SKIP {filepath}: matches '{pattern}' but lacks direct "
                    f'#include "{required_header}" — narrowing would break '
                    "transitive-include chain"
                )
            return False
    return True


def _reduce_includes(includes: Set[str], dominance: Dict[str, Set[str]]) -> Set[str]:
    """Remove headers from *includes* that are already covered by a
    more-specific header in the same set (via the dominance relation).
    """
    result = set(includes)
    for header in list(includes):
        dominated = dominance.get(header, set())
        # If another header in the set is dominated by 'header', that other
        # header is redundant (header is broader and already covers it).
        # We want to KEEP the narrowest, so we remove *header* if some
        # other member already dominates it.
        pass
    # Build the actual reduction: remove h if some h' in result has h in its
    # dominance set (i.e., h' → h, meaning h is already covered by h').
    to_remove = set()
    for h in result:
        for h2 in result:
            if h != h2 and h in dominance.get(h2, set()):
                to_remove.add(h)
                break
    return result - to_remove


def _reduce_deps(deps: Set[str], dominance: Dict[str, Set[str]]) -> Set[str]:
    """Same as _reduce_includes but for Bazel dep strings."""
    to_remove = set()
    for d in deps:
        for d2 in deps:
            if d != d2 and d in dominance.get(d2, set()):
                to_remove.add(d)
                break
    return deps - to_remove


# ---------------------------------------------------------------------------
# Per-file analysis
# ---------------------------------------------------------------------------

def _analyze_file(
    content: str,
    rules: Dict,
    verbose: bool = False,
    filepath: str = "",
) -> Optional[Tuple[Set[str], Set[str], Set[str], Set[str]]]:
    """Analyse a single source file and return the proposed changes, or None
    if no change is needed (or if the file should be skipped).

    Returns a 4-tuple:
        (old_includes, new_includes, old_deps, new_deps)
    where old_* are the current server-mock includes/deps referenced in the
    file, and new_* are the recommended replacements.

    Returns None when no narrowing is possible.
    """
    s2hd = rules["symbol_to_header_dep"]
    broad = rules["broad_headers"]
    dep_dom = rules["dep_dominates"]
    inc_dom = rules["include_dominates"]

    all_includes = set(_get_includes(content))
    all_symbols = _get_symbols(content)

    # Collect the set of currently-included "broad" server mock headers.
    current_broad = {h for h in all_includes if h in broad}
    # Also collect the non-broad server mock headers already included.
    all_server_headers = set(s2hd[sym][0] for sym in s2hd)
    current_narrow = {h for h in all_includes if h in all_server_headers and h not in broad}

    if not current_broad:
        return None  # Nothing to narrow

    # Safety: if the file uses 'using namespace', be conservative.
    if _has_using_namespace(content):
        if verbose:
            print(f"  SKIP {filepath}: uses 'using namespace', cannot safely narrow")
        return None

    # Find server-mock symbols actually referenced.
    known_symbols = set(s2hd.keys())
    used_server_symbols = all_symbols & known_symbols

    # Safety: if no server Mock* symbols are referenced, the test may be
    # relying on this dep solely for its transitive includes (e.g.
    # Singleton::ManagerImpl, Api::createApiForTest).  Never fully remove
    # the dep with no replacement unless ALLOW_FULL_REMOVAL is True.
    if not used_server_symbols and not ALLOW_FULL_REMOVAL:
        if verbose:
            print(
                f"  SKIP {filepath}: no server Mock* symbols referenced; "
                "skipping to avoid removing dep relied upon for transitive includes"
            )
        return None

    # Map each used symbol to its canonical narrow header and dep.
    needed_headers: Set[str] = set()
    needed_deps: Set[str] = set()
    for sym in used_server_symbols:
        h, d = s2hd[sym]
        needed_headers.add(h)
        needed_deps.add(d)

    # Add any currently-included narrow headers that are not covered by the
    # needed set (they were already correct and we should keep them).
    for h in current_narrow:
        needed_headers.add(h)
        # Find corresponding dep for this narrow header.
        dep_for_h = next(
            (s2hd[sym][1] for sym in s2hd if s2hd[sym][0] == h),
            None,
        )
        if dep_for_h:
            needed_deps.add(dep_for_h)

    # Apply dominance reduction: if header A already covers header B (because
    # A transitively includes B), drop B from the needed set.
    needed_headers = _reduce_includes(needed_headers, inc_dom)
    needed_deps = _reduce_deps(needed_deps, dep_dom)

    # Compute old_includes = all broad server mock headers currently present.
    old_includes = current_broad

    # new_includes = needed_headers minus what is already explicitly included
    # via a non-broad header (i.e., already present and not being replaced).
    new_includes = needed_headers

    # old_deps derived from broad headers currently included.
    old_deps = {broad[h][0] for h in current_broad}

    new_deps = needed_deps

    # Check whether a change is actually needed.
    if old_includes == new_includes and old_deps == new_deps:
        return None

    # Also skip if the "new" set is not strictly narrower (safety guard).
    # We never want to remove a narrow header and add a broader one.
    for h in new_includes:
        if h in broad:
            # The replacement itself is a broad header — only acceptable if it
            # is narrower than what we started with.
            old_broad_dep = old_deps
            new_broad_dep = {broad[h][0]}
            if not (old_broad_dep > new_broad_dep):
                if verbose:
                    print(
                        f"  SKIP {filepath}: replacement set is not strictly "
                        "narrower than original"
                    )
                return None

    # Transitive-include safety guard: if the file uses non-server symbols
    # (e.g. Http::MockStreamDecoderFilterCallbacks) that would have been
    # visible via the broader header's transitive include chain, but those
    # symbols lack their own direct #include, the narrowing would silently
    # break the build.  Skip in that case.
    if not _check_transitive_guard(content, all_includes, verbose, filepath):
        return None

    return old_includes, new_includes, old_deps, new_deps


# ---------------------------------------------------------------------------
# Source file rewriting
# ---------------------------------------------------------------------------

def _rewrite_includes(content: str, old_includes: Set[str], new_includes: Set[str]) -> str:
    """Replace the over-broad #include lines with the narrow set.

    Strategy:
    1. Find the position of the first broad include line.
    2. Delete all broad include lines.
    3. Insert the new narrow includes at the position of the first deletion,
       deduplicated against already-present includes.
    """
    lines = content.splitlines(keepends=True)
    already_present = set(_get_includes(content)) - old_includes

    # Determine which new includes actually need to be added.
    to_add = sorted(new_includes - already_present)

    # Build the output line-by-line.
    insertion_done = False
    first_broad_idx = None
    result: List[str] = []
    broad_lines_to_skip: Set[int] = set()

    for idx, line in enumerate(lines):
        m = _INCLUDE_RE.match(line)
        if m and m.group(1) in old_includes:
            broad_lines_to_skip.add(idx)
            if first_broad_idx is None:
                first_broad_idx = idx

    for idx, line in enumerate(lines):
        if idx in broad_lines_to_skip:
            if not insertion_done:
                for h in to_add:
                    result.append(f'#include "{h}"\n')
                insertion_done = True
            # Skip the old broad include line.
            continue
        result.append(line)

    # Edge case: if no broad lines were found in the end (shouldn't happen).
    if not insertion_done:
        for h in to_add:
            result.append(f'#include "{h}"\n')

    return "".join(result)


# ---------------------------------------------------------------------------
# BUILD file rewriting
# ---------------------------------------------------------------------------

def _find_build_file(cc_path: str) -> Optional[str]:
    """Return the path to the BUILD file in the same directory as *cc_path*."""
    build = os.path.join(os.path.dirname(cc_path), "BUILD")
    return build if os.path.isfile(build) else None


# Regex to find a Bazel string dep inside a deps = [...] list.
_DEP_LINE_RE = re.compile(
    r'^(\s*)"(//[^"]+)"(\s*,?\s*)$',
)


def _rewrite_build_deps(
    build_content: str,
    cc_filename: str,
    old_deps: Set[str],
    new_deps: Set[str],
    verbose: bool = False,
) -> Optional[str]:
    """Update the deps list of the Bazel target whose srcs include *cc_filename*.

    Returns the new content string, or None if no change was made.
    """
    # Locate the target block that contains cc_filename in its srcs.
    # We use a simple heuristic: find the target whose srcs = [...] list
    # contains the filename, then update its deps = [...] list.

    # Split into "stanzas" (top-level rule calls) by tracking brace depth.
    # Each stanza is a tuple (start_line_idx, end_line_idx).
    stanzas = _split_into_stanzas(build_content)

    lines = build_content.splitlines(keepends=True)

    for start, end in stanzas:
        stanza_text = "".join(lines[start:end])
        # Check if this stanza's srcs contains our file.
        if not re.search(
            r'srcs\s*=\s*\[[^\]]*"' + re.escape(cc_filename) + r'"[^\]]*\]',
            stanza_text,
            re.DOTALL,
        ):
            continue

        # Found the right stanza.  Now rewrite its deps.
        new_stanza_text = _rewrite_stanza_deps(
            stanza_text, old_deps, new_deps, verbose
        )
        if new_stanza_text is None:
            return None

        # Reconstruct the full file.
        return (
            "".join(lines[:start])
            + new_stanza_text
            + "".join(lines[end:])
        )

    if verbose:
        print(
            f"  WARNING: could not find Bazel target with srcs containing "
            f'"{cc_filename}" in BUILD file'
        )
    return None


def _split_into_stanzas(content: str) -> List[Tuple[int, int]]:
    """Split *content* into (start, end) line-index pairs for each top-level
    Bazel rule call (identified by brace depth going 0 → 1 → 0).
    """
    lines = content.splitlines(keepends=True)
    stanzas: List[Tuple[int, int]] = []
    depth = 0
    start = None
    for idx, line in enumerate(lines):
        for ch in line:
            if ch == '(':
                if depth == 0:
                    start = idx
                depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0 and start is not None:
                    stanzas.append((start, idx + 1))
                    start = None
    return stanzas


def _rewrite_stanza_deps(
    stanza: str,
    old_deps: Set[str],
    new_deps: Set[str],
    verbose: bool = False,
) -> Optional[str]:
    """Within a single rule-call stanza, replace old_deps with new_deps.

    Preserves the existing indentation style and sorts the final deps list.
    Returns None if no change is needed.
    """
    # Find the deps = [ ... ] block.
    deps_block_re = re.compile(
        r'(deps\s*=\s*\[)(.*?)(\])',
        re.DOTALL,
    )
    m = deps_block_re.search(stanza)
    if not m:
        if verbose:
            print("  WARNING: no deps = [...] found in stanza")
        return None

    prefix = m.group(1)
    body = m.group(2)
    suffix = m.group(3)

    # Extract individual dep strings.
    dep_entry_re = re.compile(r'"([^"]+)"')
    existing_deps = set(dep_entry_re.findall(body))

    # Apply the substitution.
    updated_deps = (existing_deps - old_deps) | new_deps

    if updated_deps == existing_deps:
        return None

    # Detect the indentation of the 'deps =' keyword itself — that is also
    # the correct indentation for the closing ']'.
    close_indent_m = re.search(r'^(\s*)deps\s*=\s*\[', stanza, re.MULTILINE)
    close_indent = close_indent_m.group(1) if close_indent_m else "    "

    # Detect per-entry indentation from the first dep line in the body.
    indent = close_indent + "    "  # default: 4 extra spaces
    for line in body.splitlines():
        stripped = line.lstrip()
        if stripped.startswith('"'):
            indent = line[: len(line) - len(stripped)]
            break

    # Rebuild the deps block, sorted.
    sorted_deps = sorted(updated_deps)
    new_body = "\n" + "".join(f'{indent}"{d}",\n' for d in sorted_deps)
    new_body += close_indent

    new_stanza = stanza[: m.start()] + prefix + new_body + suffix + stanza[m.end():]
    return new_stanza


# ---------------------------------------------------------------------------
# File scanning
# ---------------------------------------------------------------------------

_SKIP_DIRS = frozenset({
    ".git",
    "bazel-bin",
    "bazel-envoy",
    "bazel-out",
    "bazel-testlogs",
    "generated_api_shadow",
})

_SKIP_FILE_PATTERNS = re.compile(
    r'(\.pb\.(cc|h)|\.pb\.validate\.(cc|h)|_pb2\.py)$'
)

# Path fragments that indicate a file is a mock *definition* (not a test file
# using mocks).  We never want to analyse or rewrite these.
_MOCK_DEF_PATH_RE = re.compile(r'[/\\]test[/\\]mocks[/\\]')


def _should_skip(path: str) -> bool:
    parts = path.replace("\\", "/").split("/")
    if any(p in _SKIP_DIRS for p in parts):
        return True
    if _SKIP_FILE_PATTERNS.search(path):
        return True
    # Skip the mock definition files themselves (test/mocks/**).
    if _MOCK_DEF_PATH_RE.search(path):
        return True
    return False


def _iter_cc_files(roots: List[str]):
    """Yield absolute paths of all .cc and .h test files under *roots*."""
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root, topdown=True):
            # Prune skip directories in-place to avoid descending into them.
            dirnames[:] = [d for d in dirnames if d not in _SKIP_DIRS]
            for fname in filenames:
                if fname.endswith((".cc", ".h")):
                    full = os.path.join(dirpath, fname)
                    if not _should_skip(full):
                        yield full


# ---------------------------------------------------------------------------
# Main processing
# ---------------------------------------------------------------------------

def process_files(
    roots: List[str],
    rules_families: List[str],
    mode: str,
    verbose: bool,
) -> int:
    """Process all files under *roots*.

    Returns the number of files that need/had changes.
    """
    candidates = 0

    for cc_path in _iter_cc_files(roots):
        try:
            with open(cc_path, encoding="utf-8") as fh:
                content = fh.read()
        except (OSError, UnicodeDecodeError):
            continue

        file_changed = False

        for family_name in rules_families:
            rules = FAMILY_RULES.get(family_name)
            if rules is None:
                print(f"Unknown rules family: {family_name}", file=sys.stderr)
                continue

            result = _analyze_file(content, rules, verbose, cc_path)
            if result is None:
                continue

            old_includes, new_includes, old_deps, new_deps = result

            if mode == "check":
                # Verify the CC file actually needs include changes.
                cc_needs_change = old_includes != new_includes

                # Verify the BUILD file actually needs changes by reading it.
                build_needs_change = False
                chk_build = _find_build_file(cc_path)
                if chk_build:
                    try:
                        with open(chk_build, encoding="utf-8") as fh:
                            chk_build_content = fh.read()
                        chk_new_build = _rewrite_build_deps(
                            chk_build_content,
                            os.path.basename(cc_path),
                            old_deps,
                            new_deps,
                            verbose,
                        )
                        build_needs_change = (
                            chk_new_build is not None
                            and chk_new_build != chk_build_content
                        )
                    except (OSError, UnicodeDecodeError):
                        pass

                if not cc_needs_change and not build_needs_change:
                    continue

                print(f"{cc_path}")
                if verbose:
                    print(f"  remove includes: {sorted(old_includes)}")
                    print(f"  add    includes: {sorted(new_includes)}")
                    print(f"  remove deps:     {sorted(old_deps)}")
                    print(f"  add    deps:     {sorted(new_deps)}")
                candidates += 1
                file_changed = True
            else:
                # fix mode — rewrite includes if they changed.
                new_content = _rewrite_includes(content, old_includes, new_includes)
                if new_content != content:
                    if verbose:
                        print(f"  Fixing includes in {cc_path}")
                    with open(cc_path, "w", encoding="utf-8") as fh:
                        fh.write(new_content)
                    content = new_content
                    file_changed = True

                # Always attempt to update the BUILD file, even if the
                # includes didn't change (BUILD deps may still need narrowing).
                build_path = _find_build_file(cc_path)
                if build_path:
                    cc_filename = os.path.basename(cc_path)
                    try:
                        with open(build_path, encoding="utf-8") as fh:
                            build_content = fh.read()
                    except (OSError, UnicodeDecodeError):
                        continue
                    new_build = _rewrite_build_deps(
                        build_content, cc_filename, old_deps, new_deps, verbose
                    )
                    if new_build is not None and new_build != build_content:
                        if verbose:
                            print(f"  Fixing BUILD deps in {build_path}")
                        with open(build_path, "w", encoding="utf-8") as fh:
                            fh.write(new_build)
                        file_changed = True

        if file_changed and mode == "fix":
            candidates += 1

    return candidates


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main(argv=None):
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--mode",
        choices=["check", "fix"],
        default="check",
        help="'check' reports candidates (exit 1 if found); 'fix' applies rewrites.",
    )
    parser.add_argument(
        "--paths",
        nargs="+",
        default=["test", "contrib", "mobile/test"],
        help="Directories to scan (relative to repo root or absolute).",
    )
    parser.add_argument(
        "--rules",
        nargs="+",
        default=["server"],
        help="Rules families to apply (default: server).",
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Emit detailed per-file diagnostics.",
    )
    parser.add_argument(
        "--root",
        default=None,
        help="Repository root (default: directory of this script's parent).",
    )
    args = parser.parse_args(argv)

    # Resolve repository root.
    if args.root:
        repo_root = os.path.abspath(args.root)
    else:
        # tools/code/narrow_test_mocks.py → repo root is two levels up.
        repo_root = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..")
        )

    roots = [
        p if os.path.isabs(p) else os.path.join(repo_root, p)
        for p in args.paths
    ]

    n = process_files(roots, args.rules, args.mode, args.verbose)

    if args.mode == "check":
        if n:
            print(
                f"\n{n} file(s) have over-broad server-mock includes. "
                "Run with --mode fix to apply."
            )
            sys.exit(1)
        else:
            print("No over-broad server-mock includes found.")
            sys.exit(0)
    else:
        print(f"Fixed {n} file(s).")
        sys.exit(0)


if __name__ == "__main__":
    main()
