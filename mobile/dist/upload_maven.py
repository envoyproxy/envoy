#!/usr/bin/env python

from __future__ import print_function

import argparse
import base64
import os
import shutil
import time

try:
    from urllib.request import urlopen, Request, HTTPError
except ImportError:  # python 2
    from urllib2 import urlopen, Request, HTTPError

_USER_CREDS = os.environ.get("READWRITE_USER", "")
_KEY_CREDS = os.environ.get("READWRITE_API_KEY", "")
_ARTIFACT_HOST_URL = os.environ["ARTIFACT_HOST_URL"]

_GROUP_ID = "io.envoyproxy.envoymobile"
_ARTIFACT_ID = "envoy"
_BASE_URL = "{}/io/envoyproxy/envoymobile".format(_ARTIFACT_HOST_URL)
_LOCAL_INSTALL_PATH = os.path.expanduser("~/.m2/repository/io/envoyproxy/envoymobile/{}".format(_ARTIFACT_ID))


def _upload(target, version):
    aar = target + ".aar"
    source_jar = target + "-sources.jar"  # TODO: figure out how to generate sources jar within bazel
    pom_file = target + "-pom.xml"

    files = [
        (aar, "{}-{}.aar".format(_ARTIFACT_ID, version)),
        (source_jar, "{}-{}-sources.jar".format(_ARTIFACT_ID, version)),
        (pom_file, "{}-{}.pom".format(_ARTIFACT_ID, version))
    ]

    if version == "LOCAL-SNAPSHOT":
        _install_locally(version, files)
    else:
        _upload_to_artifactory(version, files)


def _upload_to_artifactory(version, files):
    aar = files[0][0]
    artifact_name = os.path.splitext(os.path.basename(aar))[0]

    package_url = os.path.join(_BASE_URL, artifact_name, version)
    base64string = base64.b64encode("{}:{}".format(_USER_CREDS,
                                                   _KEY_CREDS)
                                    .encode()).decode()

    for src_path, target_path in files:
        # Temporarily allow missing source jars
        if not os.path.exists(src_path):
            continue
        basename = os.path.basename(target_path)
        artifact_url = os.path.join(package_url, basename)
        try:
            with open(src_path, "rb") as f:
                request = Request(artifact_url, f.read())

            request.add_header("Authorization", "Basic {}".format(base64string))
            request.get_method = lambda: "PUT"
            urlopen(request)
        except HTTPError as e:
            if e.code == 403:
                print("Ignoring duplicate upload for {}".format(artifact_url))
            else:
                print("Exception raised for {}".format(src_path))
                print(e.headers)
                raise
        except Exception as e:
            print(e)
            raise


def _install_locally(version, files):
    path = "{}/{}".format(_LOCAL_INSTALL_PATH, version)
    if os.path.exists(path):
        shutil.rmtree(path)

    os.makedirs(path)

    f = open("{}/maven-metadata-local.xml".format(path), 'w+')
    f.write("""\
<?xml version="1.0" encoding="UTF-8"?>
<metadata>
  <groupId>{group_id}</groupId>
  <artifactId>{artifact_id}</artifactId>
  <versioning>
    <release>{version}</release>
    <versions>
      <version>{version}</version>
    </versions>
    <lastUpdated>{timestamp}</lastUpdated>
  </versioning>
</metadata>
    """.format(version=version,
               timestamp=int(round(time.time() * 1000)),
               group_id=_GROUP_ID,
               artifact_id=_ARTIFACT_ID))
    f.close()

    for src_path, target_path in files:
        # Temporarily allow missing source jars
        if not os.path.exists(src_path):
            continue

        basename = os.path.basename(target_path)
        shutil.copyfile(src_path, os.path.join(path, basename))


def _build_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default="LOCAL-SNAPSHOT")
    return parser


if __name__ == "__main__":
    args = _build_parser().parse_args()

    aar = None
    pom = None

    # Assume the bazel rule will nicely put the aar and pom in the dist/ directory
    for root, _, files in os.walk("dist"):
        for filepath in files:
            if filepath.endswith("{}-pom.xml".format(_ARTIFACT_ID)):
                pom = os.path.join(root, filepath)
            elif filepath.endswith("{}.aar".format(_ARTIFACT_ID)):
                aar = os.path.join(root, filepath)

    if pom is None or aar is None:
        # TODO: raise exception?
        print("Unable to find pom.xml and aar in dist/")

    _upload(aar[: -len(".aar")], args.version)
