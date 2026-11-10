#include "output/png_writer.h"

#include <lunasvg.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace kirend::output {

namespace {

uint32_t crc32( const uint8_t* aData, size_t aLength, uint32_t aCrc = 0 )
{
    static const std::array<uint32_t, 256> table = []
    {
        std::array<uint32_t, 256> t{};

        for( uint32_t n = 0; n < 256; ++n )
        {
            uint32_t c = n;

            for( int k = 0; k < 8; ++k )
                c = ( c & 1 ) ? 0xEDB88320u ^ ( c >> 1 ) : c >> 1;

            t[n] = c;
        }

        return t;
    }();

    uint32_t c = aCrc ^ 0xFFFFFFFFu;

    for( size_t i = 0; i < aLength; ++i )
        c = table[( c ^ aData[i] ) & 0xFF] ^ ( c >> 8 );

    return c ^ 0xFFFFFFFFu;
}


void putU32( std::vector<uint8_t>& aOut, uint32_t aValue )
{
    aOut.push_back( static_cast<uint8_t>( aValue >> 24 ) );
    aOut.push_back( static_cast<uint8_t>( aValue >> 16 ) );
    aOut.push_back( static_cast<uint8_t>( aValue >> 8 ) );
    aOut.push_back( static_cast<uint8_t>( aValue ) );
}


/// Insert a pHYs chunk (physical pixel size) right after IHDR.
std::vector<uint8_t> addPhysChunk( const std::vector<uint8_t>& aPng, double aDpi )
{
    // Signature (8) + IHDR chunk (4 length + 4 type + 13 data + 4 crc) = 33 bytes.
    constexpr size_t IHDR_END = 33;

    if( aPng.size() < IHDR_END || aDpi <= 0 )
        return aPng;

    const uint32_t pixelsPerMetre = static_cast<uint32_t>( std::lround( aDpi / 0.0254 ) );

    std::vector<uint8_t> chunk;
    putU32( chunk, 9 );
    const size_t typeStart = chunk.size();
    chunk.insert( chunk.end(), { 'p', 'H', 'Y', 's' } );
    putU32( chunk, pixelsPerMetre );
    putU32( chunk, pixelsPerMetre );
    chunk.push_back( 1 ); // unit: metre
    putU32( chunk, crc32( chunk.data() + typeStart, chunk.size() - typeStart ) );

    std::vector<uint8_t> out( aPng.begin(), aPng.begin() + IHDR_END );
    out.insert( out.end(), chunk.begin(), chunk.end() );
    out.insert( out.end(), aPng.begin() + IHDR_END, aPng.end() );
    return out;
}

} // namespace


void WritePng( const std::string& aSvg, int aWidth, int aHeight, double aDpi,
               const std::filesystem::path& aPath )
{
    std::unique_ptr<lunasvg::Document> document = lunasvg::Document::loadFromData( aSvg );

    if( !document )
        throw std::runtime_error( "internal error: could not load generated SVG" );

    lunasvg::Bitmap bitmap = document->renderToBitmap( aWidth, aHeight, 0x00000000 );

    if( bitmap.isNull() )
        throw std::runtime_error( "could not render PNG image" );

    std::vector<uint8_t> png;

    auto writer = []( void* aClosure, void* aData, int aSize )
    {
        auto* buffer = static_cast<std::vector<uint8_t>*>( aClosure );
        const auto* bytes = static_cast<const uint8_t*>( aData );
        buffer->insert( buffer->end(), bytes, bytes + aSize );
    };

    if( !bitmap.writeToPng( writer, &png ) )
        throw std::runtime_error( "could not encode PNG image" );

    png = addPhysChunk( png, aDpi );

    std::ofstream out( aPath, std::ios::binary );

    if( !out )
        throw std::runtime_error( "cannot write '" + aPath.string() + "'" );

    out.write( reinterpret_cast<const char*>( png.data() ), static_cast<std::streamsize>( png.size() ) );

    if( !out )
        throw std::runtime_error( "error writing '" + aPath.string() + "'" );
}

} // namespace kirend::output
