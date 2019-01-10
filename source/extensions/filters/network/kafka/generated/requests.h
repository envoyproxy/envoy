// DO NOT EDIT - THIS FILE WAS GENERATED
// clang-format off
#pragma once
#include "extensions/filters/network/kafka/kafka_request.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/* Represents 'partitions' element in OffsetCommitRequestV0 */
struct OffsetCommitRequestV0Partition {
	const int32_t partition_;
	const int64_t offset_;
	const NullableString metadata_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(partition_, dst);
		written += encoder.encode(offset_, dst);
		written += encoder.encode(metadata_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV0Partition& rhs) const {
		return
			partition_ == rhs.partition_ &&
			offset_ == rhs.offset_ &&
			metadata_ == rhs.metadata_;
	};

};

class OffsetCommitRequestV0PartitionDeserializer:
	public CompositeDeserializerWith3Delegates<
		OffsetCommitRequestV0Partition,
		Int32Deserializer,
		Int64Deserializer,
		NullableStringDeserializer
	>{};

/* Represents 'topics' element in OffsetCommitRequestV0 */
struct OffsetCommitRequestV0Topic {
	const std::string topic_;
	const NullableArray<OffsetCommitRequestV0Partition> partitions_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(topic_, dst);
		written += encoder.encode(partitions_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV0Topic& rhs) const {
		return
			topic_ == rhs.topic_ &&
			partitions_ == rhs.partitions_;
	};

};

class OffsetCommitRequestV0TopicDeserializer:
	public CompositeDeserializerWith2Delegates<
		OffsetCommitRequestV0Topic,
		StringDeserializer,
		ArrayDeserializer<OffsetCommitRequestV0Partition, OffsetCommitRequestV0PartitionDeserializer>
	>{};

class OffsetCommitRequestV0 : public Request {
public:
	OffsetCommitRequestV0(
		std::string group_id,
		NullableArray<OffsetCommitRequestV0Topic> topics
	):
		Request{8, 0},
		group_id_{group_id},
		topics_{topics}
	{};

	bool operator==(const OffsetCommitRequestV0& rhs) const {
		return request_header_ == rhs.request_header_ && group_id_ == rhs.group_id_ && topics_ == rhs.topics_;
	};

protected:
	size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
		size_t written{0};
		written += encoder.encode(group_id_, dst);
		written += encoder.encode(topics_, dst);
		return written;
	}

private:
	const std::string group_id_;
	const NullableArray<OffsetCommitRequestV0Topic> topics_;
};

class OffsetCommitRequestV0Deserializer:
	public CompositeDeserializerWith2Delegates<
		OffsetCommitRequestV0,
		StringDeserializer,
		ArrayDeserializer<OffsetCommitRequestV0Topic, OffsetCommitRequestV0TopicDeserializer>
	>{};

class OffsetCommitRequestV0Parser : public RequestParser<OffsetCommitRequestV0, OffsetCommitRequestV0Deserializer> {
public:
	OffsetCommitRequestV0Parser(RequestContextSharedPtr ctx) : RequestParser{ctx} {};
};

/* Represents 'partitions' element in OffsetCommitRequestV1 */
struct OffsetCommitRequestV1Partition {
	const int32_t partition_;
	const int64_t offset_;
	const int64_t timestamp_;
	const NullableString metadata_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(partition_, dst);
		written += encoder.encode(offset_, dst);
		written += encoder.encode(timestamp_, dst);
		written += encoder.encode(metadata_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV1Partition& rhs) const {
		return
			partition_ == rhs.partition_ &&
			offset_ == rhs.offset_ &&
			timestamp_ == rhs.timestamp_ &&
			metadata_ == rhs.metadata_;
	};

};

class OffsetCommitRequestV1PartitionDeserializer:
	public CompositeDeserializerWith4Delegates<
		OffsetCommitRequestV1Partition,
		Int32Deserializer,
		Int64Deserializer,
		Int64Deserializer,
		NullableStringDeserializer
	>{};

/* Represents 'topics' element in OffsetCommitRequestV1 */
struct OffsetCommitRequestV1Topic {
	const std::string topic_;
	const NullableArray<OffsetCommitRequestV1Partition> partitions_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(topic_, dst);
		written += encoder.encode(partitions_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV1Topic& rhs) const {
		return
			topic_ == rhs.topic_ &&
			partitions_ == rhs.partitions_;
	};

};

