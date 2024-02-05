Credentials
-----------

The filter uses a few different credentials providers to obtain an AWS access key ID, AWS secret access key, and AWS session token.
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
   ``Expiration``). To enable this credentials provider set ``envoy.reloadable_features.use_http_client_to_fetch_aws_credentials`` to ``true``
   so that it can use http async client to fetch the credentials. This provider is not compatible with :ref:`Grpc Credentials AWS AwsIamConfig
   <envoy_v3_api_file_envoy/config/grpc_credential/v3/aws_iam.proto>` plugin which can only support deprecated libcurl credentials fetcher
   , see https://github.com/envoyproxy/envoy/pull/30626. To fetch the credentials a static cluster is required with the name
   ``sts_token_service_internal`` pointing towards regional AWS Security Token Service. The static internal cluster will still be added even
   if initially ``envoy.reloadable_features.use_http_client_to_fetch_aws_credentials`` is not set so that subsequently if the reloadable feature
   is set to ``true`` the cluster config is available to fetch the credentials.

4. Either EC2 instance metadata or ECS task metadata. For EC2 instance metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and
   ``Token`` are used, and credentials are cached for 1 hour. For ECS task metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and
   ``Token`` are used, and credentials are cached for 1 hour or until they expire (according to the field ``Expiration``). Note that the
   latest update on AWS credentials provider utility provides an option to use http async client functionality instead of libcurl to fetch the
   credentials. This behavior can be changed by setting ``envoy.reloadable_features.use_http_client_to_fetch_aws_credentials`` to ``true``.
   The usage of libcurl is on the deprecation path and will be removed soon. To fetch the credentials from either EC2 instance
   metadata or ECS task metadata a static cluster is required pointing towards the credentials provider. The static cluster name has to be
   ``ec2_instance_metadata_server_internal`` for fetching from EC2 instance metadata or ``ecs_task_metadata_server_internal`` for fetching
   from ECS task metadata. If these clusters are not provided in the bootstrap configuration then either of these will be added by default.
   The static internal cluster will still be added even if initially ``envoy.reloadable_features.use_http_client_to_fetch_aws_credentials`` is
   not set so that subsequently if the reloadable feature is set to ``true`` the cluster config is available to fetch the credentials.
