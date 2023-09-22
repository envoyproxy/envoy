Credentials
-----------

The filter uses a few different credentials providers to obtain an AWS access key ID, AWS secret access key, and AWS session token.
It moves through the credentials providers in the order described below, stopping when one of them returns an access key ID and a
secret access key (the session token is optional).

1. Environment variables. The environment variables ``AWS_ACCESS_KEY_ID``, ``AWS_SECRET_ACCESS_KEY``, and ``AWS_SESSION_TOKEN`` are used.

2. The AWS credentials file. The environment variables ``AWS_SHARED_CREDENTIALS_FILE`` and ``AWS_PROFILE`` are respected if they are set, else
   the file ``~/.aws/credentials`` and profile ``default`` are used. The fields ``aws_access_key_id``, ``aws_secret_access_key``, and
   ``aws_session_token`` defined for the profile in the credentials file are used. These credentials are cached for 1 hour.

3. Either EC2 instance metadata or ECS task metadata. For EC2 instance metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and
   ``Token`` are used, and credentials are cached for 1 hour. For ECS task metadata, the fields ``AccessKeyId``, ``SecretAccessKey``, and
   ``Token`` are used, and credentials are cached for 1 hour or until they expire (according to the field ``Expiration``).