class OffsetCommitRequestV1TopicDeserializer:
	public CompositeDeserializerWith2Delegates<
		OffsetCommitRequestV1Topic,
		StringDeserializer,
		ArrayDeserializer<OffsetCommitRequestV1Partition, OffsetCommitRequestV1PartitionDeserializer>
	>{};

class OffsetCommitRequestV1 : public Request {
public:
	OffsetCommitRequestV1(
		std::string group_id,
		int32_t generation_id,
		std::string member_id,
		NullableArray<OffsetCommitRequestV1Topic> topics
	):
		Request{8, 1},
		group_id_{group_id},
		generation_id_{generation_id},
		member_id_{member_id},
		topics_{topics}
	{};

	bool operator==(const OffsetCommitRequestV1& rhs) const {
		return request_header_ == rhs.request_header_ && group_id_ == rhs.group_id_ && generation_id_ == rhs.generation_id_ && member_id_ == rhs.member_id_ && topics_ == rhs.topics_;
	};

protected:
	size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
		size_t written{0};
		written += encoder.encode(group_id_, dst);
		written += encoder.encode(generation_id_, dst);
		written += encoder.encode(member_id_, dst);
		written += encoder.encode(topics_, dst);
		return written;
	}

private:
	const std::string group_id_;
	const int32_t generation_id_;
	const std::string member_id_;
	const NullableArray<OffsetCommitRequestV1Topic> topics_;
};

class OffsetCommitRequestV1Deserializer:
	public CompositeDeserializerWith4Delegates<
		OffsetCommitRequestV1,
		StringDeserializer,
		Int32Deserializer,
		StringDeserializer,
		ArrayDeserializer<OffsetCommitRequestV1Topic, OffsetCommitRequestV1TopicDeserializer>
	>{};

class OffsetCommitRequestV1Parser : public RequestParser<OffsetCommitRequestV1, OffsetCommitRequestV1Deserializer> {
public:
	OffsetCommitRequestV1Parser(RequestContextSharedPtr ctx) : RequestParser{ctx} {};
};

/* Represents 'partitions' element in OffsetCommitRequestV2 */
struct OffsetCommitRequestV2Partition {
	const int32_t partition_;
	const int64_t offset_;
	const NullableString metadata_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(partition_, dst);
		written += encoder.encode(offset_, dst);
		written += encoder.encode(metadata_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV2Partition& rhs) const {
		return
			partition_ == rhs.partition_ &&
			offset_ == rhs.offset_ &&
			metadata_ == rhs.metadata_;
	};

};

class OffsetCommitRequestV2PartitionDeserializer:
	public CompositeDeserializerWith3Delegates<
		OffsetCommitRequestV2Partition,
		Int32Deserializer,
		Int64Deserializer,
		NullableStringDeserializer
	>{};

/* Represents 'topics' element in OffsetCommitRequestV2 */
struct OffsetCommitRequestV2Topic {
	const std::string topic_;
	const NullableArray<OffsetCommitRequestV2Partition> partitions_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(topic_, dst);
		written += encoder.encode(partitions_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV2Topic& rhs) const {
		return
			topic_ == rhs.topic_ &&
			partitions_ == rhs.partitions_;
	};

};

class OffsetCommitRequestV2TopicDeserializer:
	public CompositeDeserializerWith2Delegates<
		OffsetCommitRequestV2Topic,
		StringDeserializer,
		ArrayDeserializer<OffsetCommitRequestV2Partition, OffsetCommitRequestV2PartitionDeserializer>
	>{};

class OffsetCommitRequestV2 : public Request {
public:
	OffsetCommitRequestV2(
		std::string group_id,
		int32_t generation_id,
		std::string member_id,
		int64_t retention_time,
		NullableArray<OffsetCommitRequestV2Topic> topics
	):
		Request{8, 2},
		group_id_{group_id},
		generation_id_{generation_id},
		member_id_{member_id},
		retention_time_{retention_time},
		topics_{topics}
	{};

	bool operator==(const OffsetCommitRequestV2& rhs) const {
		return request_header_ == rhs.request_header_ && group_id_ == rhs.group_id_ && generation_id_ == rhs.generation_id_ && member_id_ == rhs.member_id_ && retention_time_ == rhs.retention_time_ && topics_ == rhs.topics_;
	};

protected:
	size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
		size_t written{0};
		written += encoder.encode(group_id_, dst);
		written += encoder.encode(generation_id_, dst);
		written += encoder.encode(member_id_, dst);
		written += encoder.encode(retention_time_, dst);
		written += encoder.encode(topics_, dst);
		return written;
	}

