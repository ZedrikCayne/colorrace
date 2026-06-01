#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include <crankshaft/alloc.h>
#include <crankshaft/logger.h>
#include <crankshaft/server.h>
#include <crankshaft/websocket.h>
#include <crankshaft/hashtable.h>
#include <crankshaft/storage.h>
#include <crankshaft/mime.h>
#include <crankshaft/uuid.h>
#include <crankshaft/tempbuff.h>
#include <crankshaft/util.h>
#include <crankshaft/html.h>
#include <crankshaft/string.h>

#include "colorrace.hpp"
#include "userstate.hpp"
#include "usersession.hpp"

ColorRaceApplication *ColorRaceApplication::globalInstance = NULL;


static const CS_String COLORRACE_SESSION_COOKIE = CS_STRING("colorrace_session");
static struct CS_String LOCATION = CS_STRING("Location");
static const struct CS_String WELCOME = CS_STRING("/colorrace/welcome.html");
static const struct CS_String LOGGEDOUT = CS_STRING("logged-out");


long long ColorRaceApplication::epochMillisecond() {
    long long returnValue = 0;

    struct timespec ts;
    
    clock_gettime(CLOCK_REALTIME, &ts);

    returnValue = (ts.tv_sec * 1000);
    returnValue += (ts.tv_nsec / 1000000);

    return returnValue;
}

GameSession *ColorRaceApplication::getGameSession( UserSession *userState ) {
    ColorRaceApplication *app = getInstance();
    if( !app ) return NULL;
    return app->appGetGameSession( userState );
}

bool ColorRaceApplication::reaperThread( struct CS_Thread *thread, int threadState, void *context ) {
    ColorRaceApplication *app = getInstance();
    if( !app ) return true;
    return app->appReaperThread(thread,threadState,context);
}

static const char *freshName() {
    const char *tname = CS_tempBuffSnprintf(64, "pmoc~~~%lld", ColorRaceApplication::epochMillisecond() );
    int nameLen = strlen(tname);
    char *reversedName = (char*)CS_tempBuff( nameLen + 4 );
    for( int i = 0; i < nameLen; ++i ) {
        reversedName[i] = tname[ nameLen - 1 - i ];
    }
    reversedName[ nameLen ] = 0;
    return reversedName;
}

GameSession *ColorRaceApplication::newGameSession( const char *name ) {
    const char *tempName = name?name:freshName();
    GameSession *returnValue = new GameSession(tempName);
    CS_hashtablePut( m_games, tempName, returnValue );
    if( name == NULL ) {
        returnValue->start();
    }
    return returnValue;
}

GameSession *ColorRaceApplication::appGetGameSession( UserSession *forUser ) {
    GameSession *returnValue = NULL;
    CS_hashtableGrabMutex( m_games );
    CS_HASHTABLE_ITER( m_games, entry ) {
        GameSession *gs = (GameSession *)entry->value;
        if( gs->acceptingNewUsers() ) {
            if( forUser ) {
                if( gs->checkAbandoned( forUser ) ) continue;
            }
            returnValue = gs;
            break;
        }
    }
    CS_hashtableReleaseMutex( m_games );
    if( returnValue == NULL ) {
        returnValue = newGameSession(NULL);
    }
    return returnValue;
}

bool ColorRaceApplication::appReaperThread( struct CS_Thread *thread, int threadState, void *context ) {
    const void *removeMe = NULL;
    if( threadState == CS_THREAD_RUNNING ) {
        CS_hashtableGrabMutex( m_games );
        CS_HASHTABLE_ITER( m_games, entry ) {
            GameSession *gs = (GameSession *)entry->value;
            if( gs->reapMe() ) {
                removeMe = entry->fullKey;
                break;
            }
        }
        CS_hashtableReleaseMutex( m_games );
        if( removeMe ) CS_hashtableRemove( m_games, removeMe );
        removeMe = NULL;
        struct timespec ts = { 0, 100000000 };
        nanosleep(&ts,NULL);
    }
    return false;
}

