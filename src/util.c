#include <crankshaft/stringbuilder.h>
#include <crankshaft/util.h>

#include "util.h"

void roundStringBuilderForWebsockets( struct CS_StringBuilder *sb ) {
    int currentSize = CS_SB_size( sb );
    int newSize = CS_align( currentSize, 4 );
    for( int i = currentSize; i < newSize; ++i ) {
        CS_SB_appendChar( sb, ' ' );
    }
    CS_SB_appendChar( sb, '\r' );
    CS_SB_appendChar( sb, '\n' );
    CS_SB_appendChar( sb, '\r' );
    CS_SB_appendChar( sb, '\n' );
}





