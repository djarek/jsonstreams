#include <type_traits>
#include <sstream>
#include <vector>
#include <array>
#include <memory>

#include "../ext/jsmn.h"
namespace JSON {
using String = std::string;

template <typename Container>
struct IsContainer : std::false_type { };

//template <typename... Ts> struct IsContainer<std::list<Ts...>> : std::true_type { };
template <typename... Ts> struct IsContainer<std::vector<Ts...>> : std::true_type { };

template<typename InputType, typename ReturnType>
using EnableIfFundamental = typename std::enable_if<std::is_fundamental<typename std::remove_reference<InputType>::type>::value, ReturnType>::type;

template<typename InputType, typename ReturnType>
using EnableIfContainer = typename std::enable_if<IsContainer<typename std::remove_reference<InputType>::type>::value, ReturnType>::type;

template<typename PointerType>
using EnableIfSharedPointer = typename std::enable_if<std::is_same<PointerType, std::shared_ptr<decltype(*PointerType{})>>::value, PointerType>::type;


//template<typename InputType, typename

template <typename Type>
void prepareStream(std::stringstream&, const Type&) {
}

template <>
void prepareStream<bool>(std::stringstream& stream, const bool&) {
	stream << std::boolalpha;
}

template <typename Type>
void prepareIStream(std::stringstream&, const Type&) {
}

template <>
void prepareIStream<bool>(std::stringstream& stream, const bool&) {
	auto nextCharacter = stream.peek();
	if (stream.good() and nextCharacter != '1' and nextCharacter != '0') {
		stream >> std::boolalpha;
	} else {
		stream >> std::noboolalpha;
	}
}

template <typename PointerType>
EnableIfSharedPointer<PointerType> make(PointerType& /*in*/)
{
	using PointeeRef = decltype(*PointerType{});
	using Pointee = typename std::remove_reference<PointeeRef>::type;
	return std::make_shared<Pointee>({});
}


template <typename PointerType>
PointerType make(PointerType& /*in*/)
{
	using PointeeRef = decltype(*PointerType{});
	using Pointee = typename std::remove_reference<PointeeRef>::type;
	return PointerType{new Pointee{}};
}

struct NonCopyable
{
	NonCopyable() = default;
	NonCopyable(const NonCopyable&) = delete;
	NonCopyable(NonCopyable&&) = delete;
};

struct NonMovable
{
	NonMovable() = default;
	NonMovable& operator =(const NonMovable&) = delete;
	NonMovable& operator =(NonMovable&&) = delete;
};

class InputStream
{
public:
	enum ParseError
	{
		SUCCESS = 0,
		MALFORMED_JSON,
		INVALID_TOKEN_TYPE,
		INVALID_VALUE
	};
private:
	using TokenVector = std::vector<jsmntok_t>;
	std::stringstream data;
	TokenVector tokens{};
	TokenVector::iterator currentToken{tokens.end()};
	ParseError lastError{};
public:
	explicit InputStream(const String& input) : data{input}
	{
		jsmn_parser parser;
		jsmn_init(&parser);
		auto tokenCount = jsmn_parse(&parser, input.c_str(), input.size(), nullptr, 0);
		if (tokenCount > 0) {
			tokens.resize(tokenCount);
			jsmn_init(&parser);
			jsmn_parse(&parser, input.c_str(), input.size(), tokens.data(), tokens.size());
			currentToken = tokens.begin();
		} else {
			lastError = MALFORMED_JSON;
		}
	}

	const jsmntype_t& peekToken() const {
		return currentToken->type;
	}

	bool skipToken() {
		++currentToken;
		return currentToken != tokens.end();
	}

	template <typename InputType>
	auto operator >> (InputType& in) -> decltype(*in, in = nullptr, *this)& {
		if (not good()) {
			return *this;
		}

		if (currentToken->type == JSMN_PRIMITIVE) {
			std::array<char, 4> buffer{};
			data.seekg(currentToken->start, data.beg);
			data.readsome(buffer.begin(), buffer.size());
			if (not data.good()) {
				lastError = INVALID_VALUE;
				return *this;
			}

			constexpr static std::array<char, 4> NULL_STRING {'n', 'u', 'l', 'l'};
			if (buffer == NULL_STRING) {
				in = nullptr;
				++currentToken;
			} else {
				data.seekg(currentToken->start, data.beg);
				in = JSON::make(in); // explicitly take the make from JSON namespace to prevent collisions
				*this >> *in;
			}
		} else {
			in = JSON::make(in);
			*this >> *in;
		}
		return *this;
	}

