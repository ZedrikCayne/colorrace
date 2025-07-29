#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <crankshaft/alloc.h>
#include <crankshaft/logger.h>
#include <crankshaft/mutex.h>
#include <crankshaft/list.h>
#include <crankshaft/util.h>

#include <random>

#include "colorrace.hpp"
#include "boardstate.hpp"

const char BoardState::ckeys[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ,-.";
const char BoardState::colors[] = "rgbywoxx";
std::mt19937 BoardState::mt( time(NULL) );

//Color states are stored in a line of ckeys. Each representing 2 squares on a board.
//The first character in a state is where the 'hole' is. So for a 5x5 board, you get
//12 bytes plus 1 for the hole.
//For the target, 1 for the hole, 5 bytes for the rest.
//Total board state for a puzzle is: a1234567890AB:0CDEFG
//                                   ^^^^board^^^^ target
//Total of 20 bytes.
//For the state above...the first byte is a. Which is the 10th character in the array
//which makes the 'hole' or 'x' spot on the board the first in the second row.
//
//Each following character grabs the index, and maps to:
//colors[index>>3] in the first box and colors[index%8]
//in the second one.
//
//    *******************************       *******************
//    *     *     *     *     *     *       *     *     *     *
//    *  1  *  1  *  2  *  2  *  3  *       *  C  *  C  *  D  *
//    *     *     *     *     *     *       *     *     *     *
//    *******************************       *******************
//    *     *     *     *     *     *       *     *     *     *
//    *  3  *  4  *  4  *  5  *  5  *       *  D  *  E  *  E  *
//    *     *     *     *     *     *       *     *     *     *
//    *******************************       *******************
//    *     *     *     *     *     *       *     *     *     *
//    *  x  *  6  *  6  *  7  *  7  *       *  F  *  F  *  G  *
//    *     *     *     *     *     *       *     *     *     *
//    *******************************       *******************
//    *     *     *     *     *     *
//    *  8  *  8  *  9  *  9  *  0  *
//    *     *     *     *     *     *
//    *******************************
//    *     *     *     *     *     *
//    *  0  *  A  *  A  *  B  *  B  *
//    *     *     *     *     *     *
//    *******************************

BoardState::BoardState() {
    m_currentStateMutex = CS_mutexTakeNamed("BoardMutex");
    m_moves = 0;
    clear();
}

BoardState::~BoardState() {
    CS_mutexReturn( m_currentStateMutex );
    m_moves = 0;
}

int BoardState::charToIndex( const char aChar ) {
    if( aChar >= '0' && aChar <= '9' ) return aChar - '0';
    if( aChar >= 'a' && aChar <= 'z' ) return aChar - 'a' + 10;
    if( aChar >= 'A' && aChar <= 'L' ) return aChar - 'A' + 36;
    if( aChar >= ',' && aChar <= '.' ) return aChar - ',' + 62;
    return -1;
}

bool BoardState::decodeColors( const char *encoded, int length, char *output, int outputLength ) {
    if( outputLength < 2*length ) {
        CS_LOG_WARN("Output buffer for decoding colors too short.");
        return true;
    }
    memset( output, 0, outputLength );
    int outputChar = 0;
    int colorCounters[CS_ARRAY_SIZE(colors)] = {0};
    for( int i = 0; i < length; ++i ) {
        int index = charToIndex( encoded[i] );
        if( index < 0 ) {
            CS_LOG_WARN("Board state bad.");
            return true;
        }
        int firstIndex = index >> 3;
        int secondIndex = index % 8;
        output[ outputChar++ ] = colors[ firstIndex ];
        colorCounters[ firstIndex ]++;
        output[ outputChar++ ] = colors[ secondIndex ];
        colorCounters[ secondIndex ]++;
    }

    for( int i = 0; i < 6; ++i ) {
        if( colorCounters[i] > 4 ) {
            CS_LOG_WARN("Too many tiles of a particular color in board state.");
            return true;
        }
    }
    for( int i = 6; i < CS_ARRAY_SIZE(colors); ++i ) {
        if( colorCounters[ i ] > 0 ) {
            CS_LOG_WARN("Too many black tiles in board state.");
            return true;
        }
    }

    return false;
}
bool BoardState::setupBoardState( const char *startingState, int length ) {
    if( length != BYTES_IN_STATE ) {
        CS_LOG_WARN( "Board state length not 20" );
        return true;
    }
    if( startingState[13] != ':' ) {
        CS_LOG_WARN( "Board state separator not in the right spot.");
        return true;
    }
    int holeIndex = charToIndex( *startingState );
    if( holeIndex < MINIMUM_TILE_INDEX || holeIndex > NUMBER_OF_COLORED_TILES ) {
        CS_LOG_WARN("Board hole not specified correctly.");
        return true;
    }
    char colors[ 24 ];
    int currentTile = 0;

    m_startTimeMS = ColorRaceApplication::epochMillisecond();
    m_endTimeMS = -1;
    m_moves = 0;
    m_tiles = 0;

    //Decode prints its own warnings.
    if( decodeColors( startingState + BYTES_FOR_HOLE, BYTES_FOR_BOARD, colors, BYTES_FOR_BOARD_DECODE ) ) {
        return true;
    }

    int currentColor = 0;
    for( int i = 0; i < TILES_WIDE; ++i ) {
        for( int j = 0; j < TILES_WIDE; ++j ) {
            if( currentTile == holeIndex ) {
                m_holeRow = i;
                m_holeColumn = j;
                m_board[i][j] = Black;
            } else {
                m_board[i][j] = colors[ currentColor ];
                ++currentColor;
            }
            ++currentTile;
        }
    }

    currentColor = 0;
    if( decodeColors( startingState + 15, 5, colors, 10 ) ) {
        return true;
    }
    for( int i = 0; i < TARGET_TILES_WIDE; ++i ) {
        for( int j = 0; j < TARGET_TILES_WIDE; ++j ) {
            m_target[i][j] = colors[currentColor];
            ++currentColor;
        }
    }
    if( startingState != m_startingState ) {
        memcpy( m_startingState, startingState, length );
    }
    memcpy( m_currentState, startingState, length );

    m_dirty = false;

    return false;
}

bool BoardState::startingStatesMatch( BoardState *anotherBoard ) {
    if( anotherBoard == NULL ) return false;
    return memcmp( m_startingState, anotherBoard->m_startingState, BYTES_IN_STATE ) == 0;
}

void BoardState::logboard() {
    struct CS_StringBuilder *sb = CS_SB_create( 64 );
    if( !sb ) return;
    for( int i = 0; i < TILES_WIDE; ++i ) {
        if( i != 0 ) CS_SB_appendChar( sb, '\n' );
        for( int j = 0; j < TILES_HIGH; ++j ) {
            CS_SB_appendChar( sb, m_board[ i ][ j ] );
        }
        if( i < TARGET_TILES_WIDE ) {
            CS_SB_appendChar(sb, ' ');
            for( int j = 0; j < TARGET_TILES_HIGH; ++j ) {
                CS_SB_appendChar( sb, m_target[i][j] );
            }
        }
    }
    CS_LOG_INFO( "%s", sb->buffer );
    CS_SB_free( sb );
}

const char *BoardState::currentBoardState( void ) {
    CS_mutexLock( m_currentStateMutex );
    if( m_dirty ) {
        if( encodeCurrentState() ) {
            CS_mutexUnlock( m_currentStateMutex );
            return NULL;
        }
    }
    return m_currentState;
}

void BoardState::unlockBoardState(void) {
    CS_mutexUnlock( m_currentStateMutex );
}

bool BoardState::encodeBoard( char *destination, int indexForHole, const char *boardColors, const char *targetColors ) {
    destination[0] = ckeys[ indexForHole ];
    if( encodeColors( boardColors, NUMBER_OF_COLORED_TILES, destination + BYTES_FOR_HOLE, BYTES_FOR_BOARD ) ) return true;
    destination[ BYTES_FOR_HOLE + BYTES_FOR_BOARD ] = ':';
    destination[ BYTES_FOR_HOLE + BYTES_FOR_BOARD + BYTES_FOR_SEPARATOR ] = '0';
    if( encodeColors( targetColors, NUMBER_OF_TARGET_TILES, destination + BYTES_FOR_HOLE + BYTES_FOR_BOARD + BYTES_FOR_SEPARATOR + BYTES_FOR_HOLE, BYTES_FOR_TARGET ) ) return true;

    return false;
}


bool BoardState::encodeCurrentState() {
    int indexForHole = -1;
    char boardColors[NUMBER_OF_COLORED_TILES];
    char targetColors[NUMBER_OF_TARGET_TILES];
    int currentColor = 0;
    int currentIndex = 0;
    for( int i = 0; i < TILES_WIDE; ++i ) {
        for( int j = 0; j < TILES_HIGH; ++j ) {
            if( m_board[i][j] == Black ) {
                indexForHole = currentIndex;
            } else {
                boardColors[ currentColor ] = m_board[i][j];
                ++currentColor;
            }
            ++currentIndex;
        }
    }
    currentColor = 0;
    for( int i = 0; i < TARGET_TILES_WIDE; ++i ) {
        for( int j = 0; j < TARGET_TILES_WIDE; ++j ) {
            targetColors[currentColor] = m_target[i][j];
            ++currentColor;
        }
    }
    if( indexForHole < 0 ) {
        CS_LOG_WARN( "No hole found while trying to encode a board." );
        return true;
    }

    encodeBoard( m_currentState, indexForHole, boardColors, targetColors );

    m_dirty = false;

    return false;
}

int BoardState::colorToIndex( const char aChar ) {
    for( int i = 0; i < CS_ARRAY_SIZE( colors )-1; ++i ) {
        if( aChar == colors[i] ) return i;
    }
    return -1;
}

bool BoardState::encodeColors( const char *inputColors, int length, char *output, int outputLength ) {
    int currentOutput = 0;
    int requiredLength = (length >> 2) + (length&0x01);
    if( requiredLength > outputLength ) {
        CS_LOG_WARN( "Output length too short." );
        return true;
    }
    int outputIndex1 = 0;
    int outputIndex2 = 0;
    for( int i = 0; i < length; ++i ) {
        int index = colorToIndex( inputColors[ i ] );
        if( index < 0 ) {
            CS_LOG_WARN( "Input color bad for encoding." );
            return true;
        }
        if( i % 2 ) {
            outputIndex2 = (outputIndex1 + index);
            output[currentOutput] = ckeys[outputIndex2];
            int outputIndex = charToIndex( output[currentOutput] );

            ++currentOutput;
        } else {
            outputIndex1 = index << 3;
            output[currentOutput] = ckeys[outputIndex1];
        }
    }
    return false;
}

bool BoardState::touch( int tileRow, int tileColumn ) {
    bool returnValue = false;
    CS_mutexLock( m_currentStateMutex );
    if( m_endTimeMS > 0 ) goto TOUCH_ERROR;
    //Reject move if out of bounds or we are touching the black tile.
    if( tileRow < 0 || tileRow >= TILES_WIDE ||
        tileColumn < 0 || tileColumn >= TILES_WIDE ) {
        CS_LOG_WARN( "User touch outside of bounds." );
        returnValue = true;
        goto TOUCH_ERROR;
    }
    if( !((tileRow == m_holeRow) ^ (tileColumn == m_holeColumn)) ) {
        CS_LOG_WARN( "User touch either not on the row/column with the hole or right on the hole." );
        returnValue = true;
        goto TOUCH_ERROR;
    }
    if( tileRow == m_holeRow ) {
        int incr = m_holeColumn < tileColumn ? 1 : -1;
        for( int i = m_holeColumn; i != tileColumn; i += incr ) {
            ++m_tiles;
            swap( tileRow, i, tileRow, i + incr );
        }
    } else {
        int incr = m_holeRow < tileRow ? 1 : -1;
        for( int i = m_holeRow; i != tileRow; i += incr ) {
            ++m_tiles;
            swap( i, tileColumn, i + incr, tileColumn );
        }
    }
    m_dirty = true;
    m_holeRow = tileRow;
    m_holeColumn = tileColumn;
    ++m_moves;
    if( winner() ) m_endTimeMS = ColorRaceApplication::epochMillisecond();

TOUCH_ERROR:
    CS_mutexUnlock( m_currentStateMutex );
    return returnValue;
}

int BoardState::tilesSetCorrectly() {
    int nMatch = 0;
    for( int i = 0; i < TARGET_TILES_WIDE; ++i ) {
        for( int j = 0; j < TARGET_TILES_WIDE; ++j ) {
            if( m_board[i+1][j+1] == m_target[i][j] ) {
                ++nMatch;
            }
        }
    }
    return nMatch;
}

bool BoardState::winner() {
    return tilesSetCorrectly() == NUMBER_OF_TARGET_TILES;
}

bool BoardState::reset() {
    setupBoardState(m_startingState,BYTES_IN_STATE);
    memcpy(m_currentState, m_startingState, sizeof(m_currentState));
    
    return false;
}

bool BoardState::clear() {
    m_startTimeMS = -1;
    m_endTimeMS = -1;
    m_holeRow = -1;
    m_holeColumn = -1;
    memset( m_startingState, 0, sizeof(  m_startingState ) );
    memset( m_currentState, 0, sizeof(  m_startingState ) );
    return false;
}


void BoardState::swap(int row1, int column1, int row2, int column2) {
    char temp = m_board[ row1 ][ column1 ];
    m_board[ row1 ][ column1 ] = m_board[ row2 ][ column2 ];
    m_board[ row2 ][ column2 ] = temp;
}

void BoardState::randomState() {
    CS_mutexLock( m_currentStateMutex );

    char sourceColors[] = "rrrrggggbbbbyyyyoooowwww";
    char targetColors[] = "rrrgggbbbyyyooowww";

    int indexForHole = mt() % NUMBER_OF_SPACES_FOR_TILES;
    
    for( int i = 0; i < 45; ++i ) {
        int i1 = mt() % 24;
        int i2 = mt() % 24;
        char temp = sourceColors[i1];
        sourceColors[i1] = sourceColors[i2];
        sourceColors[i2] = temp;
        i1 = mt() % 18;
        i2 = mt() % 18;
        temp = targetColors[i1];
        targetColors[i1] = targetColors[i2];
        targetColors[i2] = temp;
    }

    encodeBoard( m_startingState, indexForHole, sourceColors, targetColors );

    setupBoardState( m_startingState, BYTES_IN_STATE );

    CS_mutexUnlock( m_currentStateMutex );
}

int BoardState::elapsedMs() {
    if( m_endTimeMS > m_startTimeMS ) {
        return m_endTimeMS - m_startTimeMS;
    } else {
        return ColorRaceApplication::epochMillisecond() - m_startTimeMS;
    }
}

int BoardState::tileCount() {
    return m_tiles;
}

int BoardState::moveCount() {
    return m_moves;
}

bool BoardState::dirty() {
    return m_dirty;
}

int BoardState::startedMs() {
    return m_startTimeMS;
}
