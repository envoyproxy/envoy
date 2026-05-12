"""Unit tests for tools/code/narrow_test_mocks.py."""

import sys
import os
import tempfile
import unittest
import unittest.mock

# Make the tools/code package importable when running standalone.
_TOOLS_CODE_DIR = os.path.join(os.path.dirname(__file__), "..")
if _TOOLS_CODE_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_CODE_DIR)

from narrow_test_mocks import (  # noqa: E402
    ALLOW_FULL_REMOVAL,
    FAMILY_RULES,
    TRANSITIVE_GUARD,
    _analyze_file,
    _check_transitive_guard,
    _rewrite_includes,
    _rewrite_build_deps,
    _reduce_includes,
    _reduce_deps,
    process_files,
)

SERVER_RULES = FAMILY_RULES["server"]


class TestAnalyzeFile(unittest.TestCase):
    """Tests for _analyze_file() — the pure analysis step."""

    # ------------------------------------------------------------------
    # Case 1: Only MockServerFactoryContext used, but instance.h included
    # ------------------------------------------------------------------
    def test_only_sfc_but_includes_instance(self):
        content = """\
#include "test/mocks/server/instance.h"

void foo() {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> ctx;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNotNone(result)
        old_inc, new_inc, old_dep, new_dep = result
        self.assertIn("test/mocks/server/instance.h", old_inc)
        self.assertIn("test/mocks/server/server_factory_context.h", new_inc)
        self.assertNotIn("test/mocks/server/instance.h", new_inc)
        self.assertIn("//test/mocks/server:server_factory_context_mocks", new_dep)
        self.assertNotIn("//test/mocks/server:instance_mocks", new_dep)

    # ------------------------------------------------------------------
    # Case 2: Only MockFactoryContext used, but both instance.h and
    # factory_context.h included.  factory_context.h already #includes
    # instance.h, so the explicit instance.h (and instance_mocks dep) is
    # redundant and can be dropped.
    # ------------------------------------------------------------------
    def test_redundant_instance_when_factory_context_present(self):
        content = """\
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"