	template <typename InputType>
	EnableIfFundamental<InputType, InputStream>& operator >>(InputType& in) {
		if (not good()) {
			return *this;
		}
		if (currentToken->type != JSMN_PRIMITIVE) {
			lastError = INVALID_TOKEN_TYPE;
			return *this;
		}
		data.seekg(currentToken->start, data.beg);
		prepareIStream(data, in);
		data >> in;
		if (data.fail() or data.bad()) {
			lastError = INVALID_VALUE;
			return *this;
		}
		currentToken++;
		return *this;
	}

	InputStream& operator >>(String& in) {
		if (not good()) {
			return *this;
		}
		if (currentToken->type != JSMN_STRING) {
			lastError = INVALID_TOKEN_TYPE;
			return *this;
		}
		data.seekg(currentToken->start, data.beg);
		auto size = currentToken->end - currentToken->start;
		in.resize(size);
		data.readsome(&in[0], size);
		if (data.fail() or data.bad()) {
			lastError = INVALID_VALUE;
			return *this;
		}
		currentToken++;
		return *this;
	}

	template<typename Container>
	auto reserve(Container& container, size_t n) -> decltype(container.reserve(n))
	{
		container.reserve(n);
	}

	template<typename InputType>
	EnableIfContainer<InputType, InputStream>& operator >> (InputType& in) {
		if (not good()) {
			return *this;
		}
		if (currentToken->type != JSMN_ARRAY) {
			lastError = INVALID_TOKEN_TYPE;
			return *this;
		}
		reserve(in, currentToken->size);
		auto size = currentToken->size;
		skipToken();
		for (int i = 0; i < size; ++i) {
			if (not good()) {
				return *this;
			}
			in.push_back({});
			*this >> in.back();
			if (fail() or bad()) {
				in.pop_back();
			}
		}
		return *this;
	}

	bool eof() const {
		return currentToken >= tokens.end();
	}
	bool bad() const {
		return lastError == MALFORMED_JSON or lastError == INVALID_TOKEN_TYPE;
	}
	bool fail() const {
		return lastError == MALFORMED_JSON or lastError == INVALID_VALUE;
	}

	bool good() const {
		return currentToken < tokens.end() and lastError == SUCCESS;
	}

	bool expectToken(jsmntype_t type) {
		if (not good()) {
			return false;
		}
		if (currentToken->type != type) {
			lastError = INVALID_TOKEN_TYPE;
			return false;
		}
		return true;
	}
	int childrenCount() const {
		return currentToken->size;
	}
};

class OutputStream
{
	std::stringstream stream{};

	template<typename FundamentalType>
	EnableIfFundamental<FundamentalType, OutputStream&>& operator <<(const FundamentalType& out) {
		prepareStream(stream, out);
		stream << out;
		return *this;
	}

	OutputStream& operator <<(const String& out) {
		stream << "\"" << out << "\"";
		return *this;
	}
public:

	template<typename ContainerType>
	EnableIfContainer<ContainerType, OutputStream>& operator <<(const ContainerType& container) {
		stream << "[";
		for (const auto& out : container) {
			*this << out;
			stream << ",";
		}
		if (not container.empty()) {
			stream.seekp(-1, stream.end);
		}
		stream << "]";
		return *this;
	}

	String str() const {
		return stream.str();
	}

	template<typename OutputType>
	struct Pair
	{
		const String key;
		const OutputType& value;
	};

	class ObjectSentry : private NonCopyable, private NonMovable
	{
		OutputStream& jsonStream;
		bool filled{};
	public:
		ObjectSentry(OutputStream& stream) : jsonStream{stream} {
			stream.stream << "{";
		}
		template<typename OutputType>
		ObjectSentry& operator <<(Pair<OutputType> out) {
			jsonStream << out.key;
			jsonStream.stream << ":";
			jsonStream << out.value;
			jsonStream.stream << ",";
			filled = true;
			return *this;
		}
		~ObjectSentry() {
			if (filled) {
				jsonStream.stream.seekp(-1, jsonStream.stream.end); //overwrite trailing comma
			}
			jsonStream.stream << "}";
		}
	};

	class ArraySentry : private NonCopyable, private NonMovable
	{
		OutputStream& jsonStream;
		bool filled{};
	public:
		ArraySentry(OutputStream& stream) : jsonStream{stream} {
			stream.stream << "[";
		}

		template<typename OutputType>
		ArraySentry& operator <<(const OutputType& out) {
			jsonStream << out;
			jsonStream.stream << ",";
			filled = true;
			return *this;
		}
		~ArraySentry() {
			if (filled) {
				jsonStream.stream.seekp(-1, jsonStream.stream.end); //overwrite trailing comma
			}
			jsonStream.stream << "]";
		}
	};

	friend std::ostream& operator <<(std::ostream& ostream, const OutputStream& jsonStream){
		ostream << jsonStream.stream.rdbuf();
		return ostream;
	}
};

template<typename ValueType>
OutputStream::Pair<ValueType> makePair(String key, const ValueType& value) {
	return {move(key), value};
}
}


