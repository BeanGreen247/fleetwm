#include "pipewire_json.hpp"

#include <gtest/gtest.h>

namespace fleetwm::common {
namespace {

TEST(ExtractJsonStringField, SimpleObject) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"alsa_output.xyz"})", "name"),
            "alsa_output.xyz");
}

TEST(ExtractJsonStringField, WithSpaceAfterColon) {
  EXPECT_EQ(extract_json_string_field(R"({"name": "alsa_output.xyz"})", "name"),
            "alsa_output.xyz");
}

TEST(ExtractJsonStringField, WithSpaceBeforeColon) {
  EXPECT_EQ(extract_json_string_field(R"({"name" : "alsa_output.xyz"})", "name"),
            "alsa_output.xyz");
}

TEST(ExtractJsonStringField, FieldNotFirst) {
  EXPECT_EQ(extract_json_string_field(R"({"other":1,"name":"sink1"})", "name"), "sink1");
}

TEST(ExtractJsonStringField, FieldNotLast) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"sink1","other":1})", "name"), "sink1");
}

TEST(ExtractJsonStringField, MultipleFieldsPicksRequestedOne) {
  EXPECT_EQ(extract_json_string_field(R"({"a":"1","name":"sink1","b":"2"})", "name"), "sink1");
  EXPECT_EQ(extract_json_string_field(R"({"a":"1","name":"sink1","b":"2"})", "a"), "1");
  EXPECT_EQ(extract_json_string_field(R"({"a":"1","name":"sink1","b":"2"})", "b"), "2");
}

TEST(ExtractJsonStringField, FieldMissingReturnsEmpty) {
  EXPECT_EQ(extract_json_string_field(R"({"other":"x"})", "name"), "");
}

TEST(ExtractJsonStringField, EmptyJsonReturnsEmpty) {
  EXPECT_EQ(extract_json_string_field("{}", "name"), "");
}

TEST(ExtractJsonStringField, EmptyStringReturnsEmpty) {
  EXPECT_EQ(extract_json_string_field("", "name"), "");
}

TEST(ExtractJsonStringField, EmptyStringValue) {
  EXPECT_EQ(extract_json_string_field(R"({"name":""})", "name"), "");
}

TEST(ExtractJsonStringField, ValueContainsSpaces) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"hello world"})", "name"), "hello world");
}

TEST(ExtractJsonStringField, ValueContainsColon) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"a:b:c"})", "name"), "a:b:c");
}

TEST(ExtractJsonStringField, ValueContainsBraces) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"{nested}"})", "name"), "{nested}");
}

TEST(ExtractJsonStringField, NumericFieldValueReturnsEmpty) {
  // The field's value isn't quoted at all (a bare number) -- the first
  // quote found after the colon belongs to some later field, if any, not
  // this one, so the extracted "value" is either empty or wrong; the
  // guaranteed contract is just that it doesn't crash. Here there's no
  // following quote at all, so the result must be empty.
  EXPECT_EQ(extract_json_string_field(R"({"count":42})", "count"), "");
}

TEST(ExtractJsonStringField, MissingClosingQuoteReturnsEmpty) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"unterminated)", "name"), "");
}

TEST(ExtractJsonStringField, MissingColonReturnsEmpty) {
  EXPECT_EQ(extract_json_string_field(R"({"name" "value"})", "name"), "");
}

TEST(ExtractJsonStringField, MissingOpeningQuoteAfterColonFindsNone) {
  EXPECT_EQ(extract_json_string_field(R"({"name":novalue})", "name"), "");
}

TEST(ExtractJsonStringField, FieldNameIsSubstringOfAnotherKeyNotMatched) {
  // "name" must not match inside "nodename" -- extract_json_string_field
  // searches for the exact quoted field name, not a bare substring.
  EXPECT_EQ(extract_json_string_field(R"({"nodename":"x"})", "name"), "");
}

TEST(ExtractJsonStringField, RequestedFieldIsLongerKeyThatContainsShorterOne) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"short","nodename":"long"})", "nodename"),
            "long");
}

TEST(ExtractJsonStringField, DuplicateKeyPicksFirstOccurrence) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"first","name":"second"})", "name"), "first");
}

TEST(ExtractJsonStringField, NotAnObjectAtAllReturnsEmpty) {
  EXPECT_EQ(extract_json_string_field("not json at all", "name"), "");
}

TEST(ExtractJsonStringField, WhitespaceOnlyReturnsEmpty) {
  EXPECT_EQ(extract_json_string_field("   ", "name"), "");
}

