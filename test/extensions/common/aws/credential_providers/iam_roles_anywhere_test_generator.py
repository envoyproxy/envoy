# To use this test generation script
# 1. Download https://github.com/awslabs/iam-roles-anywhere-session
# 2. Apply test/extensions/common/aws/credential_providers/iam_roles_anywhere_test_generator.diff to src/iam_rolesanywhere_session/iam_rolesanywhere_session.py
# 3. Ensure test certificates exist in CERTPATH and use the output from this tool as matches for correctly generated Envoy ignatures

from iam_rolesanywhere_session import IAMRolesAnywhereSession
from freezegun import freeze_time

import boto3
import logging
import sys
import time

CERTPATH = "/example/envoytestcerts/"
CERT = CERTPATH + "certificate.pem"
PKEY = CERTPATH + "private-key.pem"
CHAIN = CERTPATH + "chain.pem"


@freeze_time("2018-01-02 03:04:05 GMT+0000")
def no_chain():

    roles_anywhere_session = IAMRolesAnywhereSession(
        role_arn="arn:aws:iam::1234567890123:role/rolesanywhere",
        trust_anchor_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f",
        profile_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf",
        certificate=CERT,
        private_key=PKEY,
        region="ap-southeast-2",
        raw_payload=
        '{"durationSeconds":3600.0,"profileArn":"arn:profile-arn","roleArn":"arn:role-arn","roleSessionName":"session","trustAnchorArn":"arn:trust-anchor-arn"}'
    ).get_session()
    s3 = roles_anywhere_session.client("s3")
    try:
        print(s3.list_buckets())
    except:
        pass


@freeze_time("2018-01-02 03:04:05 GMT+0000")
def chain():

    roles_anywhere_session = IAMRolesAnywhereSession(
        role_arn="arn:aws:iam::1234567890123:role/rolesanywhere",
        trust_anchor_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f",
        profile_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf",
        certificate=CERT,
        private_key=PKEY,
        certificate_chain=CHAIN,
        region="ap-southeast-2",
        raw_payload=
        '{"durationSeconds":3600.0,"profileArn":"arn:profile-arn","roleArn":"arn:role-arn","roleSessionName":"session","trustAnchorArn":"arn:trust-anchor-arn"}'
    ).get_session()
    s3 = roles_anywhere_session.client("s3")
    try:
        print(s3.list_buckets())
    except:
        pass


@freeze_time("2018-01-02 05:04:05 GMT+0000")
def fast_forward():

    roles_anywhere_session = IAMRolesAnywhereSession(
        role_arn="arn:aws:iam::1234567890123:role/rolesanywhere",
        trust_anchor_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f",
        profile_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf",
        certificate=CERT,
        private_key=PKEY,
        certificate_chain=CHAIN,
        region="ap-southeast-2",
        raw_payload=
        '{"durationSeconds":3600.0,"profileArn":"arn:profile-arn","roleArn":"arn:role-arn","roleSessionName":"session","trustAnchorArn":"arn:trust-anchor-arn"}'
    ).get_session()
    s3 = roles_anywhere_session.client("s3")
    try:
        print(s3.list_buckets())
    except:
        pass


@freeze_time("2018-01-02 03:04:05 GMT+0000")
def custom_session():

    roles_anywhere_session = IAMRolesAnywhereSession(
        role_arn="arn:aws:iam::1234567890123:role/rolesanywhere",
        trust_anchor_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f",
        profile_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf",
        certificate=CERT,
        private_key=PKEY,
        region="ap-southeast-2",
        raw_payload=
        '{"durationSeconds":3600.0,"profileArn":"arn:profile-arn","roleArn":"arn:role-arn","roleSessionName":"mysession","trustAnchorArn":"arn:trust-anchor-arn"}'
    ).get_session()
    s3 = roles_anywhere_session.client("s3")
    try:
        print(s3.list_buckets())
    except:
        pass


@freeze_time("2018-01-02 03:04:05 GMT+0000")
def blank_session():

    roles_anywhere_session = IAMRolesAnywhereSession(
        role_arn="arn:aws:iam::1234567890123:role/rolesanywhere",
        trust_anchor_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f",
        profile_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf",
        certificate=CERT,
        private_key=PKEY,
        region="ap-southeast-2",
        raw_payload=
        '{"durationSeconds":3600.0,"profileArn":"arn:profile-arn","roleArn":"arn:role-arn","trustAnchorArn":"arn:trust-anchor-arn"}'
    ).get_session()
    s3 = roles_anywhere_session.client("s3")
    try:
        print(s3.list_buckets())
    except:
        pass


@freeze_time("2018-01-02 03:04:05 GMT+0000")
def custom_duration():

    roles_anywhere_session = IAMRolesAnywhereSession(
        role_arn="arn:aws:iam::1234567890123:role/rolesanywhere",
        trust_anchor_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:trust-anchor/8d105284-f0a7-4939-a7e6-8df768ea535f",
        profile_arn=
        "arn:aws:rolesanywhere:ap-southeast-2:1234567890123:profile/4af0c6cf-506a-4469-b1b5-5f3fecdaabdf",
        certificate=CERT,
        private_key=PKEY,
        region="ap-southeast-2",
        raw_payload=
        '{"durationSeconds":123.0,"profileArn":"arn:profile-arn","roleArn":"arn:role-arn","roleSessionName":"mysession","trustAnchorArn":"arn:trust-anchor-arn"}'
    ).get_session()
    s3 = roles_anywhere_session.client("s3")
    try:
        print(s3.list_buckets())
    except:
        pass


boto3.set_stream_logger('', logging.DEBUG)

if sys.argv[1] == "1":
    # used for StandardRSASigning test
    no_chain()
if sys.argv[1] == "2":
    # used for StandardRSASigningWithChain test
    chain()
if sys.argv[1] == "3":
    # used for CredentialExpiration test
    fast_forward()
if sys.argv[1] == "4":
    # used for StandardRSASigningCustomSessionName test
    custom_session()
if sys.argv[1] == "5":
    # used for StandardRSASigningBlankSessionName test
    blank_session()
if sys.argv[1] == "6":
    # used for StandardRSASigningCustomDuration test
    custom_duration()
