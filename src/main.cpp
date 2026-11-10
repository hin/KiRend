#include "cli/options.h"
#include "kirend.h"

#include <iostream>
#include <string>
#include <vector>

int main( int argc, char** argv )
{
    std::vector<std::string> args( argv, argv + argc );

    kirend::cli::ParseResult parsed = kirend::cli::ParseCommandLine( args, std::cout, std::cerr );

    if( !parsed.job )
        return parsed.exitCode;

    return kirend::RunRenderJob( *parsed.job, std::cerr );
}
