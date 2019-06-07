# Envoy Mobile

Mobile client networking libraries based on the [Envoy](https://www.envoyproxy.io/) project.

## Documentation

* TODO(mattklein123): Fill in when hosted.

## Contact

* [envoy-mobile-announce](https://groups.google.com/forum/#!forum/envoy-mobile-announce): Low
  frequency mailing list where we will email announcements only.
* [envoy-mobile-users](https://groups.google.com/forum/#!forum/envoy-mobile-users): General user
  discussion.
* [envoy-mobile-dev](https://groups.google.com/forum/#!forum/envoy-mobile-dev): Envoy Mobile
  developer discussion (APIs, feature design, etc.).
* [Slack](https://envoyproxy.slack.com/): Slack, to get invited go
  [here](https://envoyslack.cncf.io). We can be found in the **#envoy-mobile** room.

## Contributing

Contributing to Envoy Mobile is fun! To get started:

* [Contributing guide](CONTRIBUTING.md)
* Please make sure that you let us know if you are working on an issue so we don't duplicate work!

## Copyright

Envoy Mobile is Apache licensed and copyrighted by Lyft. Envoy and the Envoy logo are
copyrighted by the *Envoy proxy authors* and the
[Cloud Native Computing Foundation](https://cncf.io). The Envoy name is used with permission from
the CNCF under the assumption that if this project finds traction it will be donated to the
Envoy proxy project.

## Building

### Requirements

- Xcode 10.2.1
- Bazel 0.26.0

### iOS

#### Build the iOS static framework:

```
bazel build ios_dist --config=ios
```

This will yield an `Envoy.framework` in the [`dist` directory](./dist).

#### Run the Swift app:

```
bazel run //examples/swift/hello_world:app --config=ios
```

#### Run the Objective-C app:

```
bazel run //examples/objective-c/hello_world:app --config=ios
```

### Android

#### Build the Android AAR:

```
bazel build android_dist --config=android
```

#### Run the Java app:

```
bazel mobile-install //examples/java/hello_world:hello_envoy --fat_apk_cpu=x86
```

If you have issues getting Envoy to compile locally, see the
[Envoy docs on building with Bazel](https://github.com/envoyproxy/envoy/tree/master/bazel).
