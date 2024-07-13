load("@rules_java//java:defs.bzl", "java_common")

def _sources_javadocs_impl(ctx):
    javabase = ctx.attr._javabase[java_common.JavaRuntimeInfo]
    plugins_classpath = ";".join([
        ctx.file._dokka_base_jar.path,
        ctx.file._dokka_analysis_jar.path,
        ctx.file._dokka_kotlin_as_java_jar.path,
        ctx.file._dokka_analysis_intellij_jar.path,
        ctx.file._dokka_analysis_compiler_jar.path,
        ctx.file._korte_jvm_jar.path,
        ctx.file._dokka_javadoc_jar.path,
    ])
    output_jar = ctx.actions.declare_file("{}.jar".format(ctx.attr.name))

    ctx.actions.run_shell(
        command = """
        set -euo pipefail

        java=$1
        dokka_cli_jar=$2
        plugin_classpath=$3
        sources_jar=$4
        output_jar=$5

        sources_dir=$(mktemp -d)
        tmp_dir=$(mktemp -d)
        trap 'rm -rf "$sources_dir" "$tmp_dir"' EXIT

        unzip $sources_jar -d $sources_dir > /dev/null

        $java \
            --add-opens java.base/java.util=ALL-UNNAMED \
            -jar $dokka_cli_jar \
            -pluginsClasspath $plugin_classpath \
            -moduleName "Envoy Mobile" \
            -sourceSet "-src $sources_dir -noStdlibLink -noJdkLink" \
            -outputDir $tmp_dir > /dev/null

        original_directory=$PWD
        cd $tmp_dir
        zip -r $original_directory/$output_jar . > /dev/null
        """,
        arguments = [
            javabase.java_executable_exec_path,
            ctx.file._dokka_cli_jar.path,
            plugins_classpath,
            ctx.file.sources_jar.path,
            output_jar.path,
        ],
        inputs = [
            ctx.file._dokka_cli_jar,
            ctx.file._dokka_base_jar,
            ctx.file._dokka_analysis_jar,
            ctx.file._dokka_kotlin_as_java_jar,
            ctx.file._dokka_analysis_intellij_jar,
            ctx.file._dokka_analysis_compiler_jar,
            ctx.file._korte_jvm_jar,
            ctx.file._dokka_javadoc_jar,
            ctx.file.sources_jar,
        ] + ctx.files._javabase,
        outputs = [output_jar],
        mnemonic = "EnvoyMobileDokka",
        progress_message = "Generating javadocs...",
    )

    return [
        DefaultInfo(files = depset([output_jar])),
    ]

sources_javadocs = rule(
    implementation = _sources_javadocs_impl,
    attrs = {
        "sources_jar": attr.label(allow_single_file = True),
        # Dokka CLI
        "_dokka_cli_jar": attr.label(
            default = "@maven//:org_jetbrains_dokka_dokka_cli",
            allow_single_file = True,
        ),
        # Dokka Javadoc plugin and dependencies
        "_dokka_analysis_jar": attr.label(
            default = "@maven//:org_jetbrains_dokka_dokka_analysis",
            allow_single_file = True,
        ),
        "_dokka_analysis_compiler_jar": attr.label(
            default = "@maven//:org_jetbrains_dokka_kotlin_analysis_compiler",
            allow_single_file = True,
        ),
        "_dokka_analysis_intellij_jar": attr.label(
            default = "@maven//:org_jetbrains_dokka_kotlin_analysis_intellij",
            allow_single_file = True,
        ),
        "_dokka_base_jar": attr.label(
            default = "@maven//:org_jetbrains_dokka_dokka_base",
            allow_single_file = True,
        ),
        "_dokka_javadoc_jar": attr.label(
            default = "@maven//:org_jetbrains_dokka_javadoc_plugin",
            allow_single_file = True,
        ),
        "_dokka_kotlin_as_java_jar": attr.label(
            default = "@maven//:org_jetbrains_dokka_kotlin_as_java_plugin",
            allow_single_file = True,
        ),
        "_korte_jvm_jar": attr.label(
            default = "@maven//:com_soywiz_korlibs_korte_korte_jvm",
            allow_single_file = True,
        ),
        # Java Runtime
        "_javabase": attr.label(
            default = Label("@bazel_tools//tools/jdk:current_java_runtime"),
            allow_files = True,
            providers = [java_common.JavaRuntimeInfo],
        ),
    },
)
