#!/usr/bin/env python

import argparse
import base64
import hashlib
import json
import os
import shutil
import sys
import time

try:
    from urllib.request import urlopen, Request, HTTPError
except ImportError:  # python 2
    from urllib2 import urlopen, Request, HTTPError

_USER_CREDS = os.environ.get("READWRITE_USER", "")
_KEY_CREDS = os.environ.get("READWRITE_API_KEY", "")
BASE64_ENCODED_CREDENTIALS = base64.b64encode("{}:{}".format(_USER_CREDS, _KEY_CREDS).encode()).decode()

_ARTIFACT_HOST_URL = "https://oss.sonatype.org/service/local/staging"
_GROUP_ID = "io.envoyproxy.envoymobile"
_ARTIFACT_ID = "envoy"
_LOCAL_INSTALL_PATH = os.path.expanduser("~/.m2/repository/{directory}/envoy".format(
    directory=_GROUP_ID.replace(".", "/"),
    artifact_id=_ARTIFACT_ID))


def _resolve_name(file):
    file_name, file_extension = os.path.splitext(file)

    extension = file_extension[1:]
    if extension == "asc" or extension == "sha256":
        if file_name.endswith("pom.xml"):
            return ".pom", extension
        elif file_name.endswith("javadoc.jar"):
            return "-javadoc.jar", extension
        elif file_name.endswith("sources.jar"):
            return "-sources.jar", extension
        elif file_name.endswith(".aar"):
            return ".aar", extension
        elif file_name.endswith(".jar"):
            return ".jar", extension
    else:
        if file_name.endswith("pom"):
            return "", "pom"
        elif file_name.endswith("javadoc"):
            return "-javadoc", extension
        elif file_name.endswith("sources"):
            return "-sources", extension
        else:
            return "", extension


def _install_locally(version, files):
    path = "{}/{}".format(_LOCAL_INSTALL_PATH, version)

    if os.path.exists(path):
        shutil.rmtree(path)

    os.makedirs(path)

    for file in files:
        suffix, file_extension = _resolve_name(file)
        basename = "{name}-{version}{suffix}.{extension}".format(
            name=_ARTIFACT_ID,
            version=version,
            suffix=suffix,
            extension=file_extension
        )

        shutil.copyfile(file, os.path.join(path, basename))
        print("{file_name}\n{sha}\n".format(file_name=file, sha=_sha256(file)))


def _urlopen_retried(request, max_retries=500, attempt=1, delay_sec=1):
    """
    Retries a request via recursion. Retries happen after the provided delay. We do not exponentially back off.
    :param request: the request to be made
    :param max_retries: Number of retries to use, default is 500. The reason we are using such a high retry is because
    sonatype fails quite frequently
    :param attempt: The current attempt number for the request
    :param delay_sec: The delay before making a retried request
    :return: the response if successful, raises error otherwise
    """
    try:
        return urlopen(request)
    except HTTPError as e:
        if max_retries > attempt and e.code >= 500:
            print(
                "[{retry_attempt}/{max_retries} Retry attempt] Retrying request after {delay}s."
                " Received error code {code}"
                    .format(
                    retry_attempt=attempt,
                    max_retries=max_retries,
                    delay=delay_sec,
                    code=e.code
                ))
            time.sleep(delay_sec)
            return _urlopen_retried(request, max_retries, attempt + 1)
        elif max_retries <= attempt:
            print("Retry limit reached. Will not continue to retry. Received error code {}".format(e.code))
            raise e
        else:
            raise e


def _create_staging_repository(profile_id):
    try:
        url = os.path.join(_ARTIFACT_HOST_URL, "profiles/{}/start".format(profile_id))
        data = {
            'data': {
                'description': ''
            }
        }
        request = Request(url)
        request.add_header("Authorization", "Basic {}".format(BASE64_ENCODED_CREDENTIALS))
        request.add_header("Content-Type", "application/json")
        request.get_method = lambda: "POST"
        request.add_data(json.dumps(data))

        response = json.load(_urlopen_retried(request))
        staging_id = response["data"]["stagedRepositoryId"]
        print("staging id {} was created".format(staging_id))
        return staging_id
    except Exception as e:
        raise e


def _upload_files(staging_id, version, files, ascs, sha256):
    uploaded_file_count = 0

    # aggregate all the files for uploading
    all_files = files + ascs + sha256
    for file in all_files:
        # This will output "envoy", ".aar" for "envoy.aar
        print("Uploading file {}".format(file))
        suffix, file_extension = _resolve_name(file)
        basename = "{name}-{version}{suffix}.{extension}".format(
            name=_ARTIFACT_ID,
            version=version,
            suffix=suffix,
            extension=file_extension
        )

        artifact_url = os.path.join(
            _ARTIFACT_HOST_URL,
            "deployByRepositoryId/{}".format(staging_id),
            _GROUP_ID.replace('.', "/"),
            _ARTIFACT_ID,
            version,
            basename
        )

        try:
            with open(file, "rb") as f:
                request = Request(artifact_url, f.read())

            request.add_header("Authorization", "Basic {}".format(BASE64_ENCODED_CREDENTIALS))
            request.add_header("Content-Type", "application/x-{extension}".format(extension=file_extension))
            request.get_method = lambda: "PUT"
            _urlopen_retried(request)
            uploaded_file_count = uploaded_file_count + 1
        except HTTPError as e:
            if e.code == 403:
                # Don't need to pipe to error since we are ignoring duplicated uploads
                print("Ignoring duplicate upload for {}".format(artifact_url))
            else:
                raise e
        except Exception as e:
            raise e

    return uploaded_file_count


