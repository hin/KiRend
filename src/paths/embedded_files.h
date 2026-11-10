#pragma once

#include "pcb/board.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace kirend {

class EmbeddedFileError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

/// Decode the data block of a KiCad embedded file (base64 of zstd-compressed content).
std::vector<uint8_t> DecodeEmbeddedFile( const pcb::EmbeddedFile& aFile );

} // namespace kirend
