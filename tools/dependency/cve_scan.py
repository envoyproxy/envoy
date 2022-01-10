#!/usr/bin/env python3

# usage
#
# with bazel:
#
#  $ bazel run //tools/dependency:cve_scan -- -h
#
#  $ bazel run //tools/dependency:cve_scan
#
#
# The upstream lib is maintained here:
#
#    https://github.com/envoyproxy/pytooling/tree/main/envoy.dependency.cve_scan
#
# Please submit issues/PRs to the pytooling repo:
#
#    https://github.com/envoyproxy/pytooling
#

import sys
from functools import cached_property
from typing import Type

import abstracts

from envoy.dependency import cve_scan

import tools.dependency.utils as dep_utils


@abstracts.implementer(cve_scan.ACVE)
class EnvoyCVE:

    @property
    def cpe_class(self):
        return EnvoyCPE

    @property
    def version_matcher_class(self) -> Type[cve_scan.ACVEVersionMatcher]:
        return EnvoyCVEVersionMatcher


@abstracts.implementer(cve_scan.ACPE)
class EnvoyCPE:
    pass


@abstracts.implementer(cve_scan.ADependency)
class EnvoyDependency:
    pass


@abstracts.implementer(cve_scan.ACVEChecker)
class EnvoyCVEChecker:

    @property
    def cpe_class(self):
        return EnvoyCPE

    @property
    def cve_class(self):
        return EnvoyCVE

    @property
    def dependency_class(self):
        return EnvoyDependency

    @cached_property
    def dependency_metadata(self):
        return dep_utils.repository_locations()

    @cached_property
    def ignored_cves(self):
        return super().ignored_cves


@abstracts.implementer(cve_scan.ACVEVersionMatcher)
class EnvoyCVEVersionMatcher:
    pass


def main(*args) -> int:
    return EnvoyCVEChecker(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
