load("@google_bazel_common//tools/maven:pom_file.bzl", "pom_file")

# This file is based on https://github.com/aj-michael/aar_with_jni which is
# subject to the following copyright and license:

# MIT License

# Copyright (c) 2019 Adam Michael

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

# android_library's implicit aar doesn't flatten its transitive
# dependencies. When using the kotlin rules, the kt_android_library rule
# creates a few underlying libraries, because of this the classes.jar in
# the aar we built was empty. This rule separately builds the underlying
# kt.jar file, and replaces the aar's classes.jar with the kotlin jar
def aar_with_jni(name, android_library, proguard_rules = "", archive_name = "", visibility = None, native_deps = []):
    if not archive_name:
        archive_name = name

    native.genrule(
        name = archive_name + "_binary_manifest_generator",
        outs = [archive_name + "_generated_AndroidManifest.xml"],
        cmd = """
cat > $(OUTS) <<EOF
<manifest
  xmlns:android="http://schemas.android.com/apk/res/android"
  package="does.not.matter">
  <uses-sdk android:minSdkVersion="999"/>
</manifest>
EOF
""",
    )

    native.android_binary(
        name = archive_name + "_jni",
        manifest = archive_name + "_generated_AndroidManifest.xml",
        custom_package = "does.not.matter",
        deps = [android_library] + native_deps,
    )

    pom_file(
        name = name + "_pom",
        targets = [android_library],
        template_file = "//bazel:pom_template.xml",
        visibility = ["//visibility:public"],
    )

    native.genrule(
        name = name,
        srcs = [android_library + "_kt.jar", android_library + ".aar", archive_name + "_jni_unsigned.apk", name + "_pom.xml", proguard_rules],
        outs = [archive_name + ".aar"],
        visibility = visibility,
        cmd = """
cp $(location {proguard_rules}) ./proguard.txt
cp $(location {android_library}.aar) $(location :{archive_name}.aar)
chmod +w $(location :{archive_name}.aar)
origdir=$$PWD
cd $$(mktemp -d)
unzip $$origdir/$(location :{archive_name}_jni_unsigned.apk) "lib/*"
cp -r lib jni
cp $$origdir/$(location {android_library}_kt.jar) classes.jar
cp $$origdir/proguard.txt .
zip -r $$origdir/$(location :{archive_name}.aar) jni/*/*.so classes.jar proguard.txt
""".format(android_library = android_library, archive_name = archive_name, proguard_rules = proguard_rules),
    )
