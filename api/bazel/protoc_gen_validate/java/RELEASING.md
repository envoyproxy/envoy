# Releasing PGV Java components

These steps are for releasing the Java components of PGV:

- pgv-java-stub
- pgv-java-grpc
- pgv-artifacts

## Releasing using CI

Releasing from main is fully automated by CI:

Releasing from versioned tags is similar. To release version `vX.Y.Z`, first
create a Git tag called `vX.Y.Z` (preferably through the GitHub release flow),
then run the following to kick off a release build:

The [`Maven Deploy`](../.github/workflows/maven-publish.yaml) CI flow will use the version number from the tag to deploy
to the Maven repository.

## Manually releasing from git history

Manually releasing from git history is a more involved process, but allows you
to release from any point in the history.

1. Create a new `release/x.y.z` branch at the point you want to release.
1. Copy `java/settings.xml` to a scratch location.
1. Fill out the parameters in `settings.xml`. You will need a published GPG key
   for code signing and the EnvoyReleaseBot sonatype username and password.
1. Execute the release command, substituting the path to `settings.xml`, the
   `releaseVersion`, and the next `developmentVersion` (-SNAPSHOT).
1. Merge the release branch back into master.

```shell
mvn -B -s /path/to/settings.xml clean release:prepare release:perform \
  -Darguments="-s /path/to/settings.xml" \
  -DreleaseVersion=x.y.z \
  -DdevelopmentVersion=x.y.z-SNAPSHOT \
  -DscmCommentPrefix="java release: "
```
