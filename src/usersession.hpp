#ifndef __usersessiondoth__
#define __usersessiondoth__
#include <stdbool.h>

#include <crankshaft/mutex.h>
#include <crankshaft/thread.h>
#include <crankshaft/websocket.h>

#include "boardstate.hpp"
#include "gamesession.hpp"

class UserSession {
public:
    UserSession(const char *sessionKey);
    ~UserSession();

    bool processFrame( struct CS_WebSocketFrame *frame );
    bool setWebSocket( struct CS_WebSocket *ws );
    bool removeWebSocket( struct CS_WebSocket *ws );
    bool boardStartingStatesMatch( BoardState *otherState );
    const char *sessionKey();
    bool sendThing( const void *buffer, int length );
    bool sendJson( struct CS_JsonNode *sendThis );
    bool dirty();
    const char *boardState();
    void unlockBoardState();
    int numberMatched();
    int moveCount();
    int tileCount();
    int currentMs();
    int startedMs();
    bool hasFinished();
    void kick();
    bool sendBoardState(void);

private:
    bool setTLA( const char *tla );
    bool touch(int row, int column);
    bool nextPuzzle(void);
    bool specificPuzzle(const char *puzzleDefinition );
    bool joinGameSession(const char *gameSessionName);
    bool createSession( const char *name );
    bool connect(void);
    bool next(void);
    bool abandon(void);
    bool sendServerIssue(const char *what);
    bool sendWait();

    char m_TLA[3];
    char *m_sessionKey;
    struct CS_Mutex *m_mutex;
    struct CS_WebSocket *m_ws;
    struct CS_JsonNode *m_json;
    struct CS_StringBuilder *m_sb;
    BoardState *m_board;
    GameSession *m_game;
    int m_idForGame;

    time_t m_started;
    
    time_t m_last;
};

#endif
