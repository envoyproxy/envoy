#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/sse/sse_event_codec.h"
#include "source/extensions/filters/http/ai_protocol_manager/sse/sse_scanner.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::IsOk;

// Kept out of the literals it prefixes: a hex escape swallows any hex digit that follows it.
constexpr absl::string_view kUtf8Bom("\xEF\xBB\xBF", 3);

// Every caller loops until its chunk drains, so an empty one never reaches the scanner; a scan of
// it would consume nothing and spin that loop.
TEST(SseScannerTest, EmptyChunkIsABug) {
  SseScanner scanner;
  EXPECT_ENVOY_BUG(scanner.scanLine(""), "SSE scanner given an empty chunk");
}

// Decoder tests for frames small enough to stay in memory. SseCodecBufferTest extends this
// fixture with somewhere to offload to, and covers the paths that need it.
class SseEventDecoderTest : public testing::Test {
protected:
  SseEventDecoderTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_) {}

  SseEventDecoder makeDecoder(SseEventDecoder::Config config = {}) {
    return SseEventDecoder(config, factory_, bridge_);
  }

  // Feeds `input` as a single chunk.
  absl::Status feed(SseEventDecoder& decoder, absl::string_view input) {
    Buffer::OwnedImpl buf;
    buf.add(input);
    return decoder.onData(buf, events_);
  }

  // Feeds `input` one byte at a time, so every boundary lands on a chunk edge.
  absl::Status feedByteWise(SseEventDecoder& decoder, absl::string_view input) {
    for (const char c : input) {
      Buffer::OwnedImpl buf;
      buf.add(absl::string_view(&c, 1));
      if (absl::Status status = decoder.onData(buf, events_); !status.ok()) {
        return status;
      }
    }
    return absl::OkStatus();
  }

  // The bytes an event's extras store holds. Only valid for a frame small enough that the store
  // never offloaded, which is every frame in this fixture.
  std::string extras(const SseEvent& event) {
    if (event.extras_store() == nullptr) {
      return {};
    }
    const Buffer::Instance* bytes = event.extras_store()->inMemoryBytes();
    return bytes == nullptr ? std::string() : bytes->toString();
  }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  // Declared before events_ so it outlives the per-frame stores they may carry.
  FakeBridge bridge_;
  std::vector<SseEventPtr> events_;
};

TEST_F(SseEventDecoderTest, SingleJsonFrame) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: {\"a\":1}\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_TRUE(events_[0]->has_data());
  EXPECT_TRUE(events_[0]->is_json());
  EXPECT_EQ(events_[0]->json().json()["a"], 1);
}

TEST_F(SseEventDecoderTest, NonJsonPayloadIsNotAnError) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: [DONE]\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_TRUE(events_[0]->has_data());
  EXPECT_FALSE(events_[0]->is_json());
  EXPECT_EQ(events_[0]->raw_data_as_string(), "[DONE]");
}

TEST_F(SseEventDecoderTest, AllMetadataFields) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, ": ping\nevent: delta\nid: 42\nretry: 1500\ndata: {}\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), ": ping\n");
  EXPECT_EQ(events_[0]->event(), "delta");
  EXPECT_EQ(events_[0]->id(), "42");
  EXPECT_EQ(events_[0]->retry(), "1500");
  EXPECT_TRUE(events_[0]->is_json());
}

// The grammar would have a client ignore a retry that is not all digits. The raw entry is
// retained in metadata() for passthrough serialization, while retry() returns std::nullopt
// per last-valid-wins semantics.
TEST_F(SseEventDecoderTest, NonNumericRetryIsKept) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "retry: soon\ndata: {}\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->retry(), std::nullopt);
  ASSERT_EQ(events_[0]->metadata().size(), 1);
  EXPECT_EQ(events_[0]->metadata()[0].value, "soon");
}

TEST_F(SseEventDecoderTest, MultipleDataLinesJoinWithNewline) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: first\ndata: second\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_FALSE(events_[0]->is_json());
  EXPECT_EQ(events_[0]->raw_data_as_string(), "first\nsecond");
}

TEST_F(SseEventDecoderTest, CommentOnlyKeepaliveCarriesNoData) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, ": keepalive\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), ": keepalive\n");
  EXPECT_FALSE(events_[0]->has_data());
}

TEST_F(SseEventDecoderTest, OnlyOneLeadingSpaceIsStripped) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data:no-space\n\ndata:  two-spaces\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 2);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "no-space");
  EXPECT_EQ(events_[1]->raw_data_as_string(), " two-spaces");
}

TEST_F(SseEventDecoderTest, ConsecutiveBlankLinesProduceNoEmptyEvents) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "\n\n\ndata: x\n\n\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "x");
}

