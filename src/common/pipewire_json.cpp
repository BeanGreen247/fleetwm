#include "pipewire_json.hpp"

namespace fleetwm::common {

std::string extract_json_string_field(const std::string& json, const std::string& field) {
  std::string quoted_field = "\"" + field + "\"";
  size_t field_pos = json.find(quoted_field);
  if (field_pos == std::string::npos) {
    return "";
  }
  size_t colon = json.find(':', field_pos + quoted_field.size());
  if (colon == std::string::npos) {
    return "";
  }
  size_t first_quote = json.find('"', colon + 1);
  if (first_quote == std::string::npos) {
    return "";
  }
  size_t second_quote = json.find('"', first_quote + 1);
  if (second_quote == std::string::npos) {
    return "";
  }
  return json.substr(first_quote + 1, second_quote - first_quote - 1);
}

}  // namespace fleetwm::common
