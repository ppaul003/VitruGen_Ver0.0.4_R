#include "JsonValue.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace vitru {

JsonValue::JsonValue(bool value) : m_type(Type::Boolean), m_boolean(value) {}
JsonValue::JsonValue(double value) : m_type(Type::Number), m_number(value) {}
JsonValue::JsonValue(const std::string& value) : m_type(Type::String), m_string(value) {}
JsonValue::JsonValue(const char* value) : m_type(Type::String), m_string(value ? value : "") {}
JsonValue JsonValue::array() { JsonValue value; value.m_type = Type::Array; return value; }
JsonValue JsonValue::object() { JsonValue value; value.m_type = Type::Object; return value; }
bool JsonValue::boolean(bool fallback) const { return isBoolean() ? m_boolean : fallback; }
double JsonValue::number(double fallback) const { return isNumber() ? m_number : fallback; }
const std::string& JsonValue::string() const { static const std::string empty; return isString() ? m_string : empty; }
const JsonValue::Array& JsonValue::arrayItems() const { static const Array empty; return isArray() ? m_array : empty; }
JsonValue::Array& JsonValue::arrayItems() { if (!isArray()) { *this = array(); } return m_array; }
const JsonValue::Object& JsonValue::objectItems() const { static const Object empty; return isObject() ? m_object : empty; }
JsonValue::Object& JsonValue::objectItems() { if (!isObject()) { *this = object(); } return m_object; }
const JsonValue* JsonValue::find(const std::string& key) const {
	if (!isObject()) return nullptr;
	const auto found = m_object.find(key); return found == m_object.end() ? nullptr : &found->second;
}
JsonValue* JsonValue::find(const std::string& key) {
	if (!isObject()) return nullptr;
	const auto found = m_object.find(key); return found == m_object.end() ? nullptr : &found->second;
}
JsonValue& JsonValue::operator[](const std::string& key) { return objectItems()[key]; }
void JsonValue::push(JsonValue value) { arrayItems().push_back(std::move(value)); }