ColorRaceApplication::ColorRaceApplication() {
    m_games = CS_HASHTABLE_STRING_VOID( 50, CS_HASHTABLE_FLAG_MUTEX|CS_HASHTABLE_FLAG_VERY_PEDANTIC );
    m_sessions = CS_HASHTABLE_STRING_VOID( 50, CS_HASHTABLE_FLAG_MUTEX|CS_HASHTABLE_FLAG_VERY_PEDANTIC );
    m_gameStorage = CS_storageOpen("GAMESINFO","file=secrets/games.sqlite",CS_STORAGE_BACKEND_SQLITE);
    m_reaperThread = CS_threadStart("Reaper",this,ColorRaceApplication::reaperThread);
}

ColorRaceApplication::~ColorRaceApplication() {
    CS_hashtableFree( m_games );
    CS_hashtableFree( m_sessions );

    CS_storageClose( m_gameStorage );
    CS_threadStop(m_reaperThread);
    CS_threadReturn(m_reaperThread);
}

bool ColorRaceApplication::sessionFilter( struct CS_ClientInfo *info ) {
    ColorRaceApplication *app = getInstance();
    if( !app ) return true;
    return app->appSessionFilter( info );
}

bool ColorRaceApplication::websocketHandler( struct CS_ClientInfo *info ) {
    ColorRaceApplication *app = getInstance();
    if( !app ) return true;
    return app->appWebsocketHandler(info);
}

bool ColorRaceApplication::logout( struct CS_ClientInfo *info ) {
    ColorRaceApplication *app = getInstance();
    if( !app ) return true;
    return app->appWebsocketHandler(info);
}

bool ColorRaceApplication::setCookie( struct CS_ClientInfo *info ) {
    ColorRaceApplication *app = getInstance();
    if( !app ) return true;
    return app->appSetCookie( info );
}

bool ColorRaceApplication::info( struct CS_ClientInfo *info ) {
    ColorRaceApplication *app = getInstance();
    if( !app ) return true;
    return app->appInfo( info );
}

ColorRaceApplication *ColorRaceApplication::getInstance() {
    if( globalInstance == NULL ) {
        globalInstance = new ColorRaceApplication();
    }    
    return globalInstance;
}

bool ColorRaceApplication::appSetCookie( struct CS_ClientInfo *info ) {
    struct CS_Reply *reply = CS_serverCreateReply(info,CS_RESPONSE_302,CS_MIME_HTML,NULL,0);
TRY_AGAIN:
    const char *key = CS_uuid4CstringTemp();
    const struct CS_String *keyString = CS_stringTempReferenceCstring(key,-1);
    const void *shouldBeNull = CS_hashtableGet( m_sessions, key );
    if( shouldBeNull != CS_HASHTABLE_ERROR && shouldBeNull != NULL ) {
        goto TRY_AGAIN;
    }
    UserSession *newSession = new UserSession(key);
    CS_hashtablePut( m_sessions, key, newSession ); 
    struct CS_String colorrace = CS_STRING("/colorrace");
    CS_serverSetReplyHeader( reply, &LOCATION, &colorrace );
    CS_serverSetReplyCookie( reply, &COLORRACE_SESSION_COOKIE, keyString, true, CS_REPLY_COOKIE_SAMESITE_LAX );
    CS_serverDoReply( info, reply );
    return true;
}

bool ColorRaceApplication::appInfo( struct CS_ClientInfo *info ) {
    struct CS_HtmlNode *root = CS_htmlCreateRoot( "html", 8192 );
    struct CS_HtmlNode *head = CS_htmlAddContainerAfter( root, "head" );
    struct CS_HtmlNode *node = CS_htmlAddContainerAfter( head, "meta" );
    CS_htmlAddAttribute( node, "charset", "utf-8" );
    node = CS_htmlAddContainerAfter( head, "title" );
    CS_htmlSetContents( node, "ColorRace server info", false );
    struct CS_HtmlNode *body = CS_htmlAddContainerAfter( root, "body" );
    CS_hashtableGrabMutex(m_games);
    CS_HASHTABLE_ITER( m_games, item ) {
        GameSession *session = (GameSession *)item->value;
        node = CS_htmlAddContainerAfter( body, "div" );
        CS_htmlSetContents( node, CS_tempBuffSnprintf(4096, "Session %s : %s :%s.", session->sessionName(), session->acceptingNewUsers()?"Accepting":"Not Accepting", session->getStateString() ), false );
        CS_htmlAddContainerAfter(body,"br");
    }
    CS_hashtableReleaseMutex(m_games);

    struct CS_StringBuilder *sb = CS_htmlToStringBuilder( root, 8192, false );
    struct CS_Reply *reply = CS_serverCreateReply( info, CS_RESPONSE_200, CS_MIME_HTML, CS_SB_buffer  ( sb ), CS_SB_size( sb ) );
    CS_serverDoReply( info, reply );
    CS_SB_free( sb );
    CS_htmlFree( root );
    return true;
}

