import sys
from functools import cached_property
from typing import Optional, Tuple

import abstracts

from envoy.docs import abstract

# We have to do some evil things to sys.path due to the way that Python module
# resolution works; we have both tools/ trees in bazel_tools and envoy. By
# default, Bazel leaves us with a sys.path in which the @bazel_tools repository
# takes precedence. Now that we're done with importing runfiles above, we can
# just remove it from the sys.path.
sys.path = [p for p in sys.path if not p.endswith('bazel_tools')]

# repo utils
from tools.docs.utils import rst_formatter

# data imports
from envoy_repo.api import v3_proto_rst
from envoy_repo.bazel import all_repository_locations
from envoy_repo.docs import empty_extensions
from envoy_repo.source.extensions import all_extensions_metadata, security_postures


@abstracts.implementer(abstract.ADocsBuilder)
class EnvoyDocsBuilder:

    @property
    def rst_formatter(self) -> abstract.ARSTFormatter:
        return rst_formatter


@abstracts.implementer(abstract.AAPIDocsBuilder)
class EnvoyAPIDocsBuilder(EnvoyDocsBuilder):

    @cached_property
    def empty_extensions(self) -> abstract.EmptyExtensionsDict:
        return {
            details["docs_path"]: self.render_empty_extension(extension, details)
            for extension, details in empty_extensions.data.items()
        }

    @cached_property
    def v3_proto_rst(self) -> Tuple[str, ...]:
        return v3_proto_rst.data


@abstracts.implementer(abstract.AExtensionsDocsBuilder)
class EnvoyExtensionsDocsBuilder(EnvoyDocsBuilder):

    @cached_property
    def extensions_metadata(self) -> abstract.ExtensionsMetadataDict:
        return all_extensions_metadata.data.copy()

    @cached_property
    def security_postures(self) -> abstract.ExtensionSecurityPosturesDict:
        return security_postures.data.copy()


@abstracts.implementer(abstract.ADependenciesDocsBuilder)
class EnvoyDependenciesDocsBuilder(EnvoyDocsBuilder):

    @cached_property
    def repository_locations(self) -> abstract.RepositoryLocationsDict:
        return all_repository_locations.data.copy()


@abstracts.implementer(abstract.ADocsBuildingRunner)
class EnvoyDocsBuildingRunner:

    @cached_property
    def builders(self) -> abstract.BuildersDict:
        return super().builders

    async def run(self) -> Optional[int]:
        return await super().run()


def main(*args) -> int:
    EnvoyDocsBuildingRunner.register_builder("api", EnvoyAPIDocsBuilder)
    EnvoyDocsBuildingRunner.register_builder("dependencies", EnvoyDependenciesDocsBuilder)
    EnvoyDocsBuildingRunner.register_builder("extensions", EnvoyExtensionsDocsBuilder)
    return EnvoyDocsBuildingRunner(*args)()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
