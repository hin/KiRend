#include "paths/embedded_files.h"

#include "paths/base64.h"

#include <zstd.h>

namespace kirend {

std::vector<uint8_t> DecodeEmbeddedFile( const pcb::EmbeddedFile& aFile )
{
    if( aFile.data.empty() )
        throw EmbeddedFileError( "embedded file '" + aFile.name + "' has no data" );

    std::optional<std::vector<uint8_t>> compressed = DecodeBase64( aFile.data );

    if( !compressed )
        throw EmbeddedFileError( "embedded file '" + aFile.name + "' is not valid base64" );

    ZSTD_DStream* stream = ZSTD_createDStream();

    if( !stream )
        throw EmbeddedFileError( "out of memory" );

    ZSTD_initDStream( stream );

    std::vector<uint8_t> out;
    std::vector<uint8_t> chunk( ZSTD_DStreamOutSize() );
    ZSTD_inBuffer        input{ compressed->data(), compressed->size(), 0 };
    size_t               ret = 1;

    while( input.pos < input.size )
    {
        ZSTD_outBuffer output{ chunk.data(), chunk.size(), 0 };
        ret = ZSTD_decompressStream( stream, &output, &input );

        if( ZSTD_isError( ret ) )
        {
            ZSTD_freeDStream( stream );
            throw EmbeddedFileError( "embedded file '" + aFile.name
                                     + "' could not be decompressed: " + ZSTD_getErrorName( ret ) );
        }

        out.insert( out.end(), chunk.begin(), chunk.begin() + output.pos );
    }

    // Flush whatever is still buffered.
    while( ret != 0 )
    {
        ZSTD_outBuffer output{ chunk.data(), chunk.size(), 0 };
        ret = ZSTD_decompressStream( stream, &output, &input );

        if( ZSTD_isError( ret ) || output.pos == 0 )
            break;

        out.insert( out.end(), chunk.begin(), chunk.begin() + output.pos );
    }

    ZSTD_freeDStream( stream );
    return out;
}

} // namespace kirend
