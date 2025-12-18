"""Bazel macros for bssl-compat library."""

def patched_bssl_filegroup(name, srcs):
    """Copy BoringSSL files from @boringssl into bssl-compat, applying patches.

    Args:
        name: The name of the filegroup, containing the copied & patched files
        srcs: List of source files relative to @boringssl e.g. ssl/ssl_x509.cc
    """
    targets = []
    for file in srcs:
        target_name = name + "_" + file.replace("/", "_").replace(".", "_")
        targets.append(":" + target_name)
        src_file = "@boringssl//:" + file
        dst_file = file
        native.genrule(
            name = target_name,
            srcs = [src_file, "//patch:all"],
            outs = [dst_file],
            cmd = """
                # Set up paths - all paths need to be relative to bssl-compat package
                SRC_FILE="$(location {src_file})"
                DST_FILE="$(location {dst_file})"
                # Patch files are in the package, so use relative paths from execroot
                PATCH_SCRIPT="external/bssl-compat/patch/{dst_file}.sh"
                PATCH_FILE="external/bssl-compat/patch/{dst_file}.patch"

                # Create output directory
                mkdir -p "$$(dirname $$DST_FILE)"

                # Create temporary directory
                TMP_DIR="$$DST_FILE.tmp"
                mkdir -p "$$TMP_DIR"
                trap 'rm -rf $$TMP_DIR' EXIT

                # Copy source file to working file
                WORKING="$$TMP_DIR/working.h"
                cp "$$SRC_FILE" "$$WORKING"
                chmod +w "$$WORKING"

                # Apply patch file if it exists
                if [ -f "$$PATCH_FILE" ]; then
                    patch -s -f "$$WORKING" "$$PATCH_FILE" -o "$$TMP_DIR/applied.patch.h"
                    cp "$$TMP_DIR/applied.patch.h" "$$WORKING"
                fi

                # Apply patch script if it exists, otherwise comment out the whole file
                if [ -f "$$PATCH_SCRIPT" ]; then
                    TOOLS_DIR="$$(dirname "$(location //tools:uncomment.sh)")"
                    PATH="$$TOOLS_DIR:$$PATH" bash "$$PATCH_SCRIPT" "$$WORKING"
                    cp "$$WORKING" "$$TMP_DIR/applied.script.h"
                else
                    bash $(location //tools:uncomment.sh) "$$WORKING" --comment
                fi

                # Copy result to destination
                cp "$$WORKING" "$$DST_FILE"
            """.format(src_file = src_file, dst_file = dst_file),
            tools = ["//tools:uncomment.sh"],
        )
    # Create a filegroup containing the copied & patched files
    native.filegroup(
        name = name,
        srcs = targets,
        visibility = ["//visibility:private"],
    )

def mapping_func_filegroup(name, funcs):
    """Find or generate mapping functions for BoringSSL API.

    For each function name, this either:
    1. Uses an existing handwritten source/function.c or source/function.cc if it exists, OR
    2. Creates a genrule that searches BoringSSL headers for the function signature
       and generates a .c file with a forwarding function that calls ossl_<function>

    Args:
        name: The name of the filegroup, containing the mapping function source files
        funcs: List of function names (e.g., ["BIO_new", "BIO_free", "SSL_new"])
    """
    targets = []
    for func in funcs: # Check for handwritten implementation (.c or .cc)
        handwritten = native.glob(["source/" + func + ".c", "source/" + func + ".cc"])
        if handwritten:
            target_name = "handwritten_" + func
            targets.append(":" + target_name)
            native.filegroup(
                name = target_name,
                srcs = [handwritten[0]],
            )
        else: # Else generate an implementation using generate.c.sh
            target_name = "generated_" + func
            targets.append(":" + target_name)
            out = "source/" + func + ".c"
            native.genrule(
                name = target_name,
                srcs = [":patched_bssl_headers"],
                tools = ["//tools:generate.c.sh"],
                outs = [out],
                cmd = """
                    mkdir -p "$$(dirname $(location {out}))"

                    # Deduce the include directory from the first include file
                    FIRST_INCLUDE="$$(echo $(SRCS) | awk '{{print $$1}}')"
                    PATCHED_BSSL_INCLUDE_DIR="$$(dirname "$$(dirname "$$FIRST_INCLUDE")")"

                    # Run generate.c.sh with the patched BoringSSL include directory
                    $(location //tools:generate.c.sh) "{func}" "$(location {out})" "$$PATCHED_BSSL_INCLUDE_DIR"
                """.format(func = func, out = out),
            )
    # Create a filegroup containing all function implementations (both handwritten and generated)
    native.filegroup(
        name = name,
        srcs = targets,
    )
