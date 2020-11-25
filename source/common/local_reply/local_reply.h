#pragma once

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "common/response_map/response_map.h"

namespace Envoy {
namespace LocalReply {

/*
 * A LocalReply is just a ResponseMap. Using a type alias here is a
 * straightforward way of expressing that. We still have the potential
 * to expand the implementation of a LocalReply beyond just a ResponseMap
 * without causing too much of a mess for our callers, but for now this is
 * simple enough.
 */
using LocalReply = ResponseMap::ResponseMap;
using LocalReplyPtr = std::unique_ptr<LocalReply>;

/**
 * Access log filter factory that reads from proto.
 */
class Factory {
public:
  /**
   * Create a LocalReply object from ProtoConfig
   */
  static LocalReplyPtr
  create(const envoy::extensions::filters::network::http_connection_manager::v3::LocalReplyConfig&
             config,
         Server::Configuration::FactoryContext& context);

  /**
   * Create a default LocalReply object with empty config.
   * It is used at places without Server::Configuration::FactoryContext.
   */
  static LocalReplyPtr createDefault();
};

} // namespace LocalReply
} // namespace Envoy