TEST_F(SseEventDecoderTest, CrlfAndBareCrTerminators) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: crlf\r\n\r\ndata: cr\r\r"), IsOk());
  // The final CR is the last byte fed, so whether an LF joins it is still unknown and the frame it
  // ends is held until the stream does.
  ASSERT_EQ(events_.size(), 1);
  ASSERT_THAT(decoder.onEndStream(events_), IsOk());

  ASSERT_EQ(events_.size(), 2);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "crlf");
  EXPECT_TRUE(events_[0]->blank_line_terminated());
  EXPECT_EQ(events_[1]->raw_data_as_string(), "cr");
  EXPECT_TRUE(events_[1]->blank_line_terminated());
}

// A boundary CR at the end of a chunk is resolved by the first byte of the next one, whether or
// not that byte is the LF that would have completed a CRLF.
TEST_F(SseEventDecoderTest, BoundaryCrAtChunkEndResolvesOnTheNextChunk) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: a\r\r"), IsOk());
  EXPECT_EQ(events_.size(), 0);

  ASSERT_THAT(feed(decoder, "data: b\r\n\r\n"), IsOk());

  ASSERT_EQ(events_.size(), 2);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "a");
  EXPECT_EQ(events_[1]->raw_data_as_string(), "b");
}

// A CRLF split across chunks must not read as a blank line, which would end the frame early.
TEST_F(SseEventDecoderTest, CrlfSplitAcrossChunks) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: a\r"), IsOk());
  ASSERT_THAT(feed(decoder, "\ndata: b\r\n\r\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "a\nb");
}

TEST_F(SseEventDecoderTest, ByteWiseFeedMatchesSingleChunk) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feedByteWise(decoder, "event: delta\nid: 9\ndata: {\"k\":\"v\"}\n\ndata: [DONE]\n\n"),
              IsOk());

  ASSERT_EQ(events_.size(), 2);
  EXPECT_EQ(events_[0]->event(), "delta");
  EXPECT_EQ(events_[0]->id(), "9");
  ASSERT_TRUE(events_[0]->is_json());
  EXPECT_EQ(events_[0]->json().json()["k"], "v");
  EXPECT_FALSE(events_[1]->is_json());
  EXPECT_EQ(events_[1]->raw_data_as_string(), "[DONE]");
}

// A provider-specific field would vanish if it were merely ignored, because this path
// re-serializes rather than forwards bytes.
TEST_F(SseEventDecoderTest, UnknownFieldIsRetained) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "x-provider: whatever\ndata: x\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), "x-provider: whatever\n");
  EXPECT_EQ(events_[0]->raw_data_as_string(), "x");
}

TEST_F(SseEventDecoderTest, UnknownFieldValueSplitAcrossChunksIsReassembled) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feedByteWise(decoder, "x-trace: abc123\ndata: x\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), "x-trace: abc123\n");
}

// A line with no colon is a field with an empty value, and is stored without one:
// re-serializing it as `ab:` would mean the same thing, but this way the bytes are the sender's. A
// name short enough to still become a modeled field resolves through beginField() rather than
// streaming.
TEST_F(SseEventDecoderTest, UnknownFieldWithNoValueIsRetained) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "ab\ndata: x\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), "ab\n");
}