TEST(ExtractJsonStringField, RealDefaultAudioSinkPayload) {
  // Real-world shape of the "default.audio.sink" metadata value this
  // function was actually written for (see volume_source.cpp/
  // audio_mixer.cpp) -- PipeWire's metadata protocol nests the sink name
  // one level under "name" alongside other fields.
  std::string json = R"({"name":"alsa_output.pci-0000_00_1f.3.analog-stereo"})";
  EXPECT_EQ(extract_json_string_field(json, "name"),
            "alsa_output.pci-0000_00_1f.3.analog-stereo");
}

TEST(ExtractJsonStringField, FieldNameWithUnderscore) {
  EXPECT_EQ(extract_json_string_field(R"({"node_name":"sink1"})", "node_name"), "sink1");
}

TEST(ExtractJsonStringField, ValueWithEscapedBackslash) {
  // Not real JSON-escape-aware (see the header comment) -- a literal
  // backslash in the value is passed through verbatim rather than
  // unescaped, since this only ever parses PipeWire's own plain node
  // names, which never contain one for real.
  EXPECT_EQ(extract_json_string_field(R"({"name":"a\\b"})", "name"), R"(a\\b)");
}

TEST(ExtractJsonStringField, FieldNameEmptyStringNeverMatches) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"x"})", ""), "");
}

TEST(ExtractJsonStringField, ThreeFieldObjectMiddleFieldNumeric) {
  EXPECT_EQ(extract_json_string_field(R"({"a":"1","count":2,"b":"3"})", "b"), "3");
}

TEST(ExtractJsonStringField, ValueIsSingleCharacter) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"x"})", "name"), "x");
}

TEST(ExtractJsonStringField, KeyAppearsAsSubstringOfValueNotConfused) {
  // "name" appears inside the *value* of "other", not as a key -- must
  // not be mistaken for the "name" key itself.
  EXPECT_EQ(extract_json_string_field(R"({"other":"my name is x"})", "name"), "");
}

TEST(ExtractJsonStringField, MultipleSpacesAroundColon) {
  EXPECT_EQ(extract_json_string_field(R"({"name"   :   "sink1"})", "name"), "sink1");
}

TEST(ExtractJsonStringField, ValueWithForwardSlash) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"a/b/c"})", "name"), "a/b/c");
}

TEST(ExtractJsonStringField, LongRealisticNodeName) {
  std::string json =
      R"({"media.class":"Audio/Sink","node.name":"alsa_output.pci-0000_00_1f.3.analog-stereo",)"
      R"("node.description":"Built-in Audio Analog Stereo"})";
  EXPECT_EQ(extract_json_string_field(json, "node.name"),
            "alsa_output.pci-0000_00_1f.3.analog-stereo");
  EXPECT_EQ(extract_json_string_field(json, "node.description"), "Built-in Audio Analog Stereo");
  EXPECT_EQ(extract_json_string_field(json, "media.class"), "Audio/Sink");
}

TEST(ExtractJsonStringField, FirstFieldOfThreeIsRequested) {
  EXPECT_EQ(extract_json_string_field(R"({"first":"1","second":"2","third":"3"})", "first"), "1");
}

TEST(ExtractJsonStringField, LastFieldOfThreeIsRequested) {
  EXPECT_EQ(extract_json_string_field(R"({"first":"1","second":"2","third":"3"})", "third"), "3");
}

TEST(ExtractJsonStringField, ValueEqualToFieldName) {
  EXPECT_EQ(extract_json_string_field(R"({"name":"name"})", "name"), "name");
}

TEST(ExtractJsonStringField, NewlineInsideJsonStillParses) {
  EXPECT_EQ(extract_json_string_field("{\"name\":\n\"sink1\"}", "name"), "sink1");
}

TEST(ExtractJsonStringField, TabCharacterAroundColonStillParses) {
  EXPECT_EQ(extract_json_string_field("{\"name\":\t\"sink1\"}", "name"), "sink1");
}

TEST(ExtractJsonStringField, ValueWithNumbersOnlyIsStillAString) {
  EXPECT_EQ(extract_json_string_field(R"({"id":"12345"})", "id"), "12345");
}

TEST(ExtractJsonStringField, ObjectWithOnlyOneCharacterFieldName) {
  EXPECT_EQ(extract_json_string_field(R"({"a":"x"})", "a"), "x");
}

TEST(ExtractJsonStringField, FieldNameWithDotCharacter) {
  EXPECT_EQ(extract_json_string_field(R"({"application.name":"Firefox"})", "application.name"),
            "Firefox");
}

}  // namespace
}  // namespace fleetwm::common
