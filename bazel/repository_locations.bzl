# This should match the schema defined in external_deps.bzl.

PROTOBUF_VERSION = "33.2"

# These names of these deps *must* match the names used in `/bazel/protobuf.patch`,
# and both must match the names from the protobuf releases (see
# https://github.com/protocolbuffers/protobuf/releases).
# The names change in upcoming versions.
# The shas are calculated from the downloads on the releases page.
PROTOC_VERSIONS = dict(
    linux_aarch_64 = "706662a332683aa2fffe1c4ea61588279d31679cd42d91c7d60a69651768edb8",
    linux_x86_64 = "b24b53f87c151bfd48b112fe4c3a6e6574e5198874f38036aff41df3456b8caf",
    linux_ppcle_64 = "16b4a36c07daab458bc040523b1f333ddd37e1440fa71634f297a458c7fef4c4",
    osx_aarch_64 = "5be1427127788c9f7dd7d606c3e69843dd3587327dea993917ffcb77e7234b44",
    osx_x86_64 = "dba51cfcc85076d56e7de01a647865c5a7f995c3dce427d5215b53e50b7be43f",
    win64 = "376770cd4073beb63db56fdd339260edb9957b3c4472e05a75f5f9ec8f98d8f5",
)

REPOSITORY_LOCATIONS_SPEC = dict(
    bazel_compdb = dict(
        version = "40864791135333e1446a04553b63cbe744d358d0",
        sha256 = "acd2a9eaf49272bb1480c67d99b82662f005b596a8c11739046a4220ec73c4da",
        strip_prefix = "bazel-compilation-database-{version}",
        urls = ["https://github.com/grailbio/bazel-compilation-database/archive/{version}.tar.gz"],
    ),
    com_github_bazelbuild_buildtools = dict(
        version = "8.2.1",
        sha256 = "53119397bbce1cd7e4c590e117dcda343c2086199de62932106c80733526c261",
        strip_prefix = "buildtools-{version}",
        urls = ["https://github.com/bazelbuild/buildtools/archive/v{version}.tar.gz"],
    ),
    aws_lc = dict(
        project_name = "AWS libcrypto (AWS-LC)",
        project_desc = "OpenSSL compatible general-purpose crypto library",
        project_url = "https://github.com/aws/aws-lc",
        version = "1.65.1",
        sha256 = "d4cf3b19593fc7876b23741e8ca7c48e0043679cec393fe24b138c3f1ffd6254",
        strip_prefix = "aws-lc-{version}",
        urls = ["https://github.com/aws/aws-lc/archive/v{version}.tar.gz"],
        use_category = ["controlplane", "dataplane_core"],
        release_date = "2025-12-01",
        cpe = "cpe:2.3:a:google:boringssl:*",
    ),
    com_github_envoyproxy_sqlparser = dict(
        version = "3b40ba2d106587bdf053a292f7e3bb17e818a57f",
        sha256 = "96c10c8e950a141a32034f19b19cdeb1da48fe859cf96ae5e19f894f36c62c71",
        strip_prefix = "sql-parser-{version}",
        urls = ["https://github.com/envoyproxy/sql-parser/archive/{version}.tar.gz"],
    ),
    com_github_google_jwt_verify = dict(
        version = "b59e8075d4a4f975ba6f109e1916d6e60aeb5613",
        sha256 = "637e4983506c4f26bbe2808ae4e1944e46cbb2277d34ff0b8a3b72bdac3c4b91",
        strip_prefix = "jwt_verify_lib-{version}",
        urls = ["https://github.com/google/jwt_verify_lib/archive/{version}.tar.gz"],
    ),
    com_github_google_quiche = dict(
        version = "b7b4c0cfe393a57b8706b0f1be81518595daaa44",
        sha256 = "9d8344faf932165b6013f8fdd2cbfe2be7c2e7a5129c5e572036d13718a3f1bf",
        urls = ["https://github.com/google/quiche/archive/{version}.tar.gz"],
        strip_prefix = "quiche-{version}",
    ),
    kafka_source = dict(
        version = "3.9.1",
        sha256 = "c15b82940cfb9f67fce909d8600dc8bcfc42d2795da2c26c149d03a627f85234",
        strip_prefix = "kafka-{version}/clients/src/main/resources/common/message",
        urls = ["https://github.com/apache/kafka/archive/{version}.zip"],
    ),
    kafka_server_binary = dict(
        version = "3.9.1",
        sha256 = "dd4399816e678946cab76e3bd1686103555e69bc8f2ab8686cda71aa15bc31a3",
        strip_prefix = "kafka_2.13-{version}",
        urls = ["https://downloads.apache.org/kafka/{version}/kafka_2.13-{version}.tgz"],
    ),
    thrift = dict(
        version = "0.22.0",
        sha256 = "c4649c5879dd56c88f1e7a1c03e0fbfcc3b2a2872fb81616bffba5aa8a225a37",
        strip_prefix = "thrift-{version}/lib/py/",
        urls = ["https://github.com/apache/thrift/archive/refs/tags/v{version}.tar.gz"],
    ),
)

def _compiled_protoc_deps(locations, versions):
    for platform, sha in versions.items():
        locations["com_google_protobuf_protoc_%s" % platform] = dict(
            version = PROTOBUF_VERSION,
            sha256 = sha,
            urls = ["https://github.com/protocolbuffers/protobuf/releases/download/v{version}/protoc-{version}-%s.zip" % platform.replace("_", "-", 1)],
        )

_compiled_protoc_deps(REPOSITORY_LOCATIONS_SPEC, PROTOC_VERSIONS)
