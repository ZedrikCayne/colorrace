#include <string.h>
#include <time.h>

#include <crankshaft/alloc.h>
#include <crankshaft/logger.h>
#include <crankshaft/mutex.h>
#include <crankshaft/thread.h>
#include <crankshaft/hashtable.h>
#include <crankshaft/slaballoc.h>
#include <crankshaft/json.h>
#include <crankshaft/util.h>
#include <crankshaft/mutex.h>
#include <crankshaft/random.h>

#include "gamesession.hpp"
#include "boardstate.hpp"
#include "usersession.hpp"
#include "util.h"

static bool userAndIdLive( struct LocalUserAndId *localUser ) {
    if( !localUser->session )
        return false;
    if( localUser->moves )
        return false;
    return true;
}

enum {
    GAME_STATE_SETTING_UP = 0,
    GAME_STATE_QUEUING,
    GAME_STATE_RUNNING_OPEN,
    GAME_STATE_RUNNING_CLOSED,
    GAME_STATE_SCORING,
    GAME_STATE_HOLDING_FOR_OTHERS,
    GAME_STATE_DONE,
    GAME_STATE_ERROR
};

static const char *enumToString[] = {
    "GAME_STATE_SETTING_UP",
    "GAME_STATE_QUEUING",
    "GAME_STATE_RUNNING_OPEN",
    "GAME_STATE_RUNNING_CLOSED",
    "GAME_STATE_SCORING",
    "GAME_STATE_HOLDING_FOR_OTHERS",
    "GAME_STATE_DONE",
    "GAME_STATE_ERROR"
};

const char *GameSession::getStateString() {
    if( m_state >= CS_ARRAY_SIZE( enumToString ) ||
        m_state < 0 ) {
        return enumToString[ GAME_STATE_ERROR ];
    }
    return enumToString[ m_state ];
}

static bool staticGameThread( CS_Thread *thread, int threadState, void *voidGameSession ) {
    GameSession *gameSession = (GameSession *)voidGameSession;
    if( gameSession == NULL ) return true;
    return gameSession->gameThread( threadState );
}

bool GameSession::broadcastBoardStates() {
    CS_hashtableGrabMutex( m_users );
    CS_HASHTABLE_ITER( m_users, item ) {
        struct LocalUserAndId *lu = (struct LocalUserAndId *)item->value;
        UserSession *session = lu->session;
        session->sendBoardState();
    }
    CS_hashtableReleaseMutex( m_users );
    return false;
}


bool GameSession::okToSendState() {
    return m_state > GAME_STATE_QUEUING;
}

bool GameSession::setState( int gameState ) {
    if( gameState != m_state ) {
        m_state = gameState;
        m_lastStateChange = time(NULL);
        broadcastGameState();
    }
    return false;
}

bool GameSession::broadcastGameState() {
    return broadcast( "state", getStateString() );
}

bool GameSession::broadcast( const char *what, const char *value ) {
    CS_jsonNodeReset( m_broadcastJSON );
    CS_SB_reset( m_broadcastSB );
    CS_jsonNodeAddUnquotedString( CS_jsonNodeAppendObject( m_broadcastJSON, NULL ), what, value );
    CS_jsonNodePrintableToStringBuilder( m_broadcastJSON, m_broadcastSB );
    roundStringBuilderForWebsockets( m_broadcastSB );
    CS_hashtableGrabMutex( m_users );
    CS_HASHTABLE_ITER( m_users, item ) {
        struct LocalUserAndId *lu = (struct LocalUserAndId *)item->value;
        UserSession *session = lu->session;
        if( session ) {
            session->sendThing( CS_SB_buffer( m_broadcastSB ), CS_SB_length( m_broadcastSB ) );
        }
    }
    CS_hashtableReleaseMutex( m_users );
    return false;
}

int GameSession::getId( UserSession *session ) {
    int returnValue = -1;
    struct LocalUserAndId *lu = getLocalUserForUserSession( session );
    if( lu ) returnValue = lu->userId;
    return returnValue;
}

void GameSession::logboard() {
    if( m_currentBoard ) m_currentBoard->logboard();
}