// Unmodeled fields go to a store that offloads, so no count bounds them: a frame may carry as many
// as the sender wrote.
TEST_F(SseEventDecoderTest, UnknownFieldsAreNotCapped) {
  SseEventDecoder decoder = makeDecoder();
  std::string input;
  std::string expected;
  for (int i = 0; i < 500; ++i) {
    absl::StrAppend(&input, "x-", i, ": v\n");
    absl::StrAppend(&expected, "x-", i, ": v\n");
  }
  ASSERT_THAT(feed(decoder, absl::StrCat(input, "\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), expected);
}

// The metadata budget bounds the fields that are copied into strings. An unmodeled field is not
// one of those, so it does not draw on it.
TEST_F(SseEventDecoderTest, UnknownFieldsDoNotCountAgainstTheMetadataBudget) {
  SseEventDecoder::Config config;
  config.max_metadata_line_bytes = 8;
  SseEventDecoder decoder = makeDecoder(config);

  ASSERT_THAT(feed(decoder, "aaaaa: 1\nbbbbb: 2\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), "aaaaa: 1\nbbbbb: 2\n");
}

TEST_F(SseEventDecoderTest, EndStreamEmitsUnterminatedFrame) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: tail"), IsOk());
  EXPECT_TRUE(events_.empty());

  ASSERT_THAT(decoder.onEndStream(events_), IsOk());
  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "tail");
  EXPECT_EQ(events_[0]->termination(), SseEvent::Termination::None);
  EXPECT_FALSE(events_[0]->blank_line_terminated());
}

TEST_F(SseEventDecoderTest, EndStreamWithNothingPendingEmitsNothing) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: x\n\n"), IsOk());
  ASSERT_THAT(decoder.onEndStream(events_), IsOk());

  EXPECT_EQ(events_.size(), 1);
}

TEST_F(SseEventDecoderTest, MetadataOverflowFailsTheStream) {
  SseEventDecoder::Config config;
  config.max_metadata_line_bytes = 8;
  SseEventDecoder decoder = makeDecoder(config);

  EXPECT_EQ(feed(decoder, "event: aaaaaaaaaaaaaaaaaaaa\n\n").code(),
            absl::StatusCode::kResourceExhausted);
}

TEST_F(SseEventDecoderTest, TooManyDataLinesFailsTheStream) {
  SseEventDecoder::Config config;
  config.max_data_lines = 2;
  SseEventDecoder decoder = makeDecoder(config);

  EXPECT_EQ(feed(decoder, "data: a\ndata: b\ndata: c\n\n").code(),
            absl::StatusCode::kResourceExhausted);
}

// A UTF-8 BOM is encoding, not framing: it has to come off before the first line is read, or the
// first field name carries three invisible bytes and stops being recognized.
TEST_F(SseEventDecoderTest, LeadingUtf8BomIsStripped) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, absl::StrCat(kUtf8Bom, "data: x\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "x");
  EXPECT_EQ(events_[0]->extras_store(), nullptr);
}

// The BOM is three bytes, so it can straddle a chunk boundary like any other terminator.
TEST_F(SseEventDecoderTest, Utf8BomSplitAcrossChunksIsStripped) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feedByteWise(decoder, absl::StrCat(kUtf8Bom, "data: x\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->raw_data_as_string(), "x");
  EXPECT_EQ(events_[0]->extras_store(), nullptr);
}

// A stream opening on a BOM prefix that turns out to be something else: the bytes withheld while
// the scanner was still deciding are content, and have to reappear as content.
TEST_F(SseEventDecoderTest, BytesThatOnlyStartLikeABomAreKeptAsContent) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, absl::StrCat(kUtf8Bom.substr(0, 2), "name: v\ndata: x\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), absl::StrCat(kUtf8Bom.substr(0, 2), "name: v\n"));
}

// The same, with the decision falling on a chunk boundary, so the withheld bytes are reported on a
// call that takes nothing from the chunk in hand.
TEST_F(SseEventDecoderTest, PartialBomResolvedOnALaterChunkIsKeptAsContent) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feedByteWise(decoder, absl::StrCat(kUtf8Bom.substr(0, 2), "name: v\ndata: x\n\n")),
              IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), absl::StrCat(kUtf8Bom.substr(0, 2), "name: v\n"));
}

