#pragma once

#include "envoy/config/core/v3/config_source.pb.h"

#include "common/protobuf/protobuf.h"

// Convenience macro for downgrading a message and obtaining a reference.
#define API_DOWNGRADE(msg) (*Envoy::Config::VersionConverter::downgrade(msg)->msg_)

// Convenience macro for recovering original message and obtaining a reference.
#define API_RECOVER_ORIGINAL(msg) (*Envoy::Config::VersionConverter::recoverOriginal(msg)->msg_)

namespace Envoy {
namespace Config {

// An instance of a dynamic message from a DynamicMessageFactory.
struct DynamicMessage {
  // The dynamic message factory must outlive the message.
  Protobuf::DynamicMessageFactory dynamic_msg_factory_;

  // Dynamic message.
  ProtobufTypes::MessagePtr msg_;
};

using DynamicMessagePtr = std::unique_ptr<DynamicMessage>;

class VersionConverter {
public:
  /**
   * Upgrade a message from an earlier to later version of the Envoy API. This
   * performs a simple wire-level reinterpretation of the fields. As a result of
   * shadow protos, earlier deprecated fields such as foo are materialized as
   * hidden_envoy_deprecated_foo.
   *
   * This should be used when you have wire input (e.g. bootstrap, xDS, some
   * opaque config) that might be at any supported version and want to upgrade
   * to Envoy's internal latest API usage.
   *
   * @param prev_message previous version message input.
   * @param next_message next version message to generate.
   *
   * @throw EnvoyException if a Protobuf (de)serialization error occurs.
   */
  static void upgrade(const Protobuf::Message& prev_message, Protobuf::Message& next_message);

  /**
   * Downgrade a message to the previous version. If no previous version exists,
   * the given message is copied in the return value. This is not super
   * efficient, most uses are expected to be tests and performance agnostic
   * code.
   *
   * This is used primarily in tests, to allow tests to internally use the
   * latest supported API but ensure that earlier versions are used on the wire.
   *
   * @param message message input.
   * @return DynamicMessagePtr with the downgraded message (and associated
   *         factory state).
   *
   * @throw EnvoyException if a Protobuf (de)serialization error occurs.
   */
  static DynamicMessagePtr downgrade(const Protobuf::Message& message);

  /**
   * Obtain JSON wire representation for an Envoy internal API message at v3
   * based on a given transport API version. This will downgrade() to an earlier
   * version or scrub the shadow deprecated fields in the existing one.
   *
   * This is typically used when Envoy is generating a JSON wire message from
   * some internally generated message, e.g. DiscoveryRequest, and we want to
   * ensure it matches a specific API version. For example, a v3
   * DiscoveryRequest must have any deprecated v2 fields removed (they only
   * exist because of shadowing) and a v2 DiscoveryRequest needs to have type
   * envoy.api.v2.DiscoveryRequest to ensure JSON representations have the
   * correct field names (after renames/deprecations are reversed).
   *
   * @param message message input.
   * @param api_version target API version.
   * @return std::string JSON representation.
   */
  static std::string getJsonStringFromMessage(const Protobuf::Message& message,
                                              envoy::config::core::v3::ApiVersion api_version);

  /**
   * Modify a v3 message to make it suitable for sending as a gRPC message. This
   * requires that a v3 message has hidden_envoy_deprecated_* fields removed,
   * and that for all versions that original type information is removed.
   *
   * @param message message to modify.
   * @param api_version target API version.
   */
  static void prepareMessageForGrpcWire(Protobuf::Message& message,
                                        envoy::config::core::v3::ApiVersion api_version);

  /**
   * Annotate an upgraded message with original message type information.
   *
   * @param prev_descriptor descriptor for original type.
   * @param upgraded_message upgraded message.
   */
  static void annotateWithOriginalType(const Protobuf::Descriptor& prev_descriptor,
                                       Protobuf::Message& upgraded_message);

  /**
   * For a message that may have been upgraded, recover the original message.
   * This is useful for config dump, debug output etc.
   *
   * @param upgraded_message upgraded message input.
   *
   * @return DynamicMessagePtr original message (as a dynamic message).
   *
   * @throw EnvoyException if a Protobuf (de)serialization error occurs.
   */
  static DynamicMessagePtr recoverOriginal(const Protobuf::Message& upgraded_message);

  /**
   * Remove original type information, when it's not needed, e.g. in tests.
   *
   * @param message upgraded message to scrub.
   */
  static void eraseOriginalTypeInformation(Protobuf::Message& message);
};

class VersionUtil {
public:
  // Some helpers for working with earlier message version deprecated fields.
  static void scrubHiddenEnvoyDeprecated(Protobuf::Message& message);

  // A prefix that is added to deprecated fields names upon shadowing.
  static const char DeprecatedFieldShadowPrefix[];
};

} // namespace Config
} // namespace Envoy
