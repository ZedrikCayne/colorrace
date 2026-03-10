#ifndef __colorracedoth__
#define __colorracedoth__
#include <stdbool.h>

#include <crankshaft/hashtable.h>
#include <crankshaft/server.h>
#include <crankshaft/thread.h>

class GameSession;
class UserSession;

class ColorRaceApplication {
public:
    static bool sessionFilter( struct CS_ClientInfo *info );
    static bool websocketHandler( struct CS_ClientInfo *info );
    static bool setCookie( struct CS_ClientInfo *info );
    static bool logout( struct CS_ClientInfo *info );
    static bool info( struct CS_ClientInfo *info );
    static bool reaperThread( struct CS_Thread *thread, int threadState, void *context );

    static GameSession *getGameSession( UserSession *userState );

    static ColorRaceApplication *getInstance();

    static long long epochMillisecond();

private:
    static ColorRaceApplication *globalInstance;

    ColorRaceApplication( void );
    ~ColorRaceApplication( void );

    bool appSessionFilter( struct CS_ClientInfo *info );
    bool appWebsocketHandler( struct CS_ClientInfo *info );
    bool appSetCookie( struct CS_ClientInfo *info );
    bool appLogout( struct CS_ClientInfo *info );
    bool appInfo( struct CS_ClientInfo *info );
    bool appReaperThread( struct CS_Thread *thread, int threadState, void *context );
    GameSession *newGameSession( const char *name );
    GameSession *appGetGameSession( UserSession *forUser = NULL );

    struct CS_Mutex *m_gamesMutex;
    struct CS_HashTable *m_games;
    struct CS_HashTable *m_sessions;
    struct CS_Thread *m_reaperThread;
    const struct CS_Storage *m_gameStorage;
};

#endif
