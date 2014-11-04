#include <engine/shared/config.h>

#include <game/server/entities/pickup.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "tourndm.h"

CGameControllerTournDM::CGameControllerTournDM(class CGameContext *pGameServer)
: IGameController(pGameServer)
{
    m_pGameType = "TournDM";
    m_GameType = GAMETYPE_TOURNDM;

    m_OldArenaMode = g_Config.m_SvArenas;

    m_Warmup = Server()->TickSpeed()*g_Config.m_SvStartWarmUp;

    m_NumParticipants = 0;
    m_NumActiveArenas = 0;

    // every arena gets an own controller
    // create always 8 in case someone changes settings...
    for(int i = 0; i < NUM_ARENAS; i++)
        m_apArenas[i] = new CGameControllerArena(this);

    m_aNumSpawnPoints[3] = 0;
    m_aNumSpawnPoints[4] = 0;
    m_aNumSpawnPoints[5] = 0;
    m_aNumSpawnPoints[6] = 0;
    m_aNumSpawnPoints[7] = 0;
    m_aNumSpawnPoints[8] = 0;
}

void CGameControllerTournDM::UpdateArenaStates()
{
    m_NumActiveArenas = 0;

    for(int i = 0; i < NUM_ARENAS; i++)
    {
        if(g_Config.m_SvArenas)
        {
            if(m_aNumSpawnPoints[i + 1])
                m_aActiveArenas[i] = true;
            else
                m_aActiveArenas[i] = false;
        }
        else
            m_aActiveArenas[i] = true;

        m_NumActiveArenas += m_aActiveArenas[i];
    }
}

void CGameControllerTournDM::SignIn(int CID)
{
    // joined already
    if(GameServer()->m_apPlayers[CID]->m_Arena != -1)
        return;

    if(!m_Warmup)
    {
        GameServer()->SendChatTarget(CID, "You need to wait until the tournament is over");
        return;
    }

    for(int i = 0; i < NUM_ARENAS; i++)
        if(ChangeArena(CID, i))
        {
            m_NumParticipants++;
            char aBuf[128];
            str_format(aBuf, sizeof(aBuf), "'%s' joined the tournament !", Server()->ClientName(CID));
            GameServer()->SendChatTarget(-1, aBuf);
            GameServer()->m_apPlayers[CID]->m_Score = 0;
            if(m_NumParticipants == 2)
            {
                GameServer()->SendChatTarget(-1, "You can join the tourney until warmup is over");
                GameServer()->SendChatTarget(-1, "GO GO GO !!!");
            }
            return;
        }
}

bool CGameControllerTournDM::ChangeArena(int CID, int ID)
{
    if(!GameServer()->m_apPlayers[CID])
        return false;

    if(ID != -1 && (!m_aActiveArenas[ID] || m_apArenas[ID]->m_NumPlayers >= CGameControllerArena::MAX_OPPONENTS))
        return false;

    if(ID == GameServer()->m_apPlayers[CID]->m_Arena)
        return false;

    // player left tourney
    if(ID == -1)
        OnPlayerLeave(CID);

    if(GameServer()->m_apPlayers[CID]->m_Arena != -1)
        Arena(GameServer()->m_apPlayers[CID]->m_Arena)->OnPlayerLeave(CID);

    GameServer()->m_apPlayers[CID]->m_Arena = ID;
    GameServer()->m_apPlayers[CID]->SetTeam(0, false);
    GameServer()->m_apPlayers[CID]->KillCharacter();

    if(ID != -1)
        Arena(ID)->OnPlayerEnter(CID);

    OnPlayerInfoChange(GameServer()->m_apPlayers[CID]);

    return true;
}

