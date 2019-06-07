# Based on https://github.com/aj-michael/aar_with_jni

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
