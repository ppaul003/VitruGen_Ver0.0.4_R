#ifndef VITRUGEN_JSON_VALUE_H
#define VITRUGEN_JSON_VALUE_H

#include <map>
#include <string>
#include <vector>

namespace vitru {

class JsonValue {
public:
	enum class Type { Null, Boolean, Number, String, Array, Object };
	using Array = std::vector<JsonValue>;
	using Object = std::map<std::string, JsonValue>;

	JsonValue() = default;
	explicit JsonValue(bool value);
	explicit JsonValue(double value);
	explicit JsonValue(const std::string& value);
	explicit JsonValue(const char* value);
	static JsonValue array();
	static JsonValue object();

	Type type() const { return m_type; }
	bool isNull() const { return m_type == Type::Null; }
	bool isBoolean() const { return m_type == Type::Boolean; }
	bool isNumber() const { return m_type == Type::Number; }
	bool isString() const { return m_type == Type::String; }
	bool isArray() const { return m_type == Type::Array; }
	bool isObject() const { return m_type == Type::Object; }
	bool boolean(bool fallback = false) const;
	double number(double fallback = 0.0) const;
	const std::string& string() const;
	const Array& arrayItems() const;
	Array& arrayItems();
	const Object& objectItems() const;
	Object& objectItems();
	const JsonValue* find(const std::string& key) const;
	JsonValue* find(const std::string& key);
	JsonValue& operator[](const std::string& key);
	void push(JsonValue value);

private:
	Type m_type = Type::Null;
	bool m_boolean = false;
	double m_number = 0.0;
	std::string m_string;
	Array m_array;
	Object m_object;
};

struct JsonParseResult {
	bool success = false;
	JsonValue value;
	std::string error;
	std::size_t errorOffset = 0;
};

JsonParseResult parseJson(const std::string& text);
std::string writeJson(const JsonValue& value, int indentSpaces = 2);

} // namespace vitru

#endif