bool GameSession::writeChanges() {
    CS_jsonNodeReset( m_broadcastJSON );
    struct CS_JsonNode *root = CS_jsonNodeAppendObject( m_broadcastJSON, NULL );
    struct CS_JsonNode *users = CS_jsonNodeAddArray( root, "users" );
    CS_hashtableGrabMutex( m_users );
    int nCount = 0;
    CS_HASHTABLE_ITER( m_users, item ) {
        struct LocalUserAndId *lu = (struct LocalUserAndId *)item->value;
        UserSession *session = lu->session;
        if( session && session->dirty() ) {
            struct CS_JsonNode *user = CS_jsonNodeAddObject( users, NULL );
            CS_jsonNodeAddInteger( user, "id", lu->userId );
            CS_jsonNodeAddUnquotedStringWithLength( user, "boardState", session->boardState(), BoardState::BYTES_IN_STATE);
            session->unlockBoardState();
            CS_jsonNodeAddInteger( user, "moves", session->moveCount() );
            CS_jsonNodeAddInteger( user, "tiles", session->tileCount() );
            CS_jsonNodeAddInteger( user, "ms", session->currentMs() );
            ++nCount;
        }
    }
    if( nCount > 0 ) {
        CS_SB_reset( m_broadcastSB );
        CS_jsonNodePrintableToStringBuilder( m_broadcastJSON, m_broadcastSB );
        roundStringBuilderForWebsockets( m_broadcastSB );
        CS_HASHTABLE_ITER( m_users, item ) {
            struct LocalUserAndId *lu = (struct LocalUserAndId *)item->value;
            UserSession *session = lu->session;
            if( session ) {
                session->sendThing(CS_SB_buffer(m_broadcastSB), CS_SB_length(m_broadcastSB) );
            }
        }
    }
    CS_hashtableReleaseMutex( m_users );
    
    return false;
}

const char *GameSession::boardState() {
    return m_currentBoard->currentBoardState();
}

void GameSession::unlockBoardState() {
    return m_currentBoard->unlockBoardState();
}

bool GameSession::allDisconnected() {
    bool returnValue = true;
    CS_hashtableGrabMutex( m_users );
    CS_HASHTABLE_ITER( m_users, item ) {
        struct LocalUserAndId *lu = (struct LocalUserAndId *)item->value;
        if( lu ) {
            UserSession *session = lu->session;
            if( !session || !session->isConnected() ) {
                returnValue = false;
                break;
            }
        }
    }
    CS_hashtableReleaseMutex( m_users );
    return returnValue;
}

bool GameSession::anyWon() {
    bool returnValue = false;
    CS_hashtableGrabMutex( m_users );
    CS_HASHTABLE_ITER( m_users, item ) {
        struct LocalUserAndId *lu = (struct LocalUserAndId *)item->value;
        if( lu ) {
            if( lu->finishedMS ) {
                returnValue = true;
                break;
            }
        }
    }
    CS_hashtableReleaseMutex( m_users );
    return returnValue;
}

const char *GameSession::name() {
    return m_name;
}

bool GameSession::userFinishRound( UserSession *userSession ) {
    if( !userSession ) return true;
    struct LocalUserAndId *local = getLocalUserForUserSession( userSession );

    if( !userSession->boardStartingStatesMatch( m_currentBoard ) ||
        !userSession->hasFinished() ) {
        return true;
    }
    
    local->finishedMS = userSession->currentMs();
    local->finishedMoves = userSession->moveCount();

    return false;
}

bool GameSession::allWon() {
    bool returnValue = true;
    CS_hashtableGrabMutex( m_users );
    CS_HASHTABLE_ITER( m_users, item ) {
        struct LocalUserAndId *lu = (struct LocalUserAndId *)item->value;
        if( lu ) {
            if( lu->abandoned || lu->finishedMS ) continue;
            UserSession *session = lu->session;
            if( session && !session->hasFinished() ) {
                returnValue = false;
                break;
            }
        }
    }
    CS_hashtableReleaseMutex( m_users );
    return returnValue;
}

bool GameSession::gameThread( int threadState ) {
    if( threadState == CS_THREAD_START ) {
        setState( GAME_STATE_QUEUING );
        return false;
    }
    int elapsed = time(NULL) - m_lastStateChange;
    if( threadState == CS_THREAD_RUNNING ) {
        switch( m_state ) {
            case GAME_STATE_QUEUING:
                if( elapsed > 5 ) {
                    setState( GAME_STATE_RUNNING_OPEN );
                    broadcast( "warn", "Let's GO!" );
                    broadcastBoardStates();
                }
                break;
            case GAME_STATE_RUNNING_OPEN:
                if( anyWon() || allDisconnected() ) {
                    setState( GAME_STATE_RUNNING_CLOSED );
                    broadcast( "warn", "30 seconds until scoring." );
                }
                break;
            case GAME_STATE_RUNNING_CLOSED:
                if( allWon() || elapsed > 30 ) {
                    broadcast( "warn", "Session scoring." );
                    setState( GAME_STATE_SCORING );
                }
                break;
            case GAME_STATE_SCORING:
                if( allWon() && elapsed > 15 ) {
                    setState( GAME_STATE_HOLDING_FOR_OTHERS );
                    broadcast( "warn", "Session over. Will remain active." );
                }
            case GAME_STATE_HOLDING_FOR_OTHERS:
                if( allDisconnected() ) {
                    setState( GAME_STATE_DONE );
                }
            case GAME_STATE_DONE:
                return true;
                break;
        }
        writeChanges();
        struct timespec ts = { 0, 100000000 };
        nanosleep(&ts,NULL);
    }
    if( threadState == CS_THREAD_STOP ) {
        broadcast("go","next");
        m_reap = true;
    }
    return false;
}

