load("@rules_python//python:defs.bzl", "py_binary")
load(":defs.bzl", "hunspell_aff", "hunspell_bin", "hunspell_dic", "hunspell_runtime")

licenses(["notice"])  # Apache 2

# The hermetic hunspell toolchain (envoy_toolshed), so the spell check needs nothing from the host.
hunspell_runtime(name = "hunspell_runtime")

hunspell_bin(name = "hunspell_bin")

hunspell_dic(name = "hunspell_dic")

hunspell_aff(name = "hunspell_aff")

# SCOWL's affix file does not list the apostrophe in WORDCHARS, so hunspell would check "doesn't"
# as "doesn" and "t". Patch the resolved toolchain's dictionary (prebuilt or source-built) until
# envoy_toolshed ships one with it, then use `:hunspell_aff`/`:hunspell_dic` directly.
genrule(
    name = "en_US_dict",
    srcs = [
        ":hunspell_aff",
        ":hunspell_dic",
    ],
    outs = [
        "en_US.aff",
        "en_US.dic",
    ],
    cmd = """
    sed "s/^WORDCHARS 0123456789$$/WORDCHARS 0123456789'/" $(execpath :hunspell_aff) > $(execpath en_US.aff) \
    && cp $(execpath :hunspell_dic) $(execpath en_US.dic)
    """,
)

py_binary(
    name = "check_spelling_pedantic",
    srcs = ["check_spelling_pedantic.py"],
    data = [
        ":en_US.aff",
        ":en_US.dic",
        ":hunspell_bin",
        ":hunspell_runtime",
        ":spelling_dictionary.txt",
    ],
    env = {
        "HUNSPELL": "$(rootpath :hunspell_bin)",
        "HUNSPELL_DICT": "$(rootpath :en_US.dic)",
        "SPELLING_DICTIONARY": "$(rootpath :spelling_dictionary.txt)",
    },
)
