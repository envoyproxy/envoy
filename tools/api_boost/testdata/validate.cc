#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/cluster.pb.validate.h"
#include "envoy/protobuf/message_validator.h"

#include "common/protobuf/utility.h"

void foo(Envoy::ProtobufMessage::ValidationVisitor& validator) {
  envoy::api::v2::Cluster msg;
  Envoy::MessageUtil::downcastAndValidate<const envoy::api::v2::Cluster&>(msg, validator);
}
