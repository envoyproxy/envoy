Fixed macOS build failure (#45061) for the Hickory DNS resolver by linking
Apple's ``SystemConfiguration`` framework. The framework is required by the
``system-configuration`` crate that ``hickory-resolver`` pulls in via its
``system-config`` feature.