bool GameSession::reapMe() {
    return m_reap;
}

bool GameSession::start() {
    m_thread = CS_threadStart( m_name, this, staticGameThread );
    return m_thread != NULL;
}

bool GameSession::abandon( UserSession *userSession ) {
    struct LocalUserAndId *local = getLocalUserForUserSession( userSession );
    if( local ) {
        local->session = NULL;
        local->abandoned = true;
    }
    return false;
}

bool GameSession::checkAbandoned( UserSession *userSession ) {
    const void *voidLocalUser = CS_hashtableGet(m_users, userSession->sessionKey() );
    if( voidLocalUser == CS_HASHTABLE_ERROR ) {
        return false;
    }
    struct LocalUserAndId *lu = (struct LocalUserAndId *)voidLocalUser;
    return lu->abandoned;
}

struct LocalUserAndId *GameSession::getLocalUserForUserSession( UserSession *userSession ) {
    const void *voidLocalUser = CS_hashtableGet(m_users, userSession->sessionKey() );
    struct LocalUserAndId *returnValue;
    if( voidLocalUser == CS_HASHTABLE_ERROR ) {
        voidLocalUser = CS_slabTakeZero( m_pairSlabs );
        returnValue = (struct LocalUserAndId *)voidLocalUser;
        returnValue->userId = nextId();
        CS_hashtablePut(m_users, userSession->sessionKey(), voidLocalUser);
    } else {
        returnValue = (struct LocalUserAndId *)voidLocalUser;
    }
    if( returnValue->abandoned ) return NULL;
    if( returnValue->session != userSession ) {
        if( returnValue->session ) {
            returnValue->session->kick();
        }
        returnValue->session = userSession;
    }
    return returnValue;
}

int GameSession::nextId() {
    int returnValue;
    CS_mutexLock(m_idsMutex);
    m_ids = m_ids + CS_randMod(3) + 1;
    returnValue = m_ids;
    CS_mutexUnlock(m_idsMutex);
    return returnValue;
}

GameSession::GameSession( const char *name ) {
    memset( m_name, 0, sizeof(m_name) );
    strncpy( m_name, name, 60 );
    m_currentBoard = new BoardState();
    m_currentBoard->randomState();
    m_state = GAME_STATE_SETTING_UP;
    m_pairSlabs = CS_slabInit("sessionpairs", sizeof(struct LocalUserAndId), 40, sizeof(void*) );
    m_owner = NULL;
    m_created = time(NULL);
    m_reap = false;
    m_broadcastJSON = CS_jsonNodeNew( 4096 );
    m_broadcastSB = CS_SB_create( 4096 );
    m_ids = CS_randMod(130) + 5;
    m_idsMutex = CS_mutexTakeNamed("GAMESESSIONIDS");

    m_mutex = CS_mutexTakeNamed( "GAMESESSION" );
    m_users = CS_HASHTABLE_STRING_VOID( 50, CS_HASHTABLE_FLAG_MUTEX | CS_HASHTABLE_FLAG_VERY_PEDANTIC );
    m_thread = NULL;
}

GameSession::~GameSession() {
    delete m_currentBoard;
    CS_mutexReturn( m_mutex );
    CS_hashtableFree( m_users );
    CS_slabFree( m_pairSlabs );
    m_owner = NULL;
}

bool GameSession::acceptingNewUsers() {
    return m_state < GAME_STATE_RUNNING_CLOSED;
}

bool GameSession::removeUserSession( UserSession *session ) {
    struct LocalUserAndId *local = getLocalUserForUserSession( session );
    if( local ) local->session = NULL;
    return false;
}

bool GameSession::addUserSession( UserSession *session ) {
    struct LocalUserAndId *local = getLocalUserForUserSession( session );
    return local == NULL;
}

bool GameSession::userSetBoard( UserSession *session, const char *boardState, int boardStateLength ) {
    if( session != m_owner ) {
        return true;
    }
    return false;
}

const char *GameSession::sessionName() {
    return m_name;
}

