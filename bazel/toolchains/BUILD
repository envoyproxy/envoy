licenses(["notice"])  # Apache 2

platform(
    name = "rbe_ubuntu_clang_platform",
    parents = ["@rbe_ubuntu_clang//config:platform"],
    remote_execution_properties = """
        {PARENT_REMOTE_EXECUTION_PROPERTIES}
        properties: {
          name: "dockerAddCapabilities"
          value: "SYS_PTRACE,NET_RAW,NET_ADMIN"
        }
        properties: {
          name: "dockerNetwork"
          value: "standard"
        }
        """,
)