namespace {

class Parser {
public:
	explicit Parser(const std::string& text) : m_text(text) {}
	JsonParseResult run() {
		JsonParseResult result;
		skipSpace();
		if (!parseValue(result.value, 0)) return failure();
		skipSpace();
		if (m_pos != m_text.size()) { fail("Unexpected data after JSON value."); return failure(); }
		result.success = true;
		return result;
	}

private:
	JsonParseResult failure() const {
		JsonParseResult result; result.error = m_error; result.errorOffset = m_errorPos; return result;
	}
	void fail(const char* message) { if (m_error.empty()) { m_error = message; m_errorPos = m_pos; } }
	void skipSpace() { while (m_pos < m_text.size() && (m_text[m_pos] == ' ' || m_text[m_pos] == '\t' || m_text[m_pos] == '\r' || m_text[m_pos] == '\n')) ++m_pos; }
	bool consume(char value) { if (m_pos < m_text.size() && m_text[m_pos] == value) { ++m_pos; return true; } return false; }
	bool keyword(const char* word) {
		const std::size_t start = m_pos;
		while (*word && m_pos < m_text.size() && m_text[m_pos] == *word) { ++m_pos; ++word; }
		if (*word == '\0') return true; m_pos = start; return false;
	}
	bool parseValue(JsonValue& output, unsigned depth) {
		if (depth > 256u) { fail("JSON nesting limit exceeded."); return false; }
		skipSpace();
		if (m_pos >= m_text.size()) { fail("Unexpected end of JSON input."); return false; }
		const char ch = m_text[m_pos];
		if (ch == '{') return parseObject(output, depth + 1u);
		if (ch == '[') return parseArray(output, depth + 1u);
		if (ch == '"') { std::string value; if (!parseString(value)) return false; output = JsonValue(value); return true; }
		if (ch == 't' && keyword("true")) { output = JsonValue(true); return true; }
		if (ch == 'f' && keyword("false")) { output = JsonValue(false); return true; }
		if (ch == 'n' && keyword("null")) { output = JsonValue(); return true; }
		if (ch == '-' || (ch >= '0' && ch <= '9')) return parseNumber(output);
		fail("Invalid JSON value."); return false;
	}
	bool parseObject(JsonValue& output, unsigned depth) {
		consume('{'); output = JsonValue::object(); skipSpace();
		if (consume('}')) return true;
		for (;;) {
			skipSpace(); std::string key;
			if (!parseString(key)) return false;
			skipSpace(); if (!consume(':')) { fail("Expected ':' after object key."); return false; }
			JsonValue value; if (!parseValue(value, depth)) return false;
			output.objectItems()[key] = std::move(value);
			skipSpace(); if (consume('}')) return true;
			if (!consume(',')) { fail("Expected ',' or '}' in object."); return false; }
		}
	}
	bool parseArray(JsonValue& output, unsigned depth) {
		consume('['); output = JsonValue::array(); skipSpace();
		if (consume(']')) return true;
		for (;;) {
			JsonValue value; if (!parseValue(value, depth)) return false;
			output.push(std::move(value)); skipSpace();
			if (consume(']')) return true;
			if (!consume(',')) { fail("Expected ',' or ']' in array."); return false; }
		}
	}
	static void appendUtf8(std::string& output, unsigned codepoint) {
		if (codepoint <= 0x7fu) output.push_back(static_cast<char>(codepoint));
		else if (codepoint <= 0x7ffu) { output.push_back(static_cast<char>(0xc0u | (codepoint >> 6))); output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu))); }
		else if (codepoint <= 0xffffu) { output.push_back(static_cast<char>(0xe0u | (codepoint >> 12))); output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu))); output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu))); }
		else { output.push_back(static_cast<char>(0xf0u | (codepoint >> 18))); output.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu))); output.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu))); output.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu))); }
	}
	bool hex4(unsigned& value) {
		value = 0;
		for (int i = 0; i < 4; ++i) {
			if (m_pos >= m_text.size()) { fail("Truncated Unicode escape."); return false; }
			const char c = m_text[m_pos++]; unsigned digit = 0;
			if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
			else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
			else { fail("Invalid Unicode escape."); return false; }
			value = (value << 4) | digit;
		}
		return true;
	}
	bool parseString(std::string& output) {
		if (!consume('"')) { fail("Expected JSON string."); return false; }
		output.clear();
		while (m_pos < m_text.size()) {
			const unsigned char c = static_cast<unsigned char>(m_text[m_pos++]);
			if (c == '"') return true;
			if (c < 0x20u) { fail("Control character in JSON string."); return false; }
			if (c != '\\') { output.push_back(static_cast<char>(c)); continue; }
			if (m_pos >= m_text.size()) { fail("Truncated JSON escape."); return false; }
			const char escaped = m_text[m_pos++];
			switch (escaped) {
			case '"': output.push_back('"'); break; case '\\': output.push_back('\\'); break;
			case '/': output.push_back('/'); break; case 'b': output.push_back('\b'); break;
			case 'f': output.push_back('\f'); break; case 'n': output.push_back('\n'); break;
			case 'r': output.push_back('\r'); break; case 't': output.push_back('\t'); break;
			case 'u': {
				unsigned codepoint = 0; if (!hex4(codepoint)) return false;
				if (codepoint >= 0xd800u && codepoint <= 0xdbffu) {
					if (m_pos + 2u > m_text.size() || m_text[m_pos] != '\\' || m_text[m_pos + 1u] != 'u') { fail("Missing low Unicode surrogate."); return false; }
					m_pos += 2u; unsigned low = 0; if (!hex4(low) || low < 0xdc00u || low > 0xdfffu) { fail("Invalid low Unicode surrogate."); return false; }
					codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) + (low - 0xdc00u);
				}
				else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu) { fail("Unexpected low Unicode surrogate."); return false; }
				appendUtf8(output, codepoint); break;
			}
			default: fail("Invalid JSON escape."); return false;
			}
		}
		fail("Unterminated JSON string."); return false;
	}
	bool parseNumber(JsonValue& output) {
		const std::size_t start = m_pos;
		consume('-');
		if (consume('0')) { if (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') { fail("Leading zero in JSON number."); return false; } }
		else { if (m_pos >= m_text.size() || m_text[m_pos] < '1' || m_text[m_pos] > '9') { fail("Invalid JSON number."); return false; } while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos; }
		if (consume('.')) { if (m_pos >= m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') { fail("Invalid JSON fraction."); return false; } while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos; }
		if (m_pos < m_text.size() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E')) { ++m_pos; if (m_pos < m_text.size() && (m_text[m_pos] == '+' || m_text[m_pos] == '-')) ++m_pos; if (m_pos >= m_text.size() || m_text[m_pos] < '0' || m_text[m_pos] > '9') { fail("Invalid JSON exponent."); return false; } while (m_pos < m_text.size() && m_text[m_pos] >= '0' && m_text[m_pos] <= '9') ++m_pos; }
		const std::string token = m_text.substr(start, m_pos - start); char* end = nullptr;
		const double number = std::strtod(token.c_str(), &end);
		if (!end || *end != '\0' || !std::isfinite(number)) { fail("JSON number is not finite."); return false; }
		output = JsonValue(number); return true;
	}

	const std::string& m_text;
	std::size_t m_pos = 0;
	std::size_t m_errorPos = 0;
	std::string m_error;
};

void escapeString(std::ostringstream& output, const std::string& value) {
	output << '"';
	for (unsigned char c : value) {
		switch (c) {
		case '"': output << "\\\""; break; case '\\': output << "\\\\"; break;
		case '\b': output << "\\b"; break; case '\f': output << "\\f"; break;
		case '\n': output << "\\n"; break; case '\r': output << "\\r"; break;
		case '\t': output << "\\t"; break;
		default:
			if (c < 0x20u) output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec << std::setfill(' ');
			else output << static_cast<char>(c);
		}
	}
	output << '"';
}

void writeValue(std::ostringstream& output, const JsonValue& value, int indent, int depth) {
	const std::string pad(static_cast<std::size_t>(depth * indent), ' ');
	const std::string childPad(static_cast<std::size_t>((depth + 1) * indent), ' ');
	switch (value.type()) {
	case JsonValue::Type::Null: output << "null"; break;
	case JsonValue::Type::Boolean: output << (value.boolean() ? "true" : "false"); break;
	case JsonValue::Type::Number: output << std::setprecision(15) << value.number(); break;
	case JsonValue::Type::String: escapeString(output, value.string()); break;
	case JsonValue::Type::Array: {
		const auto& items = value.arrayItems(); output << '[';
		for (std::size_t i = 0; i < items.size(); ++i) { if (indent > 0) output << '\n' << childPad; else if (i) output << ' '; writeValue(output, items[i], indent, depth + 1); if (i + 1u < items.size()) output << ','; }
		if (!items.empty() && indent > 0) output << '\n' << pad; output << ']'; break;
	}
	case JsonValue::Type::Object: {
		const auto& items = value.objectItems(); output << '{'; std::size_t index = 0;
		for (const auto& item : items) { if (indent > 0) output << '\n' << childPad; else if (index) output << ' '; escapeString(output, item.first); output << (indent > 0 ? ": " : ":"); writeValue(output, item.second, indent, depth + 1); if (++index < items.size()) output << ','; }
		if (!items.empty() && indent > 0) output << '\n' << pad; output << '}'; break;
	}
	}
}

} // namespace

JsonParseResult parseJson(const std::string& text) { return Parser(text).run(); }
std::string writeJson(const JsonValue& value, int indentSpaces) {
	std::ostringstream output; writeValue(output, value, (std::max)(0, indentSpaces), 0); output << '\n'; return output.str();
}

} // namespace vitru
