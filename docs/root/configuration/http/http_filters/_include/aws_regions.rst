Regions
-------

In a similar way to retrieving credentials, the region can be sourced from a number of locations. In order, these are the locations which are
checked, which align with the AWS SDK behavior:

1. The ``region`` parameter in the extension xDS configuration. For the ``aws_request_signing`` filter, this is the
   :ref:`region <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.region>` optional parameter.

2. Environment variables. The environment variables ``AWS_REGION``, ``AWS_DEFAULT_REGION`` are used.

3. The AWS credentials file. The environment variables ``AWS_SHARED_CREDENTIALS_FILE``, ``AWS_PROFILE`` and ``DEFAULT_AWS_PROFILE``
   are respected if they are set, else the file ``~/.aws/credentials`` and profile ``default`` are used. The field ``region`` defined
   for the profile in the credentials file is used.

4. The AWS config file. The environment variables ``AWS_CONFIG_FILE``, ``AWS_PROFILE``and ``DEFAULT_AWS_PROFILE`` are
   respected if they are set, else the file ``~/.aws/config`` and profile ``default`` are used. The field ``region`` defined for the
   profile in the config file is used.

*Note*: The :ref:`region <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.region>` parameter is mandatory, if :ref:`signing_algorithm <envoy_v3_api_field_extensions.filters.http.aws_request_signing.v3.AwsRequestSigning.signing_algorithm>` is set to ``AWS_SIGV4A``.