void CGameControllerTournDM::Tick()
{
    CycleMap();

    if(m_OldArenaMode != g_Config.m_SvArenas)
    {
        // force a reload otherwise entities are fucked
        GameServer()->Console()->ExecuteLine("reload");
    }

    for(int i = 0; i < NUM_ARENAS; i++)
    {
        m_apArenas[i]->Tick();
        GameServer()->m_World.m_ArenaResetRequested[i] = Arena(i)->m_ResetRequested;
        GameServer()->m_World.m_ArenaPaused[i] = Arena(i)->m_Paused;
    }

    // do warmup
    if(m_Warmup)
    {
        if(!GameServer()->m_World.m_Paused)
        {
            if(m_NumParticipants >= 2)
                m_Warmup--;
            else
                m_Warmup = Server()->TickSpeed()*g_Config.m_SvStartWarmUp;
            if(!m_Warmup)
                StartTourney();
        }
    }
    else
    {
        int WaitingID = -1;
        for(int Victories = 1; Victories <= 4; Victories++)
        {
            for(int i = 0; i < NUM_ARENAS; i++)
                if(Arena(i)->m_NumPlayers == 1 && !Arena(i)->m_RoundRunning && !Arena(i)->m_ResetRequested && !Arena(i)->m_Paused)
                {
                    int PlayerID = Arena(i)->m_apOpponents[0] ? Arena(i)->m_apOpponents[0]->GetCID() : Arena(i)->m_apOpponents[1]->GetCID();
                    if(GameServer()->m_apPlayers[PlayerID]->m_Victories == Victories)
                    {
                        if(WaitingID == -1)
                            WaitingID = PlayerID;
                        else
                        {
                            ChangeArena(PlayerID, GameServer()->m_apPlayers[WaitingID]->m_Arena);
                            WaitingID = -1;
                        }
                    }
                }
        }
    }

    if(m_GameOverTick != -1)
    {
        // game over.. wait for restart
        if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*10)
        {
            StartRound();
            m_RoundCount++;
        }
    }

    // game is Paused
    if(GameServer()->m_World.m_Paused)
        ++m_RoundStartTick;

    DoWincheck();

    if(Arena(0)->m_NumPlayers == 2 && Arena(0)->m_apOpponents[0] == 0)
    {
        GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Arena", "Something went very wrong :/");
    }
}

bool CGameControllerTournDM::CanSpawn(int Team, vec2 *pOutPos, int CID)
{
    CSpawnEval Eval;

    // spectators can't spawn
    if(Team == TEAM_SPECTATORS)
        return false;

    if(!g_Config.m_SvArenas && GameServer()->m_apPlayers[CID]->m_Arena == -1)
    {
        GameServer()->m_apPlayers[CID]->SetTeam(TEAM_SPECTATORS, false);
        return false;
    }

    if(!g_Config.m_SvArenas)
        EvaluateSpawnType(&Eval, 0, CID);
    else
        EvaluateSpawnType(&Eval, GameServer()->m_apPlayers[CID]->m_Arena + 1, CID);


    *pOutPos = Eval.m_Pos;
    return Eval.m_Got;
}

float CGameControllerTournDM::EvaluateSpawnPos(CSpawnEval *pEval, vec2 Pos, int CID)
{

    float Score = 0.0f;
    CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
    for(; pC; pC = (CCharacter *)pC->TypeNext())
    {
        if(pC->GetPlayer()->m_Arena != GameServer()->m_apPlayers[CID]->m_Arena)
            continue;
        float d = distance(Pos, pC->m_Pos);
        Score += (d == 0 ? 1000000000.0f : 1.0f/d);
    }

    return Score;

}

