Credentials
-----------

The filter uses a number of different credentials providers to obtain an AWS access key ID, AWS secret access key, and AWS session token.
By default, it moves through the credentials providers in the order described below, stopping when one of them returns an access key ID and a
secret access key (the session token is optional).

1. :ref:`inline_credentials <envoy_v3_api_field_extensions.common.aws.v3.AwsCredentialProvider.inline_credential>` field.
   If this field is configured, no other credentials providers will be used.

2. :ref:`credential_provider <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.credential_provider>` field.
   By using this field, the filter allows override of the default environment variables, credential parameters and file locations.
   Currently this supports both AWS credentials file locations and content, and AssumeRoleWithWebIdentity token files.
   If the :ref:`credential_provider <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.credential_provider>` field is provided,
   it can be used either to modify the default credentials provider chain, or when :ref:`custom_credential_provider_chain <envoy_v3_api_field_extensions.common.aws.v3.AwsCredentialProvider.custom_credential_provider_chain>`
   is set to ``true``, to create a custom credentials provider chain containing only the specified credentials provider settings. Examples of using these fields
   are provided in :ref:`configuration examples <config_http_filters_aws_request_signing_examples>`.

3. Environment variables. The environment variables ``AWS_ACCESS_KEY_ID``, ``AWS_SECRET_ACCESS_KEY``, and ``AWS_SESSION_TOKEN`` are used.

4. The AWS credentials file. The environment variables ``AWS_SHARED_CREDENTIALS_FILE`` and ``AWS_PROFILE`` are respected if they are set, else
   the file ``~/.aws/credentials`` and profile ``default`` are used. The fields ``aws_access_key_id``, ``aws_secret_access_key``, and
   ``aws_session_token`` defined for the profile in the credentials file are used. These credentials are cached for 1 hour.

5. From `AssumeRoleWithWebIdentity <https://docs.aws.amazon.com/STS/latest/APIReference/API_AssumeRoleWithWebIdentity.html>`_ API call
   towards AWS Security Token Service using ``WebIdentityToken`` read from a file pointed by ``AWS_WEB_IDENTITY_TOKEN_FILE`` environment
   variable and role arn read from ``AWS_ROLE_ARN`` environment variable. The credentials are extracted from the fields ``AccessKeyId``,
   ``SecretAccessKey``, and ``SessionToken`` are used, and credentials are cached for 1 hour or until they expire (according to the field
   ``Expiration``).
   This provider is not compatible with :ref:`Grpc Credentials AWS AwsIamConfig <envoy_v3_api_file_envoy/config/grpc_credential/v3/aws_iam.proto>`
   plugin which can only support deprecated libcurl credentials fetcher (see `issue #30626 <https://github.com/envoyproxy/envoy/pull/30626>`_).
   To fetch the credentials a static cluster is created with the name ``sts_token_service_internal-<region>`` pointing towards regional
   AWS Security Token Service.

   .. note::

      When ``signing_algorithm: AWS_SIGV4A`` is set, the STS cluster host is determined as follows:

      * If your ``region``` (set via profile, environment, or inline) is configured as a SigV4A region set **AND**
        contains a wildcard in the first region:

        - Standard endpoint: ``sts.amazonaws.com``
        - FIPS endpoint: ``sts-fips.us-east-1.amazonaws.com``

      * Otherwise:

        - Uses regional endpoint: ``sts.<first-region>.amazonaws.com``

  For alternate AWS partitions (e.g. China or GovCloud) with SigV4A signing, specify the correct regional endpoint by
  setting your first SigV4A region without wildcards (example: ``cn-northwest-1``)

6. Either EC2 instance metadata, ECS task metadata or EKS Pod Identity.
   For EC2 instance metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and ``Token`` are used, and credentials are cached for 1 hour.
   For ECS task metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and ``Token`` are used, and credentials are cached for 1 hour or
   until they expire (according to the field ``Expiration``).
   For EKS Pod Identity, The environment variable ``AWS_CONTAINER_AUTHORIZATION_TOKEN_FILE`` will point to a mounted file in the container,
   containing the string required in the Authorization header sent to the EKS Pod Identity Agent. The fields ``AccessKeyId``, ``SecretAccessKey``,
   and ``Token`` are used, and credentials are cached for 1 hour or until they expire (according to the field ``Expiration``).

   .. note::

      The AWS credentials provider now supports two methods for fetching credentials:

      * HTTP async client (new)
      * libcurl (legacy)

      To fetch credentials from EC2 or ECS, you must configure a static cluster pointing to the credentials provider:

      * For EC2: use cluster name ``ec2_instance_metadata_server_internal``
      * For ECS: use cluster name ``ecs_task_metadata_server_internal``

   These static clusters are handled automatically:

   * They are added by default if not specified in bootstrap configuration.
   * They are created even when ``envoy.reloadable_features.use_http_client_to_fetch_aws_credentials`` is disabled. This
     ensures the cluster configuration is ready when you enable HTTP client credential fetching later by setting the
     reloadable feature to ``true``.

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
