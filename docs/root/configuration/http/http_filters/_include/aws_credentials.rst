Credentials
-----------

The filter uses a number of different credentials providers to obtain an AWS access key ID, AWS secret access key, and AWS session token.
It moves through the credentials providers in the order described below, stopping when one of them returns an access key ID and a
secret access key (the session token is optional).

1. Environment variables. The environment variables ``AWS_ACCESS_KEY_ID``, ``AWS_SECRET_ACCESS_KEY``, and ``AWS_SESSION_TOKEN`` are used.

2. The AWS credentials file. The environment variables ``AWS_SHARED_CREDENTIALS_FILE`` and ``AWS_PROFILE`` are respected if they are set, else
   the file ``~/.aws/credentials`` and profile ``default`` are used. The fields ``aws_access_key_id``, ``aws_secret_access_key``, and
   ``aws_session_token`` defined for the profile in the credentials file are used. These credentials are cached for 1 hour.

3. From `AssumeRoleWithWebIdentity <https://docs.aws.amazon.com/STS/latest/APIReference/API_AssumeRoleWithWebIdentity.html>`_ API call
   towards AWS Security Token Service using ``WebIdentityToken`` read from a file pointed by ``AWS_WEB_IDENTITY_TOKEN_FILE`` environment
   variable and role arn read from ``AWS_ROLE_ARN`` environment variable. The credentials are extracted from the fields ``AccessKeyId``,
   ``SecretAccessKey``, and ``SessionToken`` are used, and credentials are cached for 1 hour or until they expire (according to the field
   ``Expiration``).
   This provider is not compatible with :ref:`Grpc Credentials AWS AwsIamConfig <envoy_v3_api_file_envoy/config/grpc_credential/v3/aws_iam.proto>`
   plugin which can only support deprecated libcurl credentials fetcher (see `issue #30626 <https://github.com/envoyproxy/envoy/pull/30626>`_).
   To fetch the credentials a static cluster is created with the name ``sts_token_service_internal-<region>`` pointing towards regional
   AWS Security Token Service.

   Note: If ``signing_algorithm: AWS_SIGV4A`` is set, the logic for STS cluster host generation is as follows:
   - If the ``region`` is configured (either through profile, environment or inline) as a SigV4A region set
   - And if the first region in the region set contains a wildcard
   - Then STS cluster host is set to ``sts.amazonaws.com`` (or ``sts-fips.us-east-1.amazonaws.com`` if compiled with FIPS support
   - Else STS cluster host is set to ``sts.<first region in region set>.amazonaws.com``

  If you require the use of SigV4A signing and you are using an alternate partition, such as cn or GovCloud, you can ensure correct generation
  of the STS endpoint by setting the first region in your SigV4A region set to the correct region (such as ``cn-northwest-1`` with no wildcard)

4. Either EC2 instance metadata, ECS task metadata or EKS Pod Identity.
   For EC2 instance metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and ``Token`` are used, and credentials are cached for 1 hour.
   For ECS task metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and ``Token`` are used, and credentials are cached for 1 hour or
   until they expire (according to the field ``Expiration``).
   For EKS Pod Identity, The environment variable ``AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE`` will point to a mounted file in the container,
   containing the string required in the Authorization header sent to the EKS Pod Identity Agent. The fields ``AccessKeyId``, ``SecretAccessKey``,
   and ``Token`` are used, and credentials are cached for 1 hour or until they expire (according to the field ``Expiration``).
   Note that the latest update on AWS credentials provider utility provides an option to use http async client functionality instead of libcurl
   to fetch the credentials. To fetch the credentials from either EC2 instance metadata or ECS task metadata a static cluster pointing
   towards the credentials provider is required. The static cluster name has to be ``ec2_instance_metadata_server_internal`` for fetching from EC2 instance
   metadata or ``ecs_task_metadata_server_internal`` for fetching from ECS task metadata.

   If these clusters are not provided in the bootstrap configuration then either of these will be added by default.
   The static internal cluster will still be added even if initially ``envoy.reloadable_features.use_http_client_to_fetch_aws_credentials`` is
   not set so that subsequently if the reloadable feature is set to ``true`` the cluster config is available to fetch the credentials.

Statistics
----------

The following statistics are output under the ``aws.metadata_credentials_provider`` namespace:

.. csv-table::
  :header: Name, Type, Description
  :escape: '
  :widths: 1, 1, 2

  <provider_cluster>.credential_refreshes_performed, Counter, Total credential refreshes performed by this cluster
  <provider_cluster>.credential_refreshes_failed, Counter, Total credential refreshes failed by this cluster. For example', this would be incremented if a WebIdentity token was expired
  <provider_cluster>.credential_refreshes_succeeded, Counter, Total successful credential refreshes for this cluster. Successful refresh would indicate credentials are available for signing
  <provider_cluster>.metadata_refresh_state, Gauge, 0 means the cluster is in initial refresh state', ie no successful credential refreshes have been performed. In 0 state the cluster will attempt credential refresh up to a maximum of once every 30 seconds. 1 means the cluster is in normal credential expiration based refresh state
  <provider_cluster>.clusters_removed_by_cds, Counter, Number of metadata clusters removed during CDS refresh
  <provider_cluster>.clusters_readded_after_cds, Counter, Number of metadata clusters replaced when CDS deletion occurs