void CGameControllerTournDM::EvaluateSpawnType(CSpawnEval *pEval, int Type, int CID)
{
    // get spawn point
    for(int i = 0; i < m_aNumSpawnPoints[Type]; i++)
    {
        // check if the position is occupado
        CCharacter *aEnts[MAX_CLIENTS];
        int Num = GameServer()->m_World.FindEntities(m_aaSpawnPoints[Type][i], 64, (CEntity**)aEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
        vec2 Positions[5] = { vec2(0.0f, 0.0f), vec2(-32.0f, 0.0f), vec2(0.0f, -32.0f), vec2(32.0f, 0.0f), vec2(0.0f, 32.0f) };	// start, left, up, right, down
        int Result = -1;
        for(int Index = 0; Index < 5 && Result == -1; ++Index)
        {
            Result = Index;
            for(int c = 0; c < Num; ++c)
                if(((CCharacter*)aEnts[c])->GetPlayer()->m_Arena == GameServer()->m_apPlayers[CID]->m_Arena &&
                        (GameServer()->Collision()->CheckPoint(m_aaSpawnPoints[Type][i]+Positions[Index]) ||
                            distance(aEnts[c]->m_Pos, m_aaSpawnPoints[Type][i]+Positions[Index]) <= aEnts[c]->m_ProximityRadius))
                {
                    Result = -1;
                    break;
                }
        }
        if(Result == -1)
            continue;	// try next spawn point

        vec2 P = m_aaSpawnPoints[Type][i]+Positions[Result];
        float S = EvaluateSpawnPos(pEval, P, CID);
        if(!pEval->m_Got || pEval->m_Score > S)
        {
            pEval->m_Got = true;
            pEval->m_Score = S;
            pEval->m_Pos = P;
        }
    }
}

bool CGameControllerTournDM::OnEntity(int Index, vec2 Pos)
{
    int Type = -1;
    int SubType = 0;

    if(Index == ENTITY_SPAWN)
        m_aaSpawnPoints[0][m_aNumSpawnPoints[0]++] = Pos;
    else if(Index == ENTITY_SPAWN_0)
        m_aaSpawnPoints[1][m_aNumSpawnPoints[1]++] = Pos;
    else if(Index == ENTITY_SPAWN_1)
        m_aaSpawnPoints[2][m_aNumSpawnPoints[2]++] = Pos;
    else if(Index == ENTITY_SPAWN_2)
        m_aaSpawnPoints[3][m_aNumSpawnPoints[3]++] = Pos;
    else if(Index == ENTITY_SPAWN_3)
        m_aaSpawnPoints[4][m_aNumSpawnPoints[4]++] = Pos;
    else if(Index == ENTITY_SPAWN_4)
        m_aaSpawnPoints[5][m_aNumSpawnPoints[5]++] = Pos;
    else if(Index == ENTITY_SPAWN_5)
        m_aaSpawnPoints[6][m_aNumSpawnPoints[6]++] = Pos;
    else if(Index == ENTITY_SPAWN_6)
        m_aaSpawnPoints[7][m_aNumSpawnPoints[7]++] = Pos;
    else if(Index == ENTITY_SPAWN_7)
        m_aaSpawnPoints[8][m_aNumSpawnPoints[8]++] = Pos;
    else if(Index == ENTITY_ARMOR)
        Type = POWERUP_ARMOR;
    else if(Index == ENTITY_HEALTH)
        Type = POWERUP_HEALTH;
    else if(Index == ENTITY_WEAPON_SHOTGUN)
    {
        Type = POWERUP_WEAPON;
        SubType = WEAPON_SHOTGUN;
    }
    else if(Index == ENTITY_WEAPON_GRENADE)
    {
        Type = POWERUP_WEAPON;
        SubType = WEAPON_GRENADE;
    }
    else if(Index == ENTITY_WEAPON_RIFLE)
    {
        Type = POWERUP_WEAPON;
        SubType = WEAPON_RIFLE;
    }
    else if(Index == ENTITY_POWERUP_NINJA && g_Config.m_SvPowerups)
    {
        Type = POWERUP_NINJA;
        SubType = WEAPON_NINJA;
    }

    if(Type != -1)
    {
        if(g_Config.m_SvArenas)
        {
            CPickup *pPickup = new CPickup(&GameServer()->m_World, Type, SubType, -2);
            pPickup->m_Pos = Pos;
        }
        else
        {
            for(int i = 0; i < 8; i++)
            {
                CPickup *pPickup = new CPickup(&GameServer()->m_World, Type, SubType, i);
                pPickup->m_Pos = Pos;
            }
        }
        UpdateArenaStates();
        return true;
    }

    UpdateArenaStates();
    return false;
}

void CGameControllerTournDM::OnPlayerLeave(int CID)
{
    if(GameServer()->m_apPlayers[CID]->m_Arena != -1)
        m_NumParticipants--;
}

int CGameControllerTournDM::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
    // do scoreing
    if(!pKiller || Weapon == WEAPON_GAME || pKiller->m_Arena != pVictim->GetPlayer()->m_Arena)
        return 0;

    if(pVictim->GetPlayer()->m_Arena != -1)
        Arena(pVictim->GetPlayer()->m_Arena)->OnCharacterDeath(pVictim, pKiller, Weapon);

    return 0;
}

void CGameControllerTournDM::OnCharacterSpawn(CCharacter *pChr)
{
    IGameController::OnCharacterSpawn(pChr);
    if(pChr->m_Arena == -1)
        pChr->GetPlayer()->m_Score = -100;
}

void CGameControllerTournDM::DoWincheck()
{
    if(m_GameOverTick == -1 && !m_Warmup && !GameServer()->m_World.m_ResetRequested)
    {
       if(m_NumParticipants == 1)
       {
           int P = -1;
           for(int i = 0; i < MAX_CLIENTS; i++)
               if(GameServer()->m_apPlayers[i]->m_Arena != -1)
               {
                   P = i;
                   break;
               }

           if(P != -1)
           {
               char aBuf[256];
               str_format(aBuf, sizeof(aBuf), "'%s' wins the tournament !", Server()->ClientName(P));
               GameServer()->SendChatTarget(-1, aBuf);
               GameServer()->SendChatTarget(-1, "Congratulations !");
           }
           EndRound();
       }
    }
}

