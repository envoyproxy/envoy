load("@com_github_chrusty_protoc_gen_jsonschema//:defs.bzl", "jsonschema_compile")

def generate_jsonschema(name, proto):
    jsonschema_compile(
        name = name,
        protos = [proto],
    )
