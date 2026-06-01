#include <string.h>

#include <crankshaft/alloc.h>
#include <crankshaft/logger.h>

#include <crankshaft/string.h>
#include <crankshaft/websocket.h>
#include <crankshaft/json.h>
#include <crankshaft/tempbuff.h>
#include <crankshaft/util.h>
#include <crankshaft/stringbuilder.h>

#include "colorrace.hpp"
#include "usersession.hpp"
#include "util.h"

UserSession::UserSession(const char *sessionKey) {
    m_sessionKey = CS_cstringCopy(sessionKey);
    m_mutex = CS_mutexTakeNamed("USER_SESSION");
    m_json = CS_jsonNodeNew( 8192 );
    m_sb = CS_SB_create( 8192 );
    m_ws = NULL;
    m_board = new BoardState();
    m_game = NULL;
    m_TLA[0] = 0;
    m_TLA[1] = 0;
    m_TLA[2] = 0;
    m_started = time(NULL);
}

UserSession::~UserSession() {
    if( m_game ) m_game->removeUserSession(this);
    m_game = NULL;
    struct CS_Mutex *mutex = m_mutex;
    CS_mutexLock( mutex );
    m_mutex = NULL;
    CS_cstringFree(m_sessionKey);
    m_sessionKey = NULL;
    if( m_ws ) {
        CS_WS_close( m_ws, CS_WS_CLOSE_GOING_AWAY );
        CS_WS_destroy( m_ws );
    }
    m_ws = NULL;
    CS_mutexUnlock( mutex );
}

const char *UserSession::sessionKey() {
    return m_sessionKey;
}

static char *tempCopyWithNulls( const char *line, int length ) {
    char *copy = (char*)CS_tempBuffZero( length + 4 );
    if( copy == NULL ) {
        return NULL;
    }
    memcpy( copy, line, length ); return copy;
}

void trimWhitespaceRight( char *ofThis ) {
    int n = strlen( ofThis );
    for( int i = n-1; i > 0; ++i ) {
        if( isspace( ofThis[i] ) ) ofThis[i]=0;
        else break;
    }
}


bool UserSession::setWebSocket( struct CS_WebSocket *ws ) {
    CS_mutexLock( m_mutex );
    if( m_ws ) CS_WS_close( m_ws, CS_WS_CLOSE_GOING_AWAY );
    m_ws = ws;
    CS_mutexUnlock( m_mutex );
    return false;
}

bool UserSession::removeWebSocket( struct CS_WebSocket *ws ) {
    CS_mutexLock( m_mutex );
    if( m_ws == ws ) m_ws = NULL;
    CS_mutexUnlock( m_mutex );
    return false;
}

bool UserSession::processFrame( struct CS_WebSocketFrame *frame ) {
    m_last = time(NULL);
    if( frame->payloadLength > 0 ) {
        char *line = tempCopyWithNulls( (const char*)frame->payload, frame->payloadLength );
        char *storage;

        char *command = strtok_r( line, " ", &storage );
        
        if( strcmp("touch", command) == 0 ) {
            char *row = strtok_r( NULL, " ", &storage );
            if( !row ) return true;
            char *column = strtok_r( NULL, " ", &storage );
            if( !column ) return true;
            int iRow = atoi( row );
            int iColumn = atoi( column );
            if( touch( iRow - 1, iColumn - 1 )) {
                //Touch was bad, reset the board state on the client.
                return sendBoardState();
            }
            if( m_board->winner() ) {
                m_game->userFinishRound(this);
                sendWait();
            }
        }

        if( strcmp("create", command) == 0 ) {
            char *name = strtok_r(NULL, " ", &storage );
            trimWhitespaceRight(name);
            return createSession(name);
        }

        if( strcmp("connect", command) == 0 ) {
            return connect();
        }

        if( strcmp("next", command) == 0 ) {
            return next();
        }

        if( strcmp("abandon", command) == 0 ) {
            return abandon();

        }
    }
    return false;
}

bool UserSession::abandon() {
    if( m_game ) m_game->abandon( this );
    m_game = NULL;
    return connect();
}

bool UserSession::next() {
    if( m_game ) m_game->removeUserSession( this );
    m_game = NULL;
    return connect();
}

int UserSession::numberMatched() {
    return m_board->tilesSetCorrectly();
}