void CGameControllerTournDM::StartTourney()
{
    ResetGame();

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(GameServer()->m_apPlayers[i])
        {
            // reset tourney stuff
            GameServer()->m_apPlayers[i]->m_Victories = 0;
            GameServer()->m_apPlayers[i]->m_Losses = 0;
        }
    }

    for(int i = 0; i < NUM_ARENAS; i++)
    {
        Arena(i)->m_TourneyStarted = true;
        Arena(i)->m_RoundRunning = true;
        Arena(i)->m_Warmup = Server()->TickSpeed()*g_Config.m_SvRoundWarmUp;
    }
}

void CGameControllerTournDM::StartRound()
{
    ResetGame();

    m_RoundStartTick = Server()->Tick();
    m_SuddenDeath = 0;
    m_GameOverTick = -1;

    GameServer()->m_World.m_Paused = false;
    m_aTeamscore[TEAM_RED] = 0;
    m_aTeamscore[TEAM_BLUE] = 0;
    m_ForceBalanced = false;
    m_Warmup = Server()->TickSpeed()*g_Config.m_SvStartWarmUp;

    for(int i = 0; i < NUM_ARENAS; i++)
        Arena(i)->m_TourneyStarted = false;

    Server()->DemoRecorder_HandleAutoStart();
    char aBuf[256];
    str_format(aBuf, sizeof(aBuf), "start round type='%s' teamplay='%d'", m_pGameType, m_GameFlags&GAMEFLAG_TEAMS);
    GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
}

void CGameControllerTournDM::EndRound()
{
    if(m_Warmup) // game can't end when we are running warmup
        return;

    GameServer()->m_World.m_Paused = true;
    m_GameOverTick = Server()->Tick();
    m_SuddenDeath = 0;

    for(int i = 0; i < MAX_CLIENTS; i++)
        if(GameServer()->m_apPlayers[i])
            ChangeArena(i, -1);
}

void CGameControllerTournDM::PostReset()
{
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(GameServer()->m_apPlayers[i])
        {
            GameServer()->m_apPlayers[i]->Respawn();
            GameServer()->m_apPlayers[i]->m_ScoreStartTick = Server()->Tick();
            GameServer()->m_apPlayers[i]->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
        }
    } 
}

CGameControllerTournDM::~CGameControllerTournDM()
{
    for(int i = 0; i < NUM_ARENAS; i++)
        delete m_apArenas[i];
}

void CGameControllerTournDM::OnPlayerInfoChange(class CPlayer *pP)
{
    if(!g_Config.m_SvColorize)
        return;

    if(pP->m_Arena == -1)
    {
        pP->m_TeeInfos.m_UseCustomColor = 0;
        return;
    }

                      //black // white // red // orange // green // light blue // blue // violett
    const int aArenaColors[8] = {0, 255, 65387, 2031418, 5504826, 8126266, 10223467, 12582714};

    pP->m_TeeInfos.m_UseCustomColor = 1;
    pP->m_TeeInfos.m_ColorBody = aArenaColors[pP->m_Arena];
    pP->m_TeeInfos.m_ColorFeet = aArenaColors[pP->m_Arena];
}

