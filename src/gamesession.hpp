#ifndef __gamesessiondoth__
#define __gamesessiondoth__
#include <stdbool.h>
#include <time.h>

#include <crankshaft/uuid.h>

class BoardState;
class UserSession;

struct LocalUserAndId {
    UserSession *session;
    struct CS_List *moves;
    bool abandoned;
    int finishedMS;
    int finishedMoves;
    int userId;
};

class GameSession {
public:
    GameSession( const char *name );
    ~GameSession();
    bool setOwner( UserSession *session );
    bool removeUserSession( UserSession *session );
    bool addUserSession( UserSession *session );
    bool userSetBoard( UserSession *session, const char *boardState, int length );
    bool userFinishRound( UserSession *session );
    bool acceptingNewUsers();
    bool checkAbandoned( UserSession *session );
    bool abandon( UserSession *session );
    bool start();
    bool gameThread( int threadState );
    bool writeChanges();
    const char *boardState();
    void unlockBoardState();
    const char *name();
    const char *getStateString();
    bool okToSendState();
    bool broadcastBoardStates();
    bool broadcastGameState();
    int getId( UserSession *session );

    void logboard();

    bool reapMe();

    const char *sessionName();

    const static int MAX_NAME_LENGTH = 60;

private:
    bool m_reap;
    int m_state;
    int m_ids;
    int m_winnerCount;
    int m_lastWinnerCount;
    struct CS_Mutex *m_idsMutex;
    char m_name[ MAX_NAME_LENGTH + 4 ];
    BoardState *m_currentBoard;

    int nextId();

    char m_ownerUuid[ UUID_CHAR_SIZE_BYTES ];
    UserSession *m_owner;

    bool setState( int gameState );
    bool broadcast( const char *what, const char *value );
    bool anyWon();
    bool allWon();
    bool allDisconnected();

    struct LocalUserAndId *getLocalUserForUserSession( UserSession *userState );

    time_t m_lastStateChange;
    time_t m_created;
    time_t m_ownerLast;

    struct CS_Mutex *m_mutex;
    struct CS_Thread *m_thread;
    struct CS_HashTable *m_users;
    struct CS_SlabAllocator *m_pairSlabs;

    struct CS_JsonNode *m_broadcastJSON;
    struct CS_StringBuilder *m_broadcastSB;
};

#endif
