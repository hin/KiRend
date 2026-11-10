#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace kirend {

/// Decode standard base64. Whitespace is ignored. Returns nullopt on invalid input.
std::optional<std::vector<uint8_t>> DecodeBase64( std::string_view aText );

} // namespace kirend
