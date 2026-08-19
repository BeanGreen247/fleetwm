#pragma once

#include <string>

namespace fleetwm::common {

// Extracts the value of one top-level string field from a small flat JSON
// object, e.g. extract_json_string_field(R"({"name":"alsa_output.xyz"})",
// "name") -> "alsa_output.xyz". Not a general JSON parser -- every
// PipeWire metadata value this is used for (default.audio.sink, etc.) is
// always a small flat object with the field of interest holding a plain
// string, so a full JSON dependency isn't worth pulling in just for this.
// Returns an empty string if the field isn't present, isn't a string, or
// `json` isn't well-formed enough to find both quotes.
std::string extract_json_string_field(const std::string& json, const std::string& field);

}  // namespace fleetwm::common