bool ColorRaceApplication::appLogout( struct CS_ClientInfo *info ) {
    struct CS_Reply *reply = CS_serverCreateReply(info,CS_RESPONSE_302,CS_MIME_HTML,NULL,0);
    CS_serverSetReplyHeader( reply, &LOCATION, &WELCOME );
    CS_serverSetReplyCookie( reply, &COLORRACE_SESSION_COOKIE, &LOGGEDOUT, true, CS_REPLY_COOKIE_SAMESITE_LAX  );
    CS_serverDoReply( info, reply );
    return true;
}

bool ColorRaceApplication::appSessionFilter( struct CS_ClientInfo *info ) {
    const struct CS_String *cookieValueString = CS_serverGetRequestCookie( info, &COLORRACE_SESSION_COOKIE );
    if( cookieValueString != NULL ) {
        const char *cookieValue = CS_stringTempCstring(cookieValueString);
        const void *currentSession =
            CS_hashtableGet( m_sessions, cookieValue );
        if( currentSession != CS_HASHTABLE_ERROR && currentSession != NULL ) {
            return false;
        }
    }
    struct CS_Reply *reply = CS_serverCreateReply(info,CS_RESPONSE_302,CS_MIME_HTML,NULL,0);
    CS_serverSetReplyHeader( reply, &LOCATION, &WELCOME );
    CS_serverSetReplyCookie( reply, &COLORRACE_SESSION_COOKIE, &LOGGEDOUT, true, CS_REPLY_COOKIE_SAMESITE_LAX );
    CS_serverDoReply( info, reply );
    return true;
}

#define SEND_FRAME(__WS__,__FRAME__,__GOTO__) if((__FRAME__)==NULL||CS_WS_pushFrame(__WS__,__FRAME__,true)) { goto __GOTO__; }
bool ColorRaceApplication::appWebsocketHandler( struct CS_ClientInfo *info ) {
    if ( CS_WS_requestWantsWebsocket(info) ) {
        const struct CS_String *cookieValue = CS_serverGetRequestCookie( info, &COLORRACE_SESSION_COOKIE );
        if( cookieValue == NULL ) return true;
        const void *currentSession = CS_hashtableGet( m_sessions, CS_stringTempCstring(cookieValue) );
        struct CS_WebSocket *gws = CS_WS_create( info, NULL );
        struct CS_WebSocketFrame *returnFrame = NULL;
        UserSession *user = (UserSession *)currentSession;
        if( gws && user ) {
            user->setWebSocket( gws );
            struct CS_WebSocketFrame * nextFrame = NULL;
            while( true ) {
                nextFrame = CS_WS_nextIncomingFrame( gws );
                if( nextFrame == NULL ) break;
                switch( nextFrame->opcode ) {
                    //We must return a pong for any ping we get.
                    case CS_WS_OPCODE_PING:
                        returnFrame = CS_WS_createFrame( gws, CS_WS_OPCODE_PONG, false, nextFrame->payload, nextFrame->payloadLength );
                        SEND_FRAME( gws, returnFrame, ERROR_CLOSE );
                        returnFrame = NULL;
                        break;
                    case CS_WS_OPCODE_TEXT:
                        user->processFrame( nextFrame );
                        break;
                    case CS_WS_OPCODE_BINARY:
                        break;
                    default:
                        break;
                }
                CS_WS_returnFrame( gws, nextFrame );
            }
ERROR_CLOSE:
            user->removeWebSocket( gws );
            if( nextFrame ) CS_WS_returnFrame( gws, nextFrame );
            nextFrame = NULL;
            CS_WS_destroy( gws );
        }
    }
    return true;
};
