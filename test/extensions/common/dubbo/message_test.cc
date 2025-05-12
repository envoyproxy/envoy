#include "source/extensions/common/dubbo/message.h"

#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "hessian2/object.hpp"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {
namespace {

TEST(RpcRequestTest, SimpleSetAndGetTest) {
  RpcRequest request("a", "b", "c", "d");

  EXPECT_EQ("a", request.version());

  EXPECT_EQ("b", request.service());

  EXPECT_EQ("c", request.serviceVersion());

  EXPECT_EQ("d", request.method());
}

TEST(RpcRequestTest, InitializeWithBufferTest) {
  // Empty buffer.
  {
    RpcRequest request("a", "b", "c", "d");

    // Initialize the request with an empty buffer.
    Buffer::OwnedImpl buffer;
    request.content().initialize(buffer, buffer.length());

    // Try get the argument vector.
    EXPECT_EQ(request.content().arguments().size(), 0);

    // Try get the attachment group.
    EXPECT_FALSE(request.content().attachments().contains("group"));

    // attachments() call will make the content to be decoded.
    // And the content is empty, so the content will be treated as broken.
    // So the content will be reset to an empty state:
    // empty types, empty arguments, empty attachments.
    EXPECT_EQ(request.content().buffer().toString(), std::string({0x0, 'H', 'Z'}));

    // Set the group.
    request.content().setAttachment("group", "group");
    EXPECT_EQ("group", request.content().attachments().at("group"));

    // buffer() call will re-encode the attachments into the content buffer.
    EXPECT_EQ(request.content().buffer().toString(),
              std::string({
                  0x0,                          // Empty string types
                  'H',                          // Attachments start
                  0x5, 'g', 'r', 'o', 'u', 'p', // Key
                  0x5, 'g', 'r', 'o', 'u', 'p', // Value
                  'Z'                           // Attachments end
              }));
  }

  // Buffer no argument but has attachment.
  {
    RpcRequest request("a", "b", "c", "d");

    Buffer::OwnedImpl buffer(std::string({
        0x0,                          // Empty string types
        'H',                          // Attachments start
        0x5, 'g', 'r', 'o', 'u', 'p', // Key
        0x5, 'g', 'r', 'o', 'u', 'p', // Value
        'Z',                          // Attachments end
        'x'                           // Anything that make no sense
    }));

    // Initialize the request with an empty buffer.
    request.content().initialize(buffer, buffer.length() - 1);

    EXPECT_EQ(1, buffer.length()); // Other data should be consumed.
    EXPECT_EQ(buffer.toString(), "x");

    // Try get the argument vector.
    EXPECT_EQ(request.content().arguments().size(), 0);

    // Try get the attachment group.
    EXPECT_EQ("group", request.content().attachments().at("group"));

    EXPECT_EQ(request.content().buffer().toString(),
              std::string({
                  0x0,                          // Empty string types
                  'H',                          // Attachments start
                  0x5, 'g', 'r', 'o', 'u', 'p', // Key
                  0x5, 'g', 'r', 'o', 'u', 'p', // Value
                  'Z'                           // Attachments end
              }));

    // Overwrite the group.
    request.content().setAttachment("group", "groupx");

    // buffer() call will re-encode the attachments into the content buffer.
    EXPECT_EQ(request.content().buffer().toString(),
              std::string({
                  0x0,                               // Empty string types
                  'H',                               // Attachments start
                  0x5, 'g', 'r', 'o', 'u', 'p',      // Key
                  0x6, 'g', 'r', 'o', 'u', 'p', 'x', // Value
                  'Z'                                // Attachments end
              }));

    request.content().delAttachment("group");

    // buffer() call will re-encode the attachments into the content buffer.
    EXPECT_EQ(request.content().buffer().toString(),
              std::string({
                  0x0, // Empty string types
                  'H', // Attachments start
                  'Z'  // Attachments end
              }));
  }

  // Broken buffer where -1 is used to tell the arguments number but
  // no following types string.
  {
    RpcRequest request("a", "b", "c", "d");
    Buffer::OwnedImpl buffer(std::string({
        '\x8f', // -1 means the following bytes is types string
        'H',
        'Z',
    }));

    // Initialize the request with the broken buffer.
    request.content().initialize(buffer, buffer.length());

    EXPECT_EQ(0, request.content().arguments().size());

    // The content is broken, so the content will be reset to an empty state:
    // empty types, empty arguments, empty attachments.
    EXPECT_EQ(request.content().buffer().toString(), std::string({0x0, 'H', 'Z'}));
  }

  // Broken buffer where -2 is used to tell the arguments number. This is
  // unexpected number.
  {
    RpcRequest request("a", "b", "c", "d");
    Buffer::OwnedImpl buffer(std::string({
        '\x8e', // -2 is unexpected
        'H',
        'Z',
    }));

    // Initialize the request with the broken buffer.
    request.content().initialize(buffer, buffer.length());

    EXPECT_EQ(0, request.content().arguments().size());

    // The content is broken, so the content will be reset to an empty state:
    // empty types, empty arguments, empty attachments.
    EXPECT_EQ(request.content().buffer().toString(), std::string({0x0, 'H', 'Z'}));
  }

  // Correct buffer where -1 is used to tell the arguments number.
  {
    RpcRequest request("a", "b", "c", "d");
    Buffer::OwnedImpl buffer(std::string({
        '\x8f',                          // -1 means the following bytes is types string
        0x2,    'Z', 'Z',                // The types string
        'T',    'F',                     // true and false
        'H',                             // Attachments start
        0x5,    'g', 'r', 'o', 'u', 'p', // Key
        0x5,    'g', 'r', 'o', 'u', 'p', // Value
        'Z'                              // Attachments end
    }));

    // Initialize the request with the correct buffer.
    request.content().initialize(buffer, buffer.length());

    // Get the argument vector.
    const auto& args = request.content().arguments();

    // There are 2 arguments.
    EXPECT_EQ(2, args.size());

    // The first argument is true.
    EXPECT_EQ(true, args[0]->toBoolean().value().get());
    // The second argument is false.
    EXPECT_EQ(false, args[1]->toBoolean().value().get());

    // Get the attachment group.
    EXPECT_EQ("group", request.content().attachments().at("group"));

    EXPECT_EQ(request.content().buffer().toString(),
              std::string({
                  '\x8f',                          // -1 means the following bytes is types string
                  0x2,    'Z', 'Z',                // The types string
                  'T',    'F',                     // true and false
                  'H',                             // Attachments start
                  0x5,    'g', 'r', 'o', 'u', 'p', // Key
                  0x5,    'g', 'r', 'o', 'u', 'p', // Value
                  'Z'                              // Attachments end
              }));

    // Move the buffer.
    Buffer::OwnedImpl buffer2;
    request.content().bufferMoveTo(buffer2);

    // The buffer is moved, and if we call buffer() again, everything will be
    // re-encoded.
    // Note: -1 is removed because the single types string is enough.
    EXPECT_EQ(request.content().buffer().toString(), std::string({
                                                         0x2, 'Z', 'Z', // The types string
                                                         'T', 'F',      // true and false
                                                         'H',           // Attachments start
                                                         0x5, 'g', 'r', 'o', 'u', 'p', // Key
                                                         0x5, 'g', 'r', 'o', 'u', 'p', // Value
                                                         'Z' // Attachments end
                                                     }));
  }

  // Correct buffer with non-negative integer for the arguments number.
  {
    RpcRequest request("a", "b", "c", "d");
    Buffer::OwnedImpl buffer(std::string({
        '\x92',                       // 2 means the following are two arguments
        'T', 'F',                     // true and false
        'H',                          // Attachments start
        0x5, 'g', 'r', 'o', 'u', 'p', // Key
        0x5, 'g', 'r', 'o', 'u', 'p', // Value
        'Z'                           // Attachments end
    }));

    // Initialize the request with the correct buffer.
    request.content().initialize(buffer, buffer.length());

    // Get the argument vector.
    const auto& args = request.content().arguments();

    // There are 2 arguments.
    EXPECT_EQ(2, args.size());

    // The first argument is true.
    EXPECT_EQ(true, args[0]->toBoolean().value().get());
    // The second argument is false.
    EXPECT_EQ(false, args[1]->toBoolean().value().get());

    // Get the attachment group.
    EXPECT_EQ("group", request.content().attachments().at("group"));

    // Move the buffer.
    Buffer::OwnedImpl buffer2;
    request.content().bufferMoveTo(buffer2);

    // No types string is provided and the arguments are not empty.
    // So the number of arguments will be used directly.
    EXPECT_EQ(request.content().buffer().toString(),
              std::string({
                  '\x92',                       // 2 means the following are two arguments
                  'T', 'F',                     // true and false
                  'H',                          // Attachments start
                  0x5, 'g', 'r', 'o', 'u', 'p', // Key
                  0x5, 'g', 'r', 'o', 'u', 'p', // Value
                  'Z'                           // Attachments end
              }));
  }

  // Normal correct buffer with normal types string.
  {
    RpcRequest request("a", "b", "c", "d");
    Buffer::OwnedImpl buffer(std::string({
        0x2, 'Z', 'Z',                // The types string
        'T', 'F',                     // true and false
        'H',                          // Attachments start
        0x5, 'g', 'r', 'o', 'u', 'p', // Key
        0x5, 'g', 'r', 'o', 'u', 'p', // Value
        'T',                          // Key
        'F',                          // Value
        'Z'                           // Attachments end
    }));

    // Initialize the request with the correct buffer.
    request.content().initialize(buffer, buffer.length());

    // Get the argument vector.
    const auto& args = request.content().arguments();

    // There are 2 arguments.
    EXPECT_EQ(2, args.size());

    // The first argument is true.
    EXPECT_EQ(true, args[0]->toBoolean().value().get());
    // The second argument is false.
    EXPECT_EQ(false, args[1]->toBoolean().value().get());

    // Only string key and value are used.
    EXPECT_EQ(1, request.content().attachments().size());

    // Get the attachment group.
    EXPECT_EQ("group", request.content().attachments().at("group"));

    // Move the buffer.
    Buffer::OwnedImpl buffer2;
    request.content().bufferMoveTo(buffer2);

    EXPECT_EQ(request.content().buffer().toString(), std::string({
                                                         0x2, 'Z', 'Z', // The types string
                                                         'T', 'F',      // true and false
                                                         'H',           // Attachments start
                                                         0x5, 'g', 'r', 'o', 'u', 'p', // Key
                                                         0x5, 'g', 'r', 'o', 'u', 'p', // Value
                                                         'Z', // Attachments end
                                                     }));
  }

  // Buffer without attachments.
  {
    RpcRequest request("a", "b", "c", "d");
    Buffer::OwnedImpl buffer(std::string({
        0x2, 'Z', 'Z', // The types string
        'T', 'F',      // true and false
    }));

    // Initialize the request with the correct buffer.
    request.content().initialize(buffer, buffer.length());

    // Get the argument vector.
    const auto& args = request.content().arguments();

    // There are 2 arguments.
    EXPECT_EQ(2, args.size());

    // The first argument is true.
    EXPECT_EQ(true, args[0]->toBoolean().value().get());
    // The second argument is false.
    EXPECT_EQ(false, args[1]->toBoolean().value().get());

    EXPECT_TRUE(request.content().attachments().empty());

    // Move the buffer.
    Buffer::OwnedImpl buffer2;
    request.content().bufferMoveTo(buffer2);

    // Empty attachments will be encoded to the buffer when buffer() is called
    // and the underlying buffer is empty.
    EXPECT_EQ(request.content().buffer().toString(), std::string({
                                                         0x2, 'Z', 'Z', // The types string
                                                         'T', 'F',      // true and false
                                                         'H',           // Attachments start
                                                         'Z',           // Attachments end
                                                     }));

    // Set the attachment.
    request.content().setAttachment("group", "group");

    // buffer() call will re-encode the attachments into the content buffer.
    EXPECT_EQ(request.content().buffer().toString(), std::string({
                                                         0x2, 'Z', 'Z', // The types string
                                                         'T', 'F',      // true and false
                                                         'H',           // Attachments start
                                                         0x5, 'g', 'r', 'o', 'u', 'p', // Key
                                                         0x5, 'g', 'r', 'o', 'u', 'p', // Value
                                                         'Z' // Attachments end
                                                     }));
  }
}

TEST(RpcRequestTest, InitializeWithDecodedValuesTest) {
  RpcRequest request("a", "b", "c", "d");

  ArgumentVec args;
  args.push_back(std::make_unique<Hessian2::BooleanObject>(true));
  args.push_back(std::make_unique<Hessian2::BooleanObject>(false));

  Attachments attachs;
  attachs.emplace("group", "group");

  // Initialize the request with the given types and arguments and attachments.
  request.content().initialize(std::string("ZZ"), std::move(args), std::move(attachs));

  // Get the argument vector.
  const auto& args2 = request.content().arguments();

  // There are 2 arguments.
  EXPECT_EQ(2, args2.size());

  // The first argument is true.
  EXPECT_EQ(true, args2[0]->toBoolean().value().get());
  // The second argument is false.
  EXPECT_EQ(false, args2[1]->toBoolean().value().get());

  // Get the attachment group.
  EXPECT_EQ("group", request.content().attachments().at("group"));

  // Get the buffer.
  EXPECT_EQ(request.content().buffer().toString(), std::string({
                                                       0x2, 'Z', 'Z', // The types string
                                                       'T', 'F',      // true and false
                                                       'H',           // Attachments start
                                                       0x5, 'g', 'r', 'o', 'u', 'p', // Key
                                                       0x5, 'g', 'r', 'o', 'u', 'p', // Value
                                                       'Z' // Attachments end
                                                   }));
}

TEST(RpcResponseTest, SimpleSetAndGetTest) {
  RpcResponse response;

  EXPECT_EQ(absl::nullopt, response.responseType());

  // Set the response type and validate we can get it.
  response.setResponseType(RpcResponseType::ResponseNullValueWithAttachments);
  EXPECT_EQ(RpcResponseType::ResponseNullValueWithAttachments, response.responseType().value());
}

TEST(RpcResponseTest, InitializeWithBufferTest) {
  // Empty buffer.
  {
    RpcResponse response;

    // Initialize the response with an empty buffer.
    Buffer::OwnedImpl buffer;
    response.content().initialize(buffer, buffer.length());

    // Try get the result.
    EXPECT_EQ(response.content().result(), nullptr);

    // Try get the attachments.
    EXPECT_FALSE(response.content().attachments().contains("group"));

    // attachments() call will make the content to be decoded.
    // And the content is empty, so the content will be treated as broken.
    // So the content will be reset to an empty state:
    // empty result, empty attachments.
    EXPECT_EQ(response.content().buffer().toString(), std::string({'N', 'H', 'Z'}));

    // Set the group.
    response.content().setAttachment("group", "group");
    EXPECT_EQ("group", response.content().attachments().at("group"));

    // buffer() call will re-encode the attachments into the content buffer.
    EXPECT_EQ(response.content().buffer().toString(), std::string({
                                                          'N',
                                                          'H', // Attachments start
                                                          0x5, 'g', 'r', 'o', 'u', 'p', // Key
                                                          0x5, 'g', 'r', 'o', 'u', 'p', // Value
                                                          'Z' // Attachments end
                                                      }));
  }

  // Buffer with result but no attachments.
  {
    RpcResponse response;

    Buffer::OwnedImpl buffer(std::string({
        'T', // true
    }));

    // Initialize the response with the buffer.
    response.content().initialize(buffer, buffer.length());

    // Get the result.
    const auto* result = response.content().result();

    // The result is a boolean.
    EXPECT_EQ(true, result->toBoolean().value().get());

    // Try get the attachments.
    EXPECT_FALSE(response.content().attachments().contains("group"));

    // Move the buffer.
    Buffer::OwnedImpl buffer2;
    response.content().bufferMoveTo(buffer2);

    // Empty attachments will be encoded to the buffer when buffer() is called
    // and the underlying buffer is empty.
    EXPECT_EQ(response.content().buffer().toString(), std::string({
                                                          'T', // true
                                                          'H', // Attachments start
                                                          'Z', // Attachments end
                                                      }));

    // Set the attachment.
    response.content().setAttachment("group", "group");

    // buffer() call will re-encode the attachments into the content buffer.
    EXPECT_EQ(response.content().buffer().toString(), std::string({
                                                          'T', // true
                                                          'H', // Attachments start
                                                          0x5, 'g', 'r', 'o', 'u', 'p', // Key
                                                          0x5, 'g', 'r', 'o', 'u', 'p', // Value
                                                          'Z' // Attachments end
                                                      }));

    // Remove the attachment.
    response.content().delAttachment("group");

    // buffer() call will re-encode the attachments into the content buffer.
    EXPECT_EQ(response.content().buffer().toString(), std::string({
                                                          'T', // true
                                                          'H', // Attachments start
                                                          'Z'  // Attachments end
                                                      }));
  }

  // Buffer with result and attachments.
  {
    RpcResponse response;

    Buffer::OwnedImpl buffer(std::string({
        'T',                          // true
        'H',                          // Attachments start
        0x5, 'g', 'r', 'o', 'u', 'p', // Key
        0x5, 'g', 'r', 'o', 'u', 'p', // Value
        'T',                          // Key
        'F',                          // Value
        'Z'                           // Attachments end
    }));

    // Initialize the response with the buffer.
    response.content().initialize(buffer, buffer.length());

    // Get the result.
    const auto* result = response.content().result();

    // The result is a boolean.
    EXPECT_EQ(true, result->toBoolean().value().get());

    // Only string key and value are used.
    EXPECT_EQ(1, response.content().attachments().size());

    // Get the attachment group.
    EXPECT_EQ("group", response.content().attachments().at("group"));
  }
}

TEST(RpcResponseTest, InitializeWithDecodedValuesTest) {
  RpcResponse response;

  Hessian2::ObjectPtr result = std::make_unique<Hessian2::BooleanObject>(true);
  Attachments attachs;
  attachs.emplace("group", "group");

  // Initialize the response with the given result and attachments.
  response.content().initialize(std::move(result), std::move(attachs));

  // Get the result.
  const auto* result2 = response.content().result();

  // The result is a boolean.
  EXPECT_EQ(true, result2->toBoolean().value().get());

  // Get the attachment group.
  EXPECT_EQ("group", response.content().attachments().at("group"));

  // Get the buffer.
  EXPECT_EQ(response.content().buffer().toString(), std::string({
                                                        'T', // true
                                                        'H', // Attachments start
                                                        0x5, 'g', 'r', 'o', 'u', 'p', // Key
                                                        0x5, 'g', 'r', 'o', 'u', 'p', // Value
                                                        'Z' // Attachments end
                                                    }));
}

} // namespace
} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
