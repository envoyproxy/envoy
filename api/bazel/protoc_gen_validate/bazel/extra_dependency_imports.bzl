load("@bazel_features//:deps.bzl", "bazel_features_deps")
load("@com_google_protobuf//:protobuf_deps.bzl", "PROTOBUF_MAVEN_ARTIFACTS")
load("@pgv_pip_deps//:requirements.bzl", "install_deps")
load("@rules_jvm_external//:defs.bzl", "maven_install")
load("//:dependencies.bzl", "go_third_party")

def pgv_extra_dependency_imports():
    bazel_features_deps()

    install_deps()

    # gazelle:repository_macro dependencies.bzl%go_third_party
    go_third_party()

    maven_install(
        artifacts = PROTOBUF_MAVEN_ARTIFACTS,
        maven_install_json = "@com_google_protobuf//:maven_install.json",
        repositories = [
            "https://repo1.maven.org/maven2",
            "https://repo.maven.apache.org/maven2",
        ],
    )
