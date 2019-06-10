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

def aar_with_jni(name, android_library, archive_name = "", visibility = None):
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
        deps = [android_library],
    )

    native.genrule(
        name = name,
        srcs = [android_library + ".aar", archive_name + "_jni_unsigned.apk"],
        outs = [archive_name + ".aar"],
        cmd = """
cp $(location {}.aar) $(location :{}.aar)
chmod +w $(location :{}.aar)
origdir=$$PWD
cd $$(mktemp -d)
unzip $$origdir/$(location :{}_jni_unsigned.apk) "lib/*"
cp -r lib jni
zip -r $$origdir/$(location :{}.aar) jni/*/*.so
""".format(android_library, archive_name, archive_name, archive_name, archive_name),
        visibility = visibility,
    )
