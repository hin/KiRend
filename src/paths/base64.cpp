#include "paths/base64.h"

#include <array>

namespace kirend {

std::optional<std::vector<uint8_t>> DecodeBase64( std::string_view aText )
{
    static const std::array<int8_t, 256> table = []
    {
        std::array<int8_t, 256> t{};
        t.fill( -1 );
        const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        for( int i = 0; i < 64; ++i )
            t[static_cast<uint8_t>( chars[i] )] = static_cast<int8_t>( i );

        return t;
    }();

    std::vector<uint8_t> out;
    out.reserve( aText.size() * 3 / 4 );

    uint32_t buffer = 0;
    int      bits = 0;
    bool     padding = false;

    for( char c : aText )
    {
        if( c == ' ' || c == '\n' || c == '\r' || c == '\t' )
            continue;

        if( c == '=' )
        {
            padding = true;
            continue;
        }

        if( padding )
            return std::nullopt; // data after padding

        const int8_t value = table[static_cast<uint8_t>( c )];

        if( value < 0 )
            return std::nullopt;

        buffer = ( buffer << 6 ) | static_cast<uint32_t>( value );
        bits += 6;

        if( bits >= 8 )
        {
            bits -= 8;
            out.push_back( static_cast<uint8_t>( ( buffer >> bits ) & 0xFF ) );
        }
    }

    return out;
}

} // namespace kirend