void foo() {
  testing::NiceMock<Server::Configuration::MockFactoryContext> ctx;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNotNone(result)
        old_inc, new_inc, old_dep, new_dep = result
        # Both broad headers appear in old_inc
        self.assertIn("test/mocks/server/instance.h", old_inc)
        self.assertIn("test/mocks/server/factory_context.h", old_inc)
        # Only factory_context.h is needed; instance.h is dominated by it
        self.assertIn("test/mocks/server/factory_context.h", new_inc)
        self.assertNotIn("test/mocks/server/instance.h", new_inc)
        self.assertIn("//test/mocks/server:factory_context_mocks", new_dep)
        self.assertNotIn("//test/mocks/server:instance_mocks", new_dep)

    # ------------------------------------------------------------------
    # Case 3: MockInstance IS used — nothing should change
    # ------------------------------------------------------------------
    def test_mock_instance_used_no_change(self):
        content = """\
#include "test/mocks/server/instance.h"

void foo() {
  testing::NiceMock<Server::MockInstance> inst;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNone(result)

    # ------------------------------------------------------------------
    # Case 4: mocks.h umbrella but only two symbols used — narrowed
    # ------------------------------------------------------------------
    def test_mocks_umbrella_narrowed(self):
        content = """\
#include "test/mocks/server/mocks.h"

void bar() {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> ctx;
  testing::NiceMock<Server::MockListenerManager> lm;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNotNone(result)
        old_inc, new_inc, old_dep, new_dep = result
        self.assertIn("test/mocks/server/mocks.h", old_inc)
        self.assertIn("//test/mocks/server:server_mocks", old_dep)
        # Should replace with the two narrow headers
        self.assertIn("test/mocks/server/server_factory_context.h", new_inc)
        self.assertIn("test/mocks/server/listener_manager.h", new_inc)
        # mocks.h itself should not appear in new_inc
        self.assertNotIn("test/mocks/server/mocks.h", new_inc)
        self.assertIn("//test/mocks/server:server_factory_context_mocks", new_dep)
        self.assertIn("//test/mocks/server:listener_manager_mocks", new_dep)
        self.assertNotIn("//test/mocks/server:server_mocks", new_dep)

    # ------------------------------------------------------------------
    # Case 5: factory_context.h but only MockServerFactoryContext used
    # ------------------------------------------------------------------
    def test_factory_context_only_sfc_used(self):
        content = """\
#include "test/mocks/server/factory_context.h"

void baz() {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> ctx;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNotNone(result)
        old_inc, new_inc, old_dep, new_dep = result
        self.assertIn("test/mocks/server/factory_context.h", old_inc)
        self.assertIn("test/mocks/server/server_factory_context.h", new_inc)
        self.assertNotIn("test/mocks/server/factory_context.h", new_inc)
        self.assertIn("//test/mocks/server:server_factory_context_mocks", new_dep)
        self.assertNotIn("//test/mocks/server:factory_context_mocks", new_dep)

    # ------------------------------------------------------------------
    # Case 6: Already using the narrowest header — no change
    # ------------------------------------------------------------------
    def test_already_narrow_no_change(self):
        content = """\
#include "test/mocks/server/server_factory_context.h"

void foo() {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> ctx;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNone(result)

    # ------------------------------------------------------------------
    # Case 7: using namespace — skip (conservative)
    # ------------------------------------------------------------------
    def test_using_namespace_skip(self):
        content = """\
#include "test/mocks/server/instance.h"
using namespace Envoy::Server;

void foo() {
  NiceMock<MockServerFactoryContext> ctx;
}
"""
        result = _analyze_file(content, SERVER_RULES, verbose=False)
        self.assertIsNone(result)

    # ------------------------------------------------------------------
    # Case 8: factory_context.h included, MockFactoryContext IS used
    # ------------------------------------------------------------------
    def test_factory_context_used_no_change(self):
        content = """\
#include "test/mocks/server/factory_context.h"

void foo() {
  testing::NiceMock<Server::Configuration::MockFactoryContext> ctx;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNone(result)

    # ------------------------------------------------------------------
    # Case 9: mocks.h + zero server-mock symbols → skip (ALLOW_FULL_REMOVAL=False)
    # ------------------------------------------------------------------
    def test_mocks_umbrella_no_server_symbols_skip_when_no_full_removal(self):
        content = """\
#include "test/mocks/server/mocks.h"

// Uses only non-server mock symbols
void foo() {
  testing::NiceMock<Network::MockConnection> conn;
}
"""
        # With ALLOW_FULL_REMOVAL=False, _analyze_file must skip the file
        # entirely to avoid removing a dep that may be needed for its
        # transitive includes (e.g. Singleton::ManagerImpl, createApiForTest).
        import narrow_test_mocks as _m
        with unittest.mock.patch.object(_m, "ALLOW_FULL_REMOVAL", False):
            result = _analyze_file(content, SERVER_RULES)
        self.assertIsNone(result)

    def test_mocks_umbrella_no_server_symbols_removed_when_full_removal_allowed(self):
        # Use content that references only non-server symbols that are NOT in
        # the TRANSITIVE_GUARD (so the guard doesn't fire), to isolate the
        # ALLOW_FULL_REMOVAL behaviour.  We use a bare Stats::IsolatedStoreImpl
        # identifier — it is not a Mock* or Test* type and therefore not in any
        # TRANSITIVE_GUARD entry.
        content = """\
#include "test/mocks/server/mocks.h"

// Uses a non-Mock, non-Test symbol not covered by the transitive guard
void foo() {
  Stats::IsolatedStoreImpl store;
}
"""
        # With ALLOW_FULL_REMOVAL=True the include/dep is removed entirely
        # when no server symbols are referenced AND the transitive guard passes.
        import narrow_test_mocks as _m
        with unittest.mock.patch.object(_m, "ALLOW_FULL_REMOVAL", True):
            result = _analyze_file(content, SERVER_RULES)
        self.assertIsNotNone(result)
        old_inc, new_inc, old_dep, new_dep = result
        self.assertIn("test/mocks/server/mocks.h", old_inc)
        self.assertNotIn("test/mocks/server/mocks.h", new_inc)
        self.assertEqual(new_inc, set())

    def test_mocks_umbrella_no_server_symbols_guard_fires_even_with_full_removal(self):
        """The transitive guard takes precedence even when ALLOW_FULL_REMOVAL=True.

        If the file uses Network::MockConnection without a direct
        test/mocks/network/mocks.h, narrowing is still skipped — the guard is
        stronger than the full-removal flag.
        """
        content = """\
#include "test/mocks/server/mocks.h"

void foo() {
  testing::NiceMock<Network::MockConnection> conn;
}
"""
        import narrow_test_mocks as _m
        with unittest.mock.patch.object(_m, "ALLOW_FULL_REMOVAL", True):
            result = _analyze_file(content, SERVER_RULES)
        # Guard fires: Network::MockConnection lacks test/mocks/network/mocks.h
        self.assertIsNone(result)


class TestReduceIncludes(unittest.TestCase):
    """Tests for the dominance-reduction helpers."""

    def test_factory_context_dominates_server_factory_context(self):
        inc_dom = SERVER_RULES["include_dominates"]
        headers = {
            "test/mocks/server/factory_context.h",
            "test/mocks/server/server_factory_context.h",
        }
        reduced = _reduce_includes(headers, inc_dom)
        # server_factory_context.h is dominated by factory_context.h
        self.assertIn("test/mocks/server/factory_context.h", reduced)
        self.assertNotIn("test/mocks/server/server_factory_context.h", reduced)

    def test_instance_dominates_server_factory_context(self):
        inc_dom = SERVER_RULES["include_dominates"]
        headers = {
            "test/mocks/server/instance.h",
            "test/mocks/server/server_factory_context.h",
        }
        reduced = _reduce_includes(headers, inc_dom)
        self.assertIn("test/mocks/server/instance.h", reduced)
        self.assertNotIn("test/mocks/server/server_factory_context.h", reduced)

    def test_no_dominance_relation_both_kept(self):
        inc_dom = SERVER_RULES["include_dominates"]
        headers = {
            "test/mocks/server/listener_factory_context.h",
            "test/mocks/server/factory_context.h",
        }
        # listener_factory_context.h does NOT include factory_context.h,
        # so both should be kept.
        reduced = _reduce_includes(headers, inc_dom)
        self.assertIn("test/mocks/server/listener_factory_context.h", reduced)
        self.assertIn("test/mocks/server/factory_context.h", reduced)


class TestReduceDeps(unittest.TestCase):
    def test_factory_context_mocks_dominates_instance_mocks(self):
        dep_dom = SERVER_RULES["dep_dominates"]
        deps = {
            "//test/mocks/server:factory_context_mocks",
            "//test/mocks/server:instance_mocks",
        }
        reduced = _reduce_deps(deps, dep_dom)
        self.assertIn("//test/mocks/server:factory_context_mocks", reduced)
        self.assertNotIn("//test/mocks/server:instance_mocks", reduced)

    def test_server_mocks_dominates_all(self):
        dep_dom = SERVER_RULES["dep_dominates"]
        deps = {
            "//test/mocks/server:server_mocks",
            "//test/mocks/server:factory_context_mocks",
            "//test/mocks/server:admin_mocks",
        }
        reduced = _reduce_deps(deps, dep_dom)
        self.assertEqual(reduced, {"//test/mocks/server:server_mocks"})


class TestRewriteIncludes(unittest.TestCase):
    """Tests for the include-rewriting step."""

    def test_replace_instance_with_sfc(self):
        content = """\
#include "source/foo.h"
#include "test/mocks/server/instance.h"
#include "test/something/else.h"

void foo() {}
"""
        old = {"test/mocks/server/instance.h"}
        new = {"test/mocks/server/server_factory_context.h"}
        result = _rewrite_includes(content, old, new)
        self.assertIn('#include "test/mocks/server/server_factory_context.h"', result)
        self.assertNotIn('#include "test/mocks/server/instance.h"', result)
        self.assertIn('#include "source/foo.h"', result)
        self.assertIn('#include "test/something/else.h"', result)

    def test_no_duplicate_include_added(self):
        """If the narrow header is already present, don't add it again."""
        content = """\
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/server/instance.h"

void foo() {}
"""
        old = {"test/mocks/server/instance.h"}
        new = {"test/mocks/server/server_factory_context.h"}
        result = _rewrite_includes(content, old, new)
        # server_factory_context.h should appear exactly once.
        self.assertEqual(
            result.count('#include "test/mocks/server/server_factory_context.h"'), 1
        )
        self.assertNotIn('#include "test/mocks/server/instance.h"', result)

    def test_drop_mocks_h_entirely_when_no_symbols(self):
        content = """\
#include "test/mocks/server/mocks.h"
#include "test/mocks/network/mocks.h"

void foo() {}
"""
        old = {"test/mocks/server/mocks.h"}
        new: set = set()
        result = _rewrite_includes(content, old, new)
        self.assertNotIn('#include "test/mocks/server/mocks.h"', result)
        self.assertIn('#include "test/mocks/network/mocks.h"', result)


class TestRewriteBuildDeps(unittest.TestCase):
    """Tests for the BUILD-file rewriting step."""

    def _build(self, srcs, deps):
        """Helper: produce a minimal envoy_extension_cc_test stanza."""
        srcs_str = ", ".join(f'"{s}"' for s in srcs)
        deps_str = "\n".join(f'        "{d}",' for d in sorted(deps))
        return f"""\
envoy_extension_cc_test(
    name = "my_test",
    srcs = [{srcs_str}],
    deps = [
{deps_str}
    ],
)
"""

    def test_replace_instance_with_sfc(self):
        build = self._build(
            ["my_test.cc"],
            [
                "//test/mocks/server:instance_mocks",
                "//source/common/foo:bar",
            ],
        )
        old = {"//test/mocks/server:instance_mocks"}
        new = {"//test/mocks/server:server_factory_context_mocks"}
        result = _rewrite_build_deps(build, "my_test.cc", old, new)
        self.assertIsNotNone(result)
        self.assertIn("server_factory_context_mocks", result)
        self.assertNotIn("instance_mocks", result)
        self.assertIn("//source/common/foo:bar", result)

    def test_replace_server_mocks_with_two_deps(self):
        build = self._build(
            ["my_test.cc"],
            ["//test/mocks/server:server_mocks"],
        )
        old = {"//test/mocks/server:server_mocks"}
        new = {
            "//test/mocks/server:server_factory_context_mocks",
            "//test/mocks/server:listener_manager_mocks",
        }
        result = _rewrite_build_deps(build, "my_test.cc", old, new)
        self.assertIsNotNone(result)
        self.assertNotIn("server_mocks", result)
        self.assertIn("server_factory_context_mocks", result)
        self.assertIn("listener_manager_mocks", result)

    def test_sorted_alphabetically(self):
        build = self._build(
            ["my_test.cc"],
            ["//test/mocks/server:server_mocks"],
        )
        new = {
            "//test/mocks/server:server_factory_context_mocks",
            "//test/mocks/server:listener_manager_mocks",
            "//test/mocks/server:admin_mocks",
        }
        result = _rewrite_build_deps(build, "my_test.cc", {"//test/mocks/server:server_mocks"}, new)
        self.assertIsNotNone(result)
        pos_admin = result.index("admin_mocks")
        pos_listener = result.index("listener_manager_mocks")
        pos_sfc = result.index("server_factory_context_mocks")
        self.assertLess(pos_admin, pos_listener)
        self.assertLess(pos_listener, pos_sfc)

    def test_no_change_when_dep_already_correct(self):
        build = self._build(
            ["my_test.cc"],
            ["//test/mocks/server:server_factory_context_mocks"],
        )
        result = _rewrite_build_deps(
            build, "my_test.cc",
            {"//test/mocks/server:server_factory_context_mocks"},
            {"//test/mocks/server:server_factory_context_mocks"},
        )
        self.assertIsNone(result)

    def test_deduplication(self):
        """If new_deps is already a subset of existing, no redundant entries."""
        build = self._build(
            ["my_test.cc"],
            [
                "//test/mocks/server:instance_mocks",
                "//test/mocks/server:server_factory_context_mocks",
            ],
        )
        old = {"//test/mocks/server:instance_mocks"}
        new = {"//test/mocks/server:server_factory_context_mocks"}
        result = _rewrite_build_deps(build, "my_test.cc", old, new)
        self.assertIsNotNone(result)
        # server_factory_context_mocks should appear exactly once
        self.assertEqual(result.count("server_factory_context_mocks"), 1)


class TestEndToEnd(unittest.TestCase):
    """Integration tests using real temp files."""

    def _run_fix(self, cc_content, build_content, cc_name="test_file.cc"):
        with tempfile.TemporaryDirectory() as tmpdir:
            cc_path = os.path.join(tmpdir, cc_name)
            build_path = os.path.join(tmpdir, "BUILD")
            with open(cc_path, "w") as f:
                f.write(cc_content)
            with open(build_path, "w") as f:
                f.write(build_content)

            process_files(
                roots=[tmpdir],
                rules_families=["server"],
                mode="fix",
                verbose=False,
            )

            new_cc = open(cc_path).read()
            new_build = open(build_path).read()
        return new_cc, new_build

    def test_fix_instance_to_sfc(self):
        cc = """\
#include "test/mocks/server/instance.h"

void foo() {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> ctx;
}
"""
        build = """\
envoy_extension_cc_test(
    name = "test_file",
    srcs = ["test_file.cc"],
    deps = [
        "//test/mocks/server:instance_mocks",
    ],
)
"""
        new_cc, new_build = self._run_fix(cc, build)
        self.assertIn("server_factory_context.h", new_cc)
        self.assertNotIn("instance.h", new_cc)
        self.assertIn("server_factory_context_mocks", new_build)
        self.assertNotIn("instance_mocks", new_build)

    def test_fix_factory_context_to_sfc(self):
        cc = """\
#include "test/mocks/server/factory_context.h"

void baz() {
  testing::NiceMock<Server::Configuration::MockServerFactoryContext> ctx;
}
"""
        build = """\
envoy_extension_cc_test(
    name = "test_file",
    srcs = ["test_file.cc"],
    deps = [
        "//test/mocks/server:factory_context_mocks",
        "//source/other:lib",
    ],
)
"""
        new_cc, new_build = self._run_fix(cc, build)
        self.assertIn("server_factory_context.h", new_cc)
        # factory_context.h should be gone — check for the full include path
        self.assertNotIn('"test/mocks/server/factory_context.h"', new_cc)
        self.assertIn("server_factory_context_mocks", new_build)
        # factory_context_mocks should be gone — check for the exact dep
        self.assertNotIn('"//test/mocks/server:factory_context_mocks"', new_build)
        self.assertIn("//source/other:lib", new_build)


class TestTransitiveGuard(unittest.TestCase):
    """Tests for _check_transitive_guard() and its integration in _analyze_file()."""

    # ------------------------------------------------------------------
    # Direct unit tests for _check_transitive_guard()
    # ------------------------------------------------------------------

    def test_safe_when_no_guarded_symbols(self):
        """A file with no non-server symbols passes the guard."""
        content = """\
#include "test/mocks/server/admin.h"

void foo() {
  testing::NiceMock<Server::MockAdmin> admin;
}
"""
        includes = {"test/mocks/server/admin.h"}
        self.assertTrue(_check_transitive_guard(content, includes))

    def test_safe_when_guarded_symbol_has_direct_include(self):
        """Http::MockStreamDecoderFilterCallbacks is safe when its include is present."""
        content = """\
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/admin.h"

void foo() {
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> cbs;
  testing::NiceMock<Server::MockAdmin> admin;
}
"""
        includes = {"test/mocks/http/mocks.h", "test/mocks/server/admin.h"}
        self.assertTrue(_check_transitive_guard(content, includes))

    def test_skip_when_http_mock_lacks_direct_include(self):
        """Http::MockStreamDecoderFilterCallbacks without test/mocks/http/mocks.h → skip."""
        content = """\
#include "test/mocks/server/admin.h"

void foo() {
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> cbs;
  testing::NiceMock<Server::MockAdmin> admin;
}
"""
        includes = {"test/mocks/server/admin.h"}
        self.assertFalse(_check_transitive_guard(content, includes))

    def test_skip_when_test_header_map_lacks_direct_include(self):
        """Http::TestRequestHeaderMapImpl without utility.h → skip."""
        content = """\
#include "test/mocks/server/instance.h"

void foo() {
  Http::TestRequestHeaderMapImpl hdrs;
}
"""
        includes = {"test/mocks/server/instance.h"}
        self.assertFalse(_check_transitive_guard(content, includes))

    def test_safe_when_test_header_map_has_utility_include(self):
        """Http::TestRequestHeaderMapImpl with utility.h already present → safe."""
        content = """\
#include "test/mocks/server/admin.h"
#include "test/test_common/utility.h"

void foo() {
  Http::TestRequestHeaderMapImpl hdrs;
}
"""
        includes = {"test/mocks/server/admin.h", "test/test_common/utility.h"}
        self.assertTrue(_check_transitive_guard(content, includes))

    def test_skip_when_singleton_manager_impl_lacks_direct_include(self):
        """Singleton::ManagerImpl without its own header → skip."""
        content = """\
#include "test/mocks/server/instance.h"

void foo() {
  Singleton::ManagerImpl mgr;
}
"""
        includes = {"test/mocks/server/instance.h"}
        self.assertFalse(_check_transitive_guard(content, includes))

    def test_verbose_output_on_skip(self):
        """Verbose mode emits a diagnostic message when skipping."""
        content = """\
#include "test/mocks/server/admin.h"

void foo() {
  Http::MockStreamDecoderFilterCallbacks cbs;
}
"""
        includes = {"test/mocks/server/admin.h"}
        import io
        from contextlib import redirect_stdout
        buf = io.StringIO()
        with redirect_stdout(buf):
            result = _check_transitive_guard(
                content, includes, verbose=True, filepath="some/test.cc"
            )
        self.assertFalse(result)
        self.assertIn("some/test.cc", buf.getvalue())
        self.assertIn("test/mocks/http/mocks.h", buf.getvalue())

    # ------------------------------------------------------------------
    # Integration: _analyze_file() skips when guard fires
    # ------------------------------------------------------------------

    def test_analyze_file_skips_when_http_mock_lacks_include(self):
        """admin_filter_test.cc scenario: MockAdmin used (narrowed to admin.h)
        but Http::MockStreamDecoderFilterCallbacks lacks a direct include.
        _analyze_file must return None (skip).
        """
        content = """\
#include "test/mocks/server/instance.h"

void foo() {
  testing::NiceMock<Server::MockAdmin> admin;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> cbs;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        # The guard fires: Http::MockStreamDecoderFilterCallbacks is used but
        # test/mocks/http/mocks.h is not directly included → skip.
        self.assertIsNone(result)

    def test_analyze_file_narrows_when_http_mock_has_direct_include(self):
        """Same as above but with test/mocks/http/mocks.h already present.
        The narrowing should proceed.
        """
        content = """\
#include "test/mocks/http/mocks.h"
#include "test/mocks/server/instance.h"

void foo() {
  testing::NiceMock<Server::MockAdmin> admin;
  testing::NiceMock<Http::MockStreamDecoderFilterCallbacks> cbs;
}
"""
        result = _analyze_file(content, SERVER_RULES)
        # Guard is satisfied (http/mocks.h is directly included) → narrowing proceeds.
        self.assertIsNotNone(result)
        old_inc, new_inc, old_dep, new_dep = result
        self.assertIn("test/mocks/server/instance.h", old_inc)
        # Replaced with admin.h (narrowest header for MockAdmin)
        self.assertIn("test/mocks/server/admin.h", new_inc)

    def test_analyze_file_skips_test_env_without_utility(self):
        """TestEnvironment:: used but test/test_common/utility.h missing → skip."""
        content = """\
#include "test/mocks/server/instance.h"

void foo() {
  testing::NiceMock<Server::MockAdmin> admin;
  auto versions = TestEnvironment::getIpVersionsForTest();
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNone(result)

    def test_analyze_file_narrows_when_test_env_has_utility(self):
        """TestEnvironment:: used AND utility.h directly included → narrowing proceeds."""
        content = """\
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

void foo() {
  testing::NiceMock<Server::MockAdmin> admin;
  auto versions = TestEnvironment::getIpVersionsForTest();
}
"""
        result = _analyze_file(content, SERVER_RULES)
        self.assertIsNotNone(result)
        _, new_inc, _, _ = result
        self.assertIn("test/mocks/server/admin.h", new_inc)


if __name__ == "__main__":
    unittest.main()
