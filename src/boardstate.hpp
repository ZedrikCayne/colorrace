#ifndef __boardstatedoth__
#define __boardstatedoth__
#include <stdlib.h>
#include <stdbool.h>

#include <random>

struct MoveAndTime {
    char row;
    char column;
    int  ms;
};

class BoardState {
public:
    BoardState();
    ~BoardState();

    bool setupBoardState( const char *startingState, int length );
    //Grabbing the board state locks the state mutex...don't forget to return it.
    bool startingStatesMatch( BoardState *board );
    const char *currentBoardState( void );
    void unlockBoardState();
    bool touch( int tileX, int tileY );
    bool reset();
    bool clear();
    int  tilesSetCorrectly();
    bool winner();
    void randomState();
    bool dirty();
    int tileCount();
    int moveCount();
    int elapsedMs();
    int startedMs();

    static const char Red = 'r';
    static const char Green = 'g';
    static const char Blue = 'b';
    static const char Yellow = 'y';
    static const char White = 'w';
    static const char Orange = 'o';
    static const char Black = 'x';
    static const int BYTES_IN_STATE = 20;
    static const int TILES_WIDE = 5;
    static const int TILES_HIGH = 5;
    static const int TARGET_TILES_WIDE = 3;
    static const int TARGET_TILES_HIGH = 3;
    static const int BYTES_FOR_HOLE = 1;
    static const int BYTES_FOR_BOARD = 12;
    static const int BYTES_FOR_BOARD_DECODE = 24;
    static const int BYTES_FOR_SEPARATOR = 1;
    static const int BYTES_FOR_TARGET = 5;
    static const int BYTES_FOR_TARGET_DECODE = 10;
    static const int NUMBER_OF_TARGET_TILES = 9;
    static const int NUMBER_OF_COLORED_TILES = 24;
    static const int NUMBER_OF_EACH_COLOR = 4;
    static const int NUMBER_OF_SPACES_FOR_TILES = 25;
    static const int MINIMUM_TILE_INDEX = 0;
    static const int TILES_MATCH_TO_WIN = 9;
    
    void logboard();

private:
    void swap(int row, int column, int row2, int column2);
    bool decodeColors( const char *encoded, int length, char *output, int outputLength );
    bool encodeColors( const char *colors, int length, char *output, int outputLength );
    int charToIndex( const char aChar );
    int colorToIndex(const char aChar);
    bool encodeCurrentState();
    bool encodeBoard( char *destination, int indexForHole, const char *boardColors, const char *targetColors );

    bool m_dirty; //Reference to the 'current state' which we will only encode if asked
    long long m_startTimeMS;
    long long m_endTimeMS;
    struct CS_SlabAlloc *m_individualMoveSlab;
    int m_moves;
    int m_tiles;
    int  m_holeRow, m_holeColumn;
    char m_startingState[BYTES_IN_STATE];
    char m_currentState[BYTES_IN_STATE];
    char m_board[TILES_WIDE][TILES_HIGH];
    char m_target[TARGET_TILES_WIDE][TARGET_TILES_HIGH];

    struct CS_Mutex *m_currentStateMutex = NULL;
    static const char ckeys[];
    static const char colors[];
    static std::mt19937 mt;
};

#endif
