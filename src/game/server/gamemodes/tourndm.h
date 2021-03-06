#ifndef TOURNDM_H
#define TOURNDM_H
#include <game/server/gamecontroller.h>


// class that handles the overall tourney and fights
class CGameControllerTournDM : public IGameController
{
public:
    CGameControllerTournDM(class CGameContext *pGameServer, int Subtype);
    virtual ~CGameControllerTournDM();
    virtual void Tick();
    virtual void PostReset();

    virtual bool CanSpawn(int Team, vec2 *pOutPos, int CID = -1);
    virtual float EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos, int CID = -1);
    virtual void EvaluateSpawnType(CSpawnEval *pEval, int Type, int CID = -1);
    virtual bool OnEntity(int Index, vec2 Pos);
    virtual void OnPlayerLeave(int CID);
    virtual void OnCharacterSpawn(class CCharacter *pChr);
    virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
    virtual void DoWincheck();
    virtual void StartRound();
    virtual void EndRound();
    virtual void OnPlayerInfoChange(class CPlayer *pP);

    virtual void Snap(int SnappingClient);

    void HandleBracket();
    void HandleOddPlayers();
    void StartTourney();

    void AddTourneyState(char* Name, int size);

    // puts the players into tourney
    void SignIn(int CID);
    bool ChangeArena(int CID, int ID);

    class CGameControllerArena* Arena(int ID) { return m_apArenas[ID]; }

    static void ConArenaColor(IConsole::IResult *pResult, void *pUserData);

    enum
    {
        NUM_ARENAS=8
    };

    bool m_TourneyStarted;
    int m_NumMatches;
    int m_NumParticipants;
    int m_NumActiveParticipants; // those who still can win

    class CPlayer* m_apBracketPlayers[16];

    // players tourney information
    struct CTInfo
    {
        int m_TourneyState;
        int m_Victories;
        int m_Losses;
    }m_aTPInfo[16]; // for normal Playerorder

    CTInfo* m_apTBInfo[16]; // bracket order

private:

    void UpdateArenaStates();

    // -1: no arena/spec/standard spawn | 0-7: arenas with their ID
    class CGameControllerArena* m_apArenas[NUM_ARENAS];

    // stores wether arena can be used at all
    bool m_aActiveArenas[NUM_ARENAS];

    int m_NumActiveArenas;

    // stores g_Config.m_SvArenas
    bool m_OldArenaMode;

    // Joincounter to determine TID
    int m_JoinCount;

    // wether odd tees are handled atm
    bool m_HandlingOdds;

    // stores g_Config.m_SvBracket for every tourney
    int m_BracketMode;
};


// /////////////////////////////////////////// //


// class that handles the fighting tees in their arenas
class CGameControllerArena
{
    friend class CGameControllerTournDM;
    typedef CGameControllerTournDM::CSpawnEval CSpawnEval;


    class CGameContext *m_pGameServer;
    class IServer *m_pServer;
    CGameControllerTournDM *m_pController;

    CGameContext *GameServer() const { return m_pGameServer; }
    IServer *Server() const { return m_pServer; }
    CGameControllerTournDM *Controller() { return m_pController; }

protected:
    int m_RoundStartTick;
    int m_GameOverTick;
    int m_SuddenDeath;

public:
    CGameControllerArena(class CGameControllerTournDM *pController);

    void Tick();

    bool IsGameOver() const { return m_GameOverTick != -1; }
    void DoWincheck();
    void DoWarmup(int Seconds);

    void StartRound();
    void EndRound(int winnerID, bool Left = false);

    void StartFight();

    void OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon);
    void OnPlayerEnter(int CID);
    void OnPlayerLeave(int CID);
    int OnPlayerArenaLeave(int CID); // returns m_apOpponents-ID from Arena

    void EvaluateSpawnType(CSpawnEval *pEval, int Type, int CID);

    void PostReset();

    void ResetGame() { m_ResetRequested = true; }
    bool m_ResetRequested;

    bool m_Paused;
    int m_Warmup;

    int m_NumPlayers;

    bool m_TourneyStarted;

    bool m_RoundRunning; // wether tees are fighting here

    class CPlayer* m_apOpponents[2];

    enum
    {
        MAX_OPPONENTS = 2
    };

};
#endif // TOURNDM_H