// A name longer than any field this decoder models cannot become one, so the line resolves as
// unmodeled without waiting for a colon that may never come, and streams out rather than growing
// in memory. A colon-less line keeps arriving without one.
TEST_F(SseEventDecoderTest, OverlongFieldNameIsStreamedNotAccumulated) {
  const std::string name(200, 'n');
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, absl::StrCat(name, "\ndata: x\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), absl::StrCat(name, "\n"));
  EXPECT_EQ(events_[0]->raw_data_as_string(), "x");
}

// Whether a name is overlong must not depend on where the chunk boundary fell, so a name that
// arrives complete with its colon is handled like one that arrives a byte at a time.
TEST_F(SseEventDecoderTest, OverlongFieldNameWithAValueIsRetained) {
  const std::string name(200, 'n');
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, absl::StrCat(name, ": v\ndata: x\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), absl::StrCat(name, ": v\n"));
  EXPECT_EQ(events_[0]->raw_data_as_string(), "x");
}

TEST_F(SseEventDecoderTest, OverlongFieldNameSplitAcrossChunksIsRetained) {
  const std::string name(200, 'n');
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feedByteWise(decoder, absl::StrCat(name, ": v\ndata: x\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), absl::StrCat(name, ": v\n"));
}

// A colon-less `data` line is still a data line, so it counts against the cap like any other.
TEST_F(SseEventDecoderTest, ColonLessDataLineCountsAgainstTheCap) {
  SseEventDecoder::Config config;
  config.max_data_lines = 1;
  SseEventDecoder decoder = makeDecoder(config);

  EXPECT_EQ(feed(decoder, "data: a\ndata\n\n").code(), absl::StatusCode::kResourceExhausted);
}

// The same line resolved at end of stream rather than at a newline must fail the same way.
TEST_F(SseEventDecoderTest, TrailingLineResolvedAtEndStreamCanFail) {
  SseEventDecoder::Config config;
  config.max_data_lines = 1;
  SseEventDecoder decoder = makeDecoder(config);
  ASSERT_THAT(feed(decoder, "data: a\ndata"), IsOk());

  EXPECT_EQ(decoder.onEndStream(events_).code(), absl::StatusCode::kResourceExhausted);
}

// Exercises the paths that need somewhere to offload to: a frame that outgrows the in-memory tier,
// and re-serialization of the references such a frame leaves behind. Both sides run against a real
// BufferManager, so an offset the decoder records and the bytes the serializer replays for it have
// to agree -- which is the whole point of the per-frame store.
class SseCodecBufferTest : public SseEventDecoderTest {
protected:
  SseCodecBufferTest()
      : buffer_manager_(BufferManager::Config{}, factory_, bridge_),
        executor_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)) {}

  void drain() {
    for (int i = 0; i < 40; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // A frame offloads once it passes 64 payload bytes, and a string longer than 16 bytes is held by
  // reference rather than materialized. Both are far below any real frame so tests stay readable.
  static SseEventDecoder::Config spillConfig() {
    SseEventDecoder::Config config;
    config.max_in_memory_frame_bytes = 64;
    config.parser.inline_string_threshold_bytes = 16;
    return config;
  }

  // Runs the serializer to completion and returns the bytes it put on the wire.
  std::string serialize(SseEvent& event) {
    absl::Status result = absl::UnknownError("never completed");
    Coroutine::DetachedHandle handle = Coroutine::launch(
        SseEventSerializer::serialize(event, buffer_manager_), executor_,
        [&result](absl::Status status) { result = std::move(status); },
        Coroutine::StartMode::Inline);
    drain();
    EXPECT_THAT(result, IsOk());

    const std::string out = bridge_.injected_.toString();
    bridge_.injected_.drain(bridge_.injected_.length());
    return out;
  }

  // bridge_ and factory_ live in the base, so they are constructed before buffer_manager_ and
  // destroyed after it.
  BufferManager buffer_manager_;
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
};

// The load-bearing case: an oversized JSON frame keeps a complete DOM, with the long value held by
// reference, and re-serializes byte for byte.
TEST_F(SseCodecBufferTest, SpilledJsonFrameRoundTrips) {
  const std::string value(120, 'x');
  const std::string payload = absl::StrCat(R"({"k":")", value, R"("})");
  SseEventDecoder decoder = makeDecoder(spillConfig());
  ASSERT_THAT(feed(decoder, absl::StrCat("data: ", payload, "\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  ASSERT_TRUE(events_[0]->is_json());
  const nlohmann::json& node = events_[0]->json().json()["k"];
  ASSERT_TRUE(JsonWithExtBuf::isExternalRef(node)) << "frame did not spill";
  const absl::StatusOr<JsonWithExtBuf::ExternalRef> ref = JsonWithExtBuf::externalRef(node);
  ASSERT_THAT(ref.status(), IsOk());
  EXPECT_EQ(ref->length, value.size());

  EXPECT_EQ(serialize(*events_[0]), absl::StrCat("data: ", payload, "\n\n"));
}

// A string long enough to be held by reference, in a frame small enough to stay in memory: the
// reference resolves out of the store's in-memory tier, with no storage IO at all.
TEST_F(SseCodecBufferTest, InMemoryFrameCanStillHoldAStringByReference) {
  const std::string value(32, 'x');
  const std::string payload = absl::StrCat(R"({"k":")", value, R"("})");
  SseEventDecoder decoder = makeDecoder(spillConfig());
  ASSERT_THAT(feed(decoder, absl::StrCat("data: ", payload, "\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  ASSERT_TRUE(events_[0]->is_json());
  ASSERT_TRUE(JsonWithExtBuf::isExternalRef(events_[0]->json().json()["k"]));
  EXPECT_EQ(serialize(*events_[0]), absl::StrCat("data: ", payload, "\n\n"));
}

// The prefix accumulated before the cap was reached has to be written out ahead of the bytes that
// tripped it, or every offset recorded afterwards is short by that much.
TEST_F(SseCodecBufferTest, SpillFlushesTheInlinePrefixFirst) {
  const std::string value(120, 'y');
  const std::string payload = absl::StrCat(R"({"k":")", value, R"("})");
  SseEventDecoder decoder = makeDecoder(spillConfig());
  // The first chunk stays under the inline cap, so the spill happens with a prefix already
  // buffered; the second carries the frame past it.
  ASSERT_THAT(feed(decoder, absl::StrCat("data: ", payload.substr(0, 40))), IsOk());
  ASSERT_THAT(feed(decoder, absl::StrCat(payload.substr(40), "\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  ASSERT_TRUE(events_[0]->is_json());
  ASSERT_TRUE(JsonWithExtBuf::isExternalRef(events_[0]->json().json()["k"]));
  EXPECT_EQ(serialize(*events_[0]), absl::StrCat("data: ", payload, "\n\n"));
}

// A spilled payload that is not JSON cannot be pulled back into memory without undoing the bound
// that spilled it, so it survives as references and streams straight back out.
TEST_F(SseCodecBufferTest, SpilledNonJsonFrameRoundTripsByReference) {
  const std::string payload(120, 'z');
  SseEventDecoder decoder = makeDecoder(spillConfig());
  ASSERT_THAT(feed(decoder, absl::StrCat("data: ", payload, "\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  SseEvent& event = *events_[0];
  EXPECT_FALSE(event.is_json());
  EXPECT_TRUE(event.has_data());
  ASSERT_EQ(event.raw_data_ext_refs().size(), 1);
  EXPECT_EQ(event.raw_data_ext_refs()[0].length, payload.size());
  EXPECT_EQ(event.raw_data().length(), 0);

  EXPECT_EQ(serialize(event), absl::StrCat("data: ", payload, "\n\n"));
}

// One reference per data line, so a multi-line payload comes back as the same lines rather than
// one line with newlines inside it.
TEST_F(SseCodecBufferTest, SpilledMultiLineRawFrameKeepsItsLines) {
  const std::string first(60, 'a');
  const std::string second(60, 'b');
  SseEventDecoder decoder = makeDecoder(spillConfig());
  ASSERT_THAT(feed(decoder, absl::StrCat("data: ", first, "\ndata: ", second, "\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  ASSERT_EQ(events_[0]->raw_data_ext_refs().size(), 2);
  EXPECT_EQ(events_[0]->raw_data_ext_refs()[0].length, first.size());
  EXPECT_EQ(events_[0]->raw_data_ext_refs()[1].length, second.size());

  EXPECT_EQ(serialize(*events_[0]), absl::StrCat("data: ", first, "\ndata: ", second, "\n\n"));
}

// Metadata still rides in memory on a spilled frame, so it must survive alongside the references.
TEST_F(SseCodecBufferTest, SpilledFrameKeepsItsMetadata) {
  const std::string payload(120, 'q');
  SseEventDecoder decoder = makeDecoder(spillConfig());
  ASSERT_THAT(feed(decoder, absl::StrCat("event: delta\nid: 7\ndata: ", payload, "\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->event(), "delta");
  EXPECT_EQ(events_[0]->id(), "7");
  EXPECT_EQ(serialize(*events_[0]), absl::StrCat("event: delta\nid: 7\ndata: ", payload, "\n\n"));
}

TEST_F(SseCodecBufferTest, SerializesEveryMetadataField) {
  SseEvent event;
  EXPECT_THAT(event.set_event("delta"), IsOk());
  EXPECT_THAT(event.set_id("42"), IsOk());
  EXPECT_THAT(event.set_retry("1500"), IsOk());

  EXPECT_EQ(serialize(event), "event: delta\nid: 42\nretry: 1500\n\n");
}

// A keepalive carries no data line at all, which is what distinguishes it from a frame whose
// payload happens to be empty. Its comment is stored rather than modeled, so it comes back with
// the spacing it arrived with.
TEST_F(SseCodecBufferTest, SerializesCommentOnlyKeepaliveWithNoDataLine) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, ": keepalive\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_FALSE(events_[0]->has_data());
  EXPECT_EQ(serialize(*events_[0]), ": keepalive\n\n");
}

// A comment is emitted with the unmodeled fields, after the metadata this codec does model. SSE
// assigns no meaning to the order, and a client ignores comments outright.
TEST_F(SseCodecBufferTest, CommentIsReorderedAfterModeledMetadata) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, ": ping\nevent: delta\ndata: p\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(serialize(*events_[0]), "event: delta\n: ping\ndata: p\n\n");
}

// A raw payload holding newlines has to be split back into one data line each; leaving them inline
// would insert framing breaks the sender never wrote.
TEST_F(SseCodecBufferTest, SerializesMultiLineRawPayloadAsSeparateLines) {
  auto raw = std::make_unique<Buffer::OwnedImpl>();
  raw->add("first\nsecond");
  SseEvent event;
  event.set_raw_data(std::move(raw));

  EXPECT_EQ(serialize(event), "data: first\ndata: second\n\n");
}

// A filter may edit a non-JSON payload in place; what it leaves behind is what goes on the wire.
TEST_F(SseCodecBufferTest, MutatedRawPayloadIsWhatGetsWritten) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: [DONE]\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  events_[0]->raw_data().add("!");

  EXPECT_EQ(serialize(*events_[0]), "data: [DONE]!\n\n");
}

// `data` and `data:` name the same field with the same empty value, so the colon comes back even
// though it was never sent: modeled fields are re-rendered from what they parsed to, and only
// unmodeled ones keep the sender's bytes.
TEST_F(SseCodecBufferTest, FieldLineWithNoColonHasEmptyValue) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_TRUE(events_[0]->has_data());
  EXPECT_EQ(events_[0]->raw_data_as_string(), "");
  EXPECT_EQ(serialize(*events_[0]), "data:\n\n");
}

// An empty data field is a frame with data, not a frame without one; dropping the line would
// turn it into a bare event.
TEST_F(SseCodecBufferTest, EmptyDataLineRoundTrips) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data:\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_TRUE(events_[0]->has_data());
  EXPECT_EQ(serialize(*events_[0]), "data:\n\n");
}

// The point of retaining unknown fields: a provider-specific field that arrives on the wire has to
// leave on the wire. Note the reordering -- unknown fields follow the modeled metadata -- which is
// safe because SSE assigns no meaning to field order outside the `data:` lines.
TEST_F(SseCodecBufferTest, UnknownFieldSurvivesAReserialization) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "x-provider: v\nevent: delta\ndata: {\"a\":1}\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(serialize(*events_[0]), "event: delta\nx-provider: v\ndata: {\"a\":1}\n\n");
}

// Repeats are meaningful to whoever sent them, so they are re-emitted in arrival order rather than
// collapsed into one.
TEST_F(SseCodecBufferTest, RepeatedUnknownFieldsKeepArrivalOrder) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "x-a: 1\nx-b: 2\nx-a: 3\ndata: p\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(serialize(*events_[0]), "x-a: 1\nx-b: 2\nx-a: 3\ndata: p\n\n");
}

// A valueless field must not gain a space on the way out. It does not, for free: the line goes
// back out exactly as it came in.
TEST_F(SseCodecBufferTest, UnknownFieldWithNoValueSerializesWithoutASpace) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "x-flag:\ndata: p\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(serialize(*events_[0]), "x-flag:\ndata: p\n\n");
}

// Unmodeled field lines are the one part of a frame that is not re-rendered, so odd spacing
// survives a round trip instead of being normalized to `name: value`.
TEST_F(SseCodecBufferTest, UnknownFieldSpacingSurvivesAReserialization) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "x-a:no-space\nx-b:  two\ndata: p\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(serialize(*events_[0]), "x-a:no-space\nx-b:  two\ndata: p\n\n");
}

// An unmodeled field large enough to leave memory is replayed out of storage on the way back, the
// same as an oversized payload.
TEST_F(SseCodecBufferTest, SpilledUnknownFieldRoundTrips) {
  const std::string value(200, 'u');
  SseEventDecoder decoder = makeDecoder(spillConfig());
  ASSERT_THAT(feed(decoder, absl::StrCat("x-big: ", value, "\ndata: p\n\n")), IsOk());

  ASSERT_EQ(events_.size(), 1);
  ASSERT_NE(events_[0]->extras_store(), nullptr);
  EXPECT_EQ(events_[0]->extras_store()->inMemoryBytes(), nullptr) << "extras did not spill";

  EXPECT_EQ(serialize(*events_[0]), absl::StrCat("x-big: ", value, "\ndata: p\n\n"));
}

// Per the SSE specification, repeated event:, id:, or retry: fields within a single frame
// overwrite the previous value ("last occurrence wins") rather than concatenating.
TEST_F(SseEventDecoderTest, RepeatedEventIdRetryLinesLastOccurrenceWins) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(
      feed(decoder,
           "event: first\nevent: second\nid: 1\nid: 2\nretry: 100\nretry: 200\ndata: p\n\n"),
      IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->event(), "second");
  EXPECT_EQ(events_[0]->id(), "2");
  EXPECT_EQ(events_[0]->retry(), "200");
}

// If the stream ends after 1 or 2 bytes of a UTF-8 BOM prefix without completing
// the 3-byte BOM, onEndStream() flushes the withheld prefix as ordinary content rather than
// dropping it.
TEST_F(SseEventDecoderTest, PartialUtf8BomAtEofIsNotDropped) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "\xEF\xBB"), IsOk());
  ASSERT_THAT(decoder.onEndStream(events_), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(extras(*events_[0]), "\xEF\xBB\n");
}

// SseEvent metadata setters forbid CR, LF, NULL in id, and non-digits in retry (triggering
// IS_ENVOY_BUG and returning InvalidArgumentError without mutating the field), and serialization
// forbids CR in raw_data.
TEST_F(SseCodecBufferTest, ForbidsCrOrLfInMetadataSettersAndCrInRawData) {
  SseEvent event;
  EXPECT_ENVOY_BUG(
      EXPECT_EQ(event.set_event("bad\nevent").code(), absl::StatusCode::kInvalidArgument),
      "SSE event field must not contain CR or LF");
  EXPECT_TRUE(event.event().empty());

  EXPECT_ENVOY_BUG(EXPECT_EQ(event.set_id("bad\rid").code(), absl::StatusCode::kInvalidArgument),
                   "SSE id field must not contain CR, LF, or NULL");
  EXPECT_FALSE(event.id().has_value());

  constexpr absl::string_view kNullId("bad\0id", 6);
  EXPECT_ENVOY_BUG(EXPECT_EQ(event.set_id(kNullId).code(), absl::StatusCode::kInvalidArgument),
                   "SSE id field must not contain CR, LF, or NULL");
  EXPECT_FALSE(event.id().has_value());

  EXPECT_ENVOY_BUG(EXPECT_EQ(event.set_retry("100\r\n").code(), absl::StatusCode::kInvalidArgument),
                   "SSE retry field must consist of ASCII digits");
  EXPECT_FALSE(event.retry().has_value());

  EXPECT_ENVOY_BUG(EXPECT_EQ(event.set_retry("soon").code(), absl::StatusCode::kInvalidArgument),
                   "SSE retry field must consist of ASCII digits");
  EXPECT_FALSE(event.retry().has_value());

#if !defined(NDEBUG)
  auto raw = std::make_unique<Buffer::OwnedImpl>();
  raw->add("line1\r\nline2");
  event.set_raw_data(std::move(raw));
  EXPECT_DEBUG_DEATH(serialize(event), ".*");
#endif
}

// Empty metadata values (`id:`, `event:`, `retry:`) are preserved on passthrough serialization,
// and an explicit empty `id:` returns `""` (distinct from `std::nullopt` when absent) so filters
// know the frame resets the client's `lastEventId`.
TEST_F(SseCodecBufferTest, EmptyMetadataValuesRoundTripOnPassthrough) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "id:\nevent:\nretry:\ndata: p\n\n"), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->id(), "");
  EXPECT_EQ(events_[0]->event(), "");
  EXPECT_EQ(events_[0]->retry(), std::nullopt);
  EXPECT_EQ(serialize(*events_[0]), "id:\nevent:\nretry:\ndata: p\n\n");
}

// When duplicated metadata lines contain both valid and invalid entries, getters return the last
// valid value per the SSE specification, passthrough serialization preserves all raw lines in
// order, and calling setters normalizes the vector to single valid entries.
TEST_F(SseCodecBufferTest, RepeatedMetadataLastValidWinsOnReadAndPreservesRawOnPassthrough) {
  SseEventDecoder decoder = makeDecoder();
  static constexpr char kRawInput[] =
      "id: 1\nid: 2\nid: bad\0id\nretry: 100\nretry: 200\nretry: soon\ndata: p\n\n";
  constexpr absl::string_view kInput(kRawInput, sizeof(kRawInput) - 1);
  ASSERT_THAT(feed(decoder, kInput), IsOk());

  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->id(), "2");
  EXPECT_EQ(events_[0]->retry(), "200");
  EXPECT_EQ(serialize(*events_[0]), kInput);

  EXPECT_THAT(events_[0]->set_id("3"), IsOk());
  EXPECT_THAT(events_[0]->set_retry("300"), IsOk());
  EXPECT_EQ(serialize(*events_[0]), "id: 3\nretry: 300\ndata: p\n\n");
}

// Exceeding max_metadata_fields fails the stream with ResourceExhaustedError.
TEST_F(SseEventDecoderTest, FrameExceedingMaxMetadataFieldsFails) {
  SseEventDecoder::Config config;
  config.max_metadata_fields = 2;
  SseEventDecoder decoder = makeDecoder(config);

  const absl::Status status = feed(decoder, "id: 1\nid: 2\nid: 3\ndata: p\n\n");
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
}

// ExternalRef ranges in raw_data_ext_refs() assert non-null payload_store and are range-checked by
// ReplayAwaitable.
TEST_F(SseCodecBufferTest, RejectsOutOfBoundsExternalRefViaReplayAwaitable) {
#if !defined(NDEBUG)
  SseEvent null_store_event;
  null_store_event.set_raw_data_ext_refs({JsonWithExtBuf::ExternalRef{0, 10}});
  EXPECT_DEBUG_DEATH(serialize(null_store_event), ".*");
#endif

  SseEvent out_of_bounds_event;
  auto store = std::make_shared<BufferManager>(BufferManager::Config{}, factory_, bridge_);
  Buffer::OwnedImpl short_data("12345");
  store->onData(short_data);
  store->endStream();
  out_of_bounds_event.set_payload_store(std::move(store));
  out_of_bounds_event.set_raw_data_ext_refs({JsonWithExtBuf::ExternalRef{0, 10}});

  absl::Status result = absl::OkStatus();
  Coroutine::DetachedHandle handle = Coroutine::launch(
      SseEventSerializer::serialize(out_of_bounds_event, buffer_manager_), executor_,
      [&result](absl::Status status) { result = std::move(status); }, Coroutine::StartMode::Inline);
  drain();
  EXPECT_EQ(result.code(), absl::StatusCode::kInvalidArgument);
}

// Synchronous stream reset (detaching bridge and cancelling coroutine) inside bridge.injectData()
// during SseEventSerializer::serialize() must not cause stack UAF when SseEvent and its
// payload_store are destroyed on-stack.
TEST_F(SseCodecBufferTest, SynchronousStreamResetDuringSerializationDoesNotUAF) {
  const std::string value(120, 'x');
  const std::string payload = absl::StrCat(R"({"k":")", value, R"("})");
  SseEventDecoder decoder = makeDecoder(spillConfig());
  ASSERT_THAT(feed(decoder, absl::StrCat("data: ", payload, "\n\n")), IsOk());
  ASSERT_EQ(events_.size(), 1);

  std::optional<Coroutine::DetachedHandle> handle;
  // On the 2nd inject (the ExternalRef replay from event.payload_store()), simulate downstream
  // stream reset: detach the bridge, cancel the coroutine handle, and destroy events_[0].
  bridge_.on_inject_ = [this, &handle]() {
    if (bridge_.inject_calls_ == 2) {
      bridge_.detachFromFilterChain();
      if (handle.has_value()) {
        handle->cancel();
      }
      events_.clear();
    }
  };

  absl::Status result = absl::OkStatus();
  handle = Coroutine::launch(
      SseEventSerializer::serialize(*events_[0], buffer_manager_), executor_,
      [&result](absl::Status status) { result = std::move(status); }, Coroutine::StartMode::Inline);
  drain();
  EXPECT_TRUE(events_.empty());
}

// Exceeding max_frame_bytes fails the stream with ResourceExhaustedError.
TEST_F(SseEventDecoderTest, FrameExceedingMaxFrameBytesFails) {
  SseEventDecoder::Config config;
  config.max_frame_bytes = 32;
  SseEventDecoder decoder = makeDecoder(config);

  const absl::Status status = feed(decoder, absl::StrCat("data: ", std::string(40, 'a'), "\n\n"));
  EXPECT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
}

// A frame flushed at end-of-stream without a terminating blank line must not have a trailing blank
// line inserted during serialization, ensuring downstream clients discard the incomplete trailing
// frame just as they would when receiving the original stream.
TEST_F(SseCodecBufferTest, UnterminatedFrameAtEndStreamDoesNotInsertBlankLineOnSerialization) {
  SseEventDecoder decoder = makeDecoder();
  ASSERT_THAT(feed(decoder, "data: complete\n\nevent: partial\ndata: incomplete\n"), IsOk());
  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->termination(), SseEvent::Termination::BlankLine);
  EXPECT_EQ(serialize(*events_[0]), "data: complete\n\n");

  events_.clear();
  ASSERT_THAT(decoder.onEndStream(events_), IsOk());
  ASSERT_EQ(events_.size(), 1);
  EXPECT_EQ(events_[0]->termination(), SseEvent::Termination::LineBreak);
  EXPECT_EQ(serialize(*events_[0]), "event: partial\ndata: incomplete\n");
}

// Verifies that all three EOF termination states (BlankLine, LineBreak including PendingCrContent,
// and None) serialize without inserting spurious line breaks or blank lines, so a downstream client
// sees identical event completion and discard behavior as when the proxy is absent.
TEST_F(SseCodecBufferTest, EndStreamTerminationStatesPreserveWireEndingAndClientBehavior) {
  struct Case {
    absl::string_view input;
    SseEvent::Termination expected_termination;
    absl::string_view expected_serialized;
    bool client_dispatches_on_data;
  };
  const Case cases[] = {
      // Complete frames terminated by blank line (LF or CR at EOF).
      {"data: ok\n\n", SseEvent::Termination::BlankLine, "data: ok\n\n", true},
      {"data: ok\r\r", SseEvent::Termination::BlankLine, "data: ok\n\n", true},
      // Frames ending after a single line break (LF, CRLF, or trailing CR via PendingCrContent).
      {"data: line_lf\n", SseEvent::Termination::LineBreak, "data: line_lf\n", false},
      {"data: line_crlf\r\n", SseEvent::Termination::LineBreak, "data: line_crlf\n", false},
      {"data: line_cr\r", SseEvent::Termination::LineBreak, "data: line_cr\n", false},
      {": comment_cr\r", SseEvent::Termination::LineBreak, ": comment_cr\n", false},
      // Frames truncated mid-line without any line break (ScanState::LineContent).
      {"data: midline", SseEvent::Termination::None, "data: midline", false},
      {"event: midline", SseEvent::Termination::None, "event: midline", false},
      {": comment_midline", SseEvent::Termination::None, ": comment_midline", false},
      {"event: a\ndata: b", SseEvent::Termination::None, "event: a\ndata: b", false},
  };

  for (const Case& c : cases) {
    events_.clear();
    SseEventDecoder upstream_decoder = makeDecoder();
    ASSERT_THAT(feed(upstream_decoder, c.input), IsOk()) << c.input;
    ASSERT_THAT(upstream_decoder.onEndStream(events_), IsOk()) << c.input;
    ASSERT_EQ(events_.size(), 1) << c.input;
    EXPECT_EQ(events_[0]->termination(), c.expected_termination) << c.input;

    const std::string proxied = serialize(*events_[0]);
    EXPECT_EQ(proxied, c.expected_serialized) << c.input;

    // Feed the proxied wire bytes to a fresh downstream client decoder and verify that:
    // 1) An event is dispatched during onData iff it was terminated by a blank line.
    // 2) At EOF (onEndStream), the downstream client observes the exact same termination state.
    std::vector<SseEventPtr> client_events;
    SseEventDecoder client_decoder = makeDecoder();
    Buffer::OwnedImpl buf(proxied);
    ASSERT_THAT(client_decoder.onData(buf, client_events), IsOk()) << c.input;
    if (c.client_dispatches_on_data) {
      EXPECT_EQ(client_events.size(), 1) << c.input;
    } else {
      EXPECT_TRUE(client_events.empty()) << c.input;
    }
    ASSERT_THAT(client_decoder.onEndStream(client_events), IsOk()) << c.input;
    ASSERT_EQ(client_events.size(), 1) << c.input;
    EXPECT_EQ(client_events[0]->termination(), c.expected_termination) << c.input;
  }
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
