#include "common/buffer/buffer_impl.h"
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

class String {
	std::string value_;
	public:
	bool read(Buffer::Instance& data) {
		// First find the terminating zero.
		char zero = 0;
		auto pos = data.search(&zero, 1, 0);
		if (pos == -1 ) {
			return false;
		}

		// reserve that much in the string
		value_.resize(pos);
		// Now copy from buffer to string
		data.copyOut(0, pos, value_.data());
		data.drain(pos + 1);

		return true;
	}

	std::string to_string() const {
		return "[" + value_ +"]";
	}
};

template <typename T>
class Int {
  T value_;
	public:
  bool read(Buffer::Instance& data) {
		  value_ = data.drainBEInt<T>();
		  return true;
  }

  std::string to_string() const {
	  char buf[32];
		sprintf(buf, "[%02d]", value_);
		return std::string(buf);
  }
};

using Int32 = Int<uint32_t>;
using Int16 = Int<uint16_t>;
using Int8 = Int<uint8_t>;

class Byte1 {
	char value_;
	public:
	bool read(Buffer::Instance& data) {
		value_ = data.drainBEInt<uint8_t>();
		return true;
	}

	std::string to_string() const {
		char buf[4];
		sprintf(buf, "[%c]", value_);
		return std::string(buf);
	}
};

// This type is used as the last type ion the message and contains
// sequence of bytes.
class ByteN {
	std::vector<char> value_;
	public:
	bool read(Buffer::Instance& data) {
		value_.resize(data.length());
		data.copyOut(0, data.length(), value_.data());
		return true;
	}

	std::string to_string() const {
		std::string out = "[";
		out.reserve(value_.size() * 3);
		bool first = true;
		for (const auto& i : value_) {
		  char buf[4];
		  if (first) {
		  sprintf(buf, "%02d", i);
		  first = false;
		  } else {
		  sprintf(buf, " %02d", i);
		  }
		  out += buf;
		}
		out += "]";
		//return std::string(buf);
	return out;
		}
};

// Class represents the structure consiting of 4 bytes of length
// indicating how many bytes follow.
// In Postgres documentation it is described as:
// Int32
// The length of the function result value, in bytes (this count does not include itself). Can be zero. As a special case, -1 indicates a NULL function result. No value bytes follow in the NULL case.
//
// Byten
// The value of the function result, in the format indicated by the associated format code. n is the above length.
class VarByteN {
	std::vector<char> value_;
	public:
	bool read(Buffer::Instance& data) {
		int32_t len = data.drainBEInt<int32_t>();
		if (len == -1) {
			// -1 indicates that nothing follows the length
			len = 0;
		}
		value_.resize(len);
		data.copyOut(0, len, value_.data());
		return true;
	}

	std::string to_string() const {
		  char buf[16];
		  sprintf(buf, "[%zu]", value_.size());

		std::string out = buf;
	       out += "[";
		out.reserve(value_.size() * 3 + 16);
		bool first = true;
		for (const auto& i : value_) {
		  if (first) {
		  sprintf(buf, "%02d", i);
		  first = false;
		  } else {
		  sprintf(buf, " %02d", i);
		  }
		  out += buf;
		}
		return out;
	}
};

template <typename T>
class Array {
	std::vector<std::unique_ptr<T>> value_;
	public:
	bool read(Buffer::Instance& data) {
		// First read the 16 bits value which indicates how many
		// elements there are in the array.
		uint16_t num = data.drainBEInt<uint16_t>();
		if (num != 0) {
			//value_.resize(num);
			for (auto i = 0; i < num; i++) {
				//T item;
				auto item = std::make_unique<T>();
				item->read(data);
				value_.push_back(std::move(item));
			}
		}
		return true;
	}
	std::string to_string() const {
		std::string out;
		char buf[128];
		sprintf(buf, "[Array of %zu:{", value_.size());
		out = buf;

		for (const auto& i : value_) {
			out +="[";
			out += i->to_string();
			out +="]";
		}
		out += "}]";

		return out;
	}
};


// Interface to Postgres message class.
class MessageI {
	public:
		virtual ~MessageI() {}
		virtual void read(Buffer::Instance& data) = 0;
		virtual std::string to_string() const = 0;
};


template <typename... Types>
class FullMessage;

template <typename FirstField, typename... Remaining>
class FullMessage<FirstField, Remaining...> : public MessageI{
  FirstField first_;
FullMessage<Remaining...> remaining_;
	public:
	FullMessage() {}
  FullMessage(char type, uint32_t length) : remaining_(type, length) {}
  std::string to_string() const override {
	  return first_.to_string() + remaining_.to_string();
  }

  void read(Buffer::Instance& data) override {
	  first_.read(data);
	  if (data.length() > 0) {
		  remaining_.read(data);
	  }
  }

};

template <>
class FullMessage<> : public MessageI{
	char msg_type_;
	uint32_t length_;
	public:
	FullMessage() {}
	FullMessage<>(char type, uint32_t length) : msg_type_(type), length_(length) {}
 	char getType() const {return msg_type_;}
	uint32_t getLength() const {return length_;}
	std::string to_string() const override {
		//char buf[100];
		//sprintf(buf, "[%c][%d]", msg_type_, length_);
		return "";//std::string(buf);
	}
	void read(Buffer::Instance&) override {
	}

};

template<typename... Types>
std::unique_ptr<MessageI> createMsg(char type, uint32_t length)
{
	return std::make_unique<FullMessage<Types...>>(type, length);
}


} //namespace Envoy
} //namespace Extensions
} //namespace NetworkFilters
} //namespace PostgresProxy