private:
	const std::string group_id_;
	const int32_t generation_id_;
	const std::string member_id_;
	const int64_t retention_time_;
	const NullableArray<OffsetCommitRequestV2Topic> topics_;
};

class OffsetCommitRequestV2Deserializer:
	public CompositeDeserializerWith5Delegates<
		OffsetCommitRequestV2,
		StringDeserializer,
		Int32Deserializer,
		StringDeserializer,
		Int64Deserializer,
		ArrayDeserializer<OffsetCommitRequestV2Topic, OffsetCommitRequestV2TopicDeserializer>
	>{};

class OffsetCommitRequestV2Parser : public RequestParser<OffsetCommitRequestV2, OffsetCommitRequestV2Deserializer> {
public:
	OffsetCommitRequestV2Parser(RequestContextSharedPtr ctx) : RequestParser{ctx} {};
};

/* Represents 'partitions' element in OffsetCommitRequestV3 */
struct OffsetCommitRequestV3Partition {
	const int32_t partition_;
	const int64_t offset_;
	const NullableString metadata_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(partition_, dst);
		written += encoder.encode(offset_, dst);
		written += encoder.encode(metadata_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV3Partition& rhs) const {
		return
			partition_ == rhs.partition_ &&
			offset_ == rhs.offset_ &&
			metadata_ == rhs.metadata_;
	};

};

class OffsetCommitRequestV3PartitionDeserializer:
	public CompositeDeserializerWith3Delegates<
		OffsetCommitRequestV3Partition,
		Int32Deserializer,
		Int64Deserializer,
		NullableStringDeserializer
	>{};

/* Represents 'topics' element in OffsetCommitRequestV3 */
struct OffsetCommitRequestV3Topic {
	const std::string topic_;
	const NullableArray<OffsetCommitRequestV3Partition> partitions_;

	size_t encode(Buffer::Instance& dst, EncodingContext& encoder) const {
		size_t written{0};
		written += encoder.encode(topic_, dst);
		written += encoder.encode(partitions_, dst);
		return written;
	}

	bool operator==(const OffsetCommitRequestV3Topic& rhs) const {
		return
			topic_ == rhs.topic_ &&
			partitions_ == rhs.partitions_;
	};

};

class OffsetCommitRequestV3TopicDeserializer:
	public CompositeDeserializerWith2Delegates<
		OffsetCommitRequestV3Topic,
		StringDeserializer,
		ArrayDeserializer<OffsetCommitRequestV3Partition, OffsetCommitRequestV3PartitionDeserializer>
	>{};

class OffsetCommitRequestV3 : public Request {
public:
	OffsetCommitRequestV3(
		std::string group_id,
		int32_t generation_id,
		std::string member_id,
		int64_t retention_time,
		NullableArray<OffsetCommitRequestV3Topic> topics
	):
		Request{8, 3},
		group_id_{group_id},
		generation_id_{generation_id},
		member_id_{member_id},
		retention_time_{retention_time},
		topics_{topics}
	{};

	bool operator==(const OffsetCommitRequestV3& rhs) const {
		return request_header_ == rhs.request_header_ && group_id_ == rhs.group_id_ && generation_id_ == rhs.generation_id_ && member_id_ == rhs.member_id_ && retention_time_ == rhs.retention_time_ && topics_ == rhs.topics_;
	};

protected:
	size_t encodeDetails(Buffer::Instance& dst, EncodingContext& encoder) const override {
		size_t written{0};
		written += encoder.encode(group_id_, dst);
		written += encoder.encode(generation_id_, dst);
		written += encoder.encode(member_id_, dst);
		written += encoder.encode(retention_time_, dst);
		written += encoder.encode(topics_, dst);
		return written;
	}

private:
	const std::string group_id_;
	const int32_t generation_id_;
	const std::string member_id_;
	const int64_t retention_time_;
	const NullableArray<OffsetCommitRequestV3Topic> topics_;
};

class OffsetCommitRequestV3Deserializer:
	public CompositeDeserializerWith5Delegates<
		OffsetCommitRequestV3,
		StringDeserializer,
		Int32Deserializer,
		StringDeserializer,
		Int64Deserializer,
		ArrayDeserializer<OffsetCommitRequestV3Topic, OffsetCommitRequestV3TopicDeserializer>
	>{};

class OffsetCommitRequestV3Parser : public RequestParser<OffsetCommitRequestV3, OffsetCommitRequestV3Deserializer> {
public:
	OffsetCommitRequestV3Parser(RequestContextSharedPtr ctx) : RequestParser{ctx} {};
};

}}}}
// clang-format on