bool UserSession::boardStartingStatesMatch( BoardState *otherState ) {
    return m_board->startingStatesMatch( otherState );
}

bool UserSession::hasFinished() {
    return m_board->winner();
}

bool UserSession::isConnected() {
    return m_ws != NULL;
}

void UserSession::kick() {
    if( m_ws ) CS_WS_close( m_ws, CS_WS_CLOSE_GOING_AWAY );
}

bool UserSession::connect(void) {
    if( m_game == NULL ) {
        m_game = ColorRaceApplication::getGameSession(this);
        m_game->addUserSession(this);
        m_idForGame = m_game->getId(this);
        m_board->setupBoardState(m_game->boardState(),BoardState::BYTES_IN_STATE); 
        m_game->unlockBoardState();
    }
    return sendBoardState();
}

bool UserSession::sendServerIssue(const char *what) {
    const char *message = what?what:"Huh. Server Error";
    CS_JsonNode *root = CS_jsonNodeReset(m_json);
    CS_JsonNode *object = CS_jsonNodeAppendObject(root,NULL);
    CS_jsonNodeAddUnquotedCstringWithLength(object,"servererror", message, strlen(message) );
    m_board->unlockBoardState();
    return sendJson( root );
}

bool UserSession::sendBoardState() {
    CS_JsonNode *root = CS_jsonNodeReset(m_json);
    CS_JsonNode *object = CS_jsonNodeAppendObject(root,NULL);
    if( m_game->okToSendState() ) {
        CS_jsonNodeAddUnquotedCstringWithLength(object,"boardstate",m_board->currentBoardState(), BoardState::BYTES_IN_STATE);
        m_board->unlockBoardState();
        CS_jsonNodeAddInteger(object,"start",m_board->startedMs());
        CS_jsonNodeAddInteger(object,"current",m_board->elapsedMs());
        CS_jsonNodeAddInteger(object,"moves",m_board->moveCount());
        CS_jsonNodeAddInteger(object,"tiles",m_board->tileCount());
    }
    CS_jsonNodeAddUnquotedCstring( object, "state", m_game->getStateString() );
    CS_jsonNodeAddInteger(object,"id",m_idForGame);
    return sendJson( root );
}

bool UserSession::sendWait() {
    return sendThing("{\"warn\":\"wait\"}",15);
}

bool UserSession::setTLA( const char *tla ) {
    return false;
}

bool UserSession::createSession( const char *name ) {
    return false;
}

bool UserSession::sendThing( const void *buffer, int length ) {
    bool returnValue = false;
    CS_mutexLock( m_mutex );

    if(!m_ws) {
        returnValue = true;
    } else {

        struct CS_WebSocketFrame *frame = CS_WS_createFrame( m_ws, CS_WS_OPCODE_BINARY, false, buffer, length );
        if( CS_WS_pushFrame( m_ws, frame, true ) ) {
            returnValue = true;
            CS_WS_close( m_ws, CS_WS_CLOSE_GOING_AWAY );
            m_ws = NULL;
        }
    }

    CS_mutexUnlock( m_mutex );
    return returnValue;
}

bool UserSession::sendJson( struct CS_JsonNode *json ) {
    CS_SB_reset( m_sb );
    CS_jsonNodePrintableToStringBuilder( json, m_sb );
    roundStringBuilderForWebsockets( m_sb );
    return sendThing( CS_SB_buffer(m_sb), CS_SB_length(m_sb) );
}

bool UserSession::dirty() {
    return m_board && m_board->dirty();
}

const char *UserSession::boardState() {
    return m_board?m_board->currentBoardState():NULL;
}

void UserSession::unlockBoardState() {
    if( m_board ) m_board->unlockBoardState();
}

int UserSession::tileCount() {
    return m_board?m_board->tileCount():0;
}

int UserSession::moveCount() {
    return m_board?m_board->moveCount():0;
}

int UserSession::currentMs() {
    return m_board?m_board->elapsedMs():0;
}

int UserSession::startedMs() {
    return m_board?m_board->elapsedMs():0;
}

bool UserSession::touch(int row, int column) {
    if( m_board ) {
        return m_board->touch( row, column );
    }
    return true;
}

bool UserSession::nextPuzzle() {
    return false;
}

bool UserSession::specificPuzzle(const char *puzzleDefinition ) {
    return false;
}

bool UserSession::joinGameSession(const char *gameSessionName) {
    return false;
}