void CGameControllerTournDM::Snap(int SnappingClient)
{
    CNetObj_GameInfo *pGameInfoObj = (CNetObj_GameInfo *)Server()->SnapNewItem(NETOBJTYPE_GAMEINFO, 0, sizeof(CNetObj_GameInfo));
    if(!pGameInfoObj)
        return;

    pGameInfoObj->m_GameFlags = m_GameFlags;
    pGameInfoObj->m_GameStateFlags = 0;

    CPlayer* pPSnap = GameServer()->m_apPlayers[SnappingClient];

    if(pPSnap->GetTeam() == TEAM_SPECTATORS || pPSnap->m_Arena == -1)
    {
        if(pPSnap->m_SpectatorID != SPEC_FREEVIEW && GameServer()->m_apPlayers[pPSnap->m_SpectatorID] && pPSnap->GetTeam() == TEAM_SPECTATORS && GameServer()->m_apPlayers[pPSnap->m_SpectatorID]->m_Arena != -1)
        {
            int ArenaID = GameServer()->m_apPlayers[pPSnap->m_SpectatorID]->m_Arena;
            if(Arena(ArenaID)->m_GameOverTick != -1)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
            if(Arena(ArenaID)->m_SuddenDeath)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
            if(Arena(ArenaID)->m_Paused || GameServer()->m_World.m_Paused)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
            pGameInfoObj->m_RoundStartTick = Arena(ArenaID)->m_RoundStartTick;
            pGameInfoObj->m_WarmupTimer = Arena(ArenaID)->m_Warmup;
            pGameInfoObj->m_TimeLimit = g_Config.m_SvTimelimit;
        }
        else
        {
            if(m_GameOverTick != -1)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
            if(m_SuddenDeath)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
            if(GameServer()->m_World.m_Paused)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
            pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
            pGameInfoObj->m_WarmupTimer = m_Warmup;
            pGameInfoObj->m_TimeLimit = 0;
        }
    }
    else
    {
        int ArenaID = pPSnap->m_Arena;
        if(Arena(ArenaID)->m_GameOverTick != -1)
            pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
        if(Arena(ArenaID)->m_SuddenDeath)
            pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
        if(Arena(ArenaID)->m_Paused || GameServer()->m_World.m_Paused)
            pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
        pGameInfoObj->m_RoundStartTick = Arena(ArenaID)->m_RoundStartTick;
        pGameInfoObj->m_WarmupTimer = Arena(ArenaID)->m_Warmup;
        pGameInfoObj->m_TimeLimit = g_Config.m_SvTimelimit;
    }

    pGameInfoObj->m_ScoreLimit = g_Config.m_SvScorelimit;

    pGameInfoObj->m_RoundNum = (str_length(g_Config.m_SvMaprotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
    pGameInfoObj->m_RoundCurrent = m_RoundCount+1;
}


// /////////////////////////////////////////////////////////////// //


CGameControllerArena::CGameControllerArena(CGameControllerTournDM *pController)
{
    m_pController = pController;
    m_pGameServer = pController->GameServer();
    m_pServer = pController->Server();

    for(int i = 0; i < MAX_OPPONENTS; i++)
        m_apOpponents[i] = 0;

    m_NumPlayers = 0;
    m_ResetRequested = false;
    m_Paused = false;
    m_RoundRunning = false;
    m_Winner = 0;

    m_GameOverTick = -1;
    m_SuddenDeath = 0;
    m_RoundStartTick = Server()->Tick();

    m_TourneyStarted = false;

    m_Warmup = Server()->TickSpeed()*g_Config.m_SvRoundWarmUp;
}

void CGameControllerArena::OnPlayerEnter(int CID)
{
    if(m_NumPlayers < MAX_OPPONENTS)
    {
        m_NumPlayers++;
        if(m_apOpponents[0])
            m_apOpponents[1] = GameServer()->m_apPlayers[CID];
        else
            m_apOpponents[0] = GameServer()->m_apPlayers[CID];
    }
}

void CGameControllerArena::OnPlayerLeave(int CID)
{ 
    for(int i = 0; i < MAX_OPPONENTS; i++)
        if(m_apOpponents[i] && m_apOpponents[i]->GetCID() == CID)
        {
            m_apOpponents[i] = 0;
            m_NumPlayers--;
            return;
        }
}

void CGameControllerArena::OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int Weapon)
{
    if(pKiller == pVictim->GetPlayer())
        pVictim->GetPlayer()->m_Score--; // suicide
    else
            pKiller->m_Score++; // normal kill

    if(Weapon == WEAPON_SELF)
        pVictim->GetPlayer()->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()*3.0f;
}

void CGameControllerArena::DoWincheck()
{
    if(m_NumPlayers == 1)
        for(int i = 0; i < MAX_OPPONENTS; i++)
            if(m_apOpponents[i])
                m_Winner = i;

    if(m_GameOverTick == -1 && !m_Warmup && !m_ResetRequested)
    {
        // gather some stats
        int Topscore = 0;
        int TopscoreCount = 0;
        for(int i = 0; i < MAX_OPPONENTS; i++)
        {
            if(m_apOpponents[i])
            {
                if(m_apOpponents[i]->m_Score > Topscore)
                {
                    Topscore = m_apOpponents[i]->m_Score;
                    TopscoreCount = 1;
                    m_Winner = i;
                }
                else if(m_apOpponents[i]->m_Score == Topscore)
                    TopscoreCount++;
            }
        }

        // check score win condition
        if((g_Config.m_SvScorelimit > 0 && Topscore >= g_Config.m_SvScorelimit) ||
            (g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60))
        {
            if(TopscoreCount == 1)
            {
                m_apOpponents[m_Winner]->m_Victories++;
                for(int i = 0; i < MAX_OPPONENTS; i++)
                    if(i != m_Winner && m_apOpponents[i])
                        m_apOpponents[i]->m_Losses++;

                EndRound();
            }
            else
                m_SuddenDeath = 1;
        }
    }
    if(m_RoundRunning && m_NumPlayers == 1)
    {
        for(int i = 0; i < MAX_OPPONENTS; i++)
            if(m_apOpponents[i])
            {
                m_Winner = i;
                m_apOpponents[m_Winner]->m_Victories++;
                EndRound();
            }
    }
}

void CGameControllerArena::StartRound()
{
    ResetGame();

    m_RoundStartTick = Server()->Tick();
    m_SuddenDeath = 0;
    m_GameOverTick = -1;
    m_Paused = false;
    m_Warmup = Server()->TickSpeed()*g_Config.m_SvRoundWarmUp;
}

void CGameControllerArena::StartFight()
{
    ResetGame();

    m_RoundStartTick = Server()->Tick();
    m_SuddenDeath = 0;
    m_GameOverTick = -1;
    m_Paused = false;

    for(int i = 0; i < MAX_OPPONENTS; i++)
        if(m_apOpponents[i])
            m_apOpponents[i]->SetTeam(0, false);

    m_RoundRunning = true;
}

void CGameControllerArena::EndRound()
{
    if(m_Warmup && !m_RoundRunning) // game can't end when we are running warmup
        return;

    m_RoundRunning = false;

    char aBuf[256];
    if(m_apOpponents[0] && m_apOpponents[1])
        str_format(aBuf, sizeof(aBuf), "'%s' defeated '%s' !", Server()->ClientName(m_apOpponents[m_Winner]->GetCID()), Server()->ClientName(m_apOpponents[!m_Winner]->GetCID()));
    else if(m_apOpponents[m_Winner])
        str_format(aBuf, sizeof(aBuf), "'%s' won the round !", Server()->ClientName(m_apOpponents[m_Winner]->GetCID()));

    if(m_apOpponents[0] || m_apOpponents[1])
        GameServer()->SendChatTarget(-1, aBuf);

    m_Paused = true;
    m_GameOverTick = Server()->Tick();
    m_SuddenDeath = 0;
}

void CGameControllerArena::PostReset()
{
    for(int i = 0; i < MAX_OPPONENTS; i++)
        if(m_apOpponents[i])
        {
            m_apOpponents[i]->Respawn();
            m_apOpponents[i]->m_Score = 0;
            m_apOpponents[i]->m_ScoreStartTick = Server()->Tick();
            m_apOpponents[i]->m_RespawnTick = Server()->Tick()+Server()->TickSpeed()/2;
        }
}

void CGameControllerArena::Tick()
{
    if(!m_TourneyStarted)
        m_Warmup = m_pController->m_Warmup;

    // do warmup
    if(m_Warmup && !m_Paused && !GameServer()->m_World.m_Paused && m_TourneyStarted)
    {
        if(m_NumPlayers >= 2)
            m_Warmup--;
        else
            m_Warmup = Server()->TickSpeed()*g_Config.m_SvRoundWarmUp;
        if(!m_Warmup)
            StartFight();
    }

    // voted to spec -> disqualified
    if(!m_Warmup)
        for(int i = 0; i < MAX_OPPONENTS; i++)
            if(m_apOpponents[i] && m_apOpponents[i]->GetTeam() == TEAM_SPECTATORS)
                m_pController->ChangeArena(m_apOpponents[i]->GetCID(), -1);

    if(m_GameOverTick != -1)
    {
        // game over.. wait for restart
        if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*10)
        {
            for(int i = 0; i < MAX_OPPONENTS; i++)
                if(m_apOpponents[i] && i != m_Winner)
                {
                    m_pController->ChangeArena(m_apOpponents[i]->GetCID(), -1);
                }
            StartRound();
        }
    }

    // game is Paused
    if(m_Paused || GameServer()->m_World.m_Paused)
        ++m_RoundStartTick;

    DoWincheck();
}