def _close_staging_repository(profile_id, staging_id):
    url = os.path.join(_ARTIFACT_HOST_URL, "profiles/{}/finish".format(profile_id))
    data = {
        'data': {
            'stagedRepositoryId': staging_id,
            'description': ''
        }
    }

    try:
        request = Request(url)

        request.add_header("Authorization", "Basic {}".format(BASE64_ENCODED_CREDENTIALS))
        request.add_header("Content-Type", "application/json")
        request.add_data(json.dumps(data))
        request.get_method = lambda: "POST"
        _urlopen_retried(request)
    except Exception as e:
        raise e


def _drop_staging_repository(staging_id, message):
    url = os.path.join(_ARTIFACT_HOST_URL, "bulk/drop")
    data = {
        'data': {
            'stagedRepositoryIds': [staging_id],
            'description': message
        }
    }

    try:
        request = Request(url)

        request.add_header("Authorization", "Basic {}".format(BASE64_ENCODED_CREDENTIALS))
        request.add_header("Content-Type", "application/json")
        request.add_data(json.dumps(data))
        request.get_method = lambda: "POST"
        _urlopen_retried(request)
    except Exception as e:
        raise e


def _release_staging_repository(staging_id):
    url = os.path.join(_ARTIFACT_HOST_URL, "bulk/promote")
    data = {
        'data': {
            'stagedRepositoryIds': [staging_id],
            'description': ''
        }
    }

    try:
        request = Request(url)

        request.add_header("Authorization", "Basic {}".format(BASE64_ENCODED_CREDENTIALS))
        request.add_header("Content-Type", "application/json")
        request.add_data(json.dumps(data))
        request.get_method = lambda: "POST"
        _urlopen_retried(request)
    except Exception as e:
        raise e


def _create_sha256_files(files):
    sha256_files = []
    for file in files:
        sha256_file_name = "{}.sha256".format(file)
        sha256 = _sha256(file)
        sha256_file = open(sha256_file_name, 'w+')
        sha256_file.write(sha256)
        sha256_file.close()
        sha256_files.append(sha256_file_name)
    return sha256_files


def _sha256(file_name):
    sha256 = hashlib.sha256()
    with open(file_name, 'rb') as file:
        for line in file.readlines():
            sha256.update(line)
    return sha256.hexdigest()


def _build_parser():
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile_id", required=False,
                        help="""
                        The staging profile id of the sonatype repository target.
                        This is the id in the sonatype web ui. The REST api is:
                        curl -u {usr}:{psswrd} -H "Accept: application/json"
                        https://oss.sonatype.org//nexus/service/local/staging/profile_repositories
                        """)
    parser.add_argument("--version", default="LOCAL-SNAPSHOT",
                        help="""
                        The version of the artifact to be published. `LOCAL-SNAPSHOT` is defaulted
                        if the version is not set. This version should be consistent with the pom.xml
                        provided.
                        """)
    parser.add_argument("--local", nargs='?', const=True, default=False,
                        help="""
                        For installing artifacts into local maven. For now, we only support
                        installing to the path `~/.m2/repository/io/envoyproxy/envoymobile/`
                        """)
    parser.add_argument("--files", nargs="+", required=True,
                        help="""
                        Files to upload

                        The checklist for Envoy Mobile files are:
                            envoy.aar
                            envoy-pom.xml
                            envoy-sources.jar
                            envoy-javadoc.jar
                        """)
    parser.add_argument("--signed_files", nargs="+", required=False,
                        help="""
                        Files to upload.
                        Sonatype requires uploaded artifacts to be gpg signed

                        GPG signed:
                            envoy.aar.asc
                            envoy-pom.xml.asc
                            envoy-sources.jar.asc
                            envoy-javadoc.jar.asc
                        """)
    return parser


if __name__ == "__main__":
    args = _build_parser().parse_args()

    version = args.version
    if args.local:
        _install_locally(version, args.files)
    else:
        staging_id = ""

        try:
            staging_id = _create_staging_repository(args.profile_id)
        except:
            sys.exit("Unable to create staging id")

        # Upload files using the staging_id, close the staging repository, and release
        # If an error occurs, we will attempt to drop the repository. The script will
        # need to be re-run to initiate another upload attempt
        try:
            print("Uploading files...")
            sha256_files = _create_sha256_files(args.files)
            uploaded_file_count = _upload_files(staging_id, version, args.files, args.signed_files, sha256_files)
            if uploaded_file_count > 0:
                print("Uploading files complete!")
                print("Closing staging repository...")
                _close_staging_repository(args.profile_id, staging_id)
                print("Closing staging complete!")
                print("Releasing artifact {}...".format(version))
                _release_staging_repository(staging_id)
                print("Release complete!")
            else:
                print("No files were uploaded. Dropping staging repository...")
                _drop_staging_repository(staging_id, "droppng release due to no uploaded files")
                print("Dropping staging id {} complete!".format(staging_id))
        except Exception as e:
            print(e)

            print("Unable to complete file upload. Will attempt to drop staging id: [{}]".format(staging_id))
            try:
                _drop_staging_repository(staging_id, "droppng release due to error")
                sys.exit("Dropping staging id: [{}] successful.".format(staging_id))
            except Exception as e:
                print(e)
                sys.exit("Dropping staging id: [{}] failed.".format(staging_id))
