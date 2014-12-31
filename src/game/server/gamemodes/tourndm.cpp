#include <engine/shared/config.h>

#include <game/server/entities/pickup.h>
#include <game/server/entities/character.h>
#include <game/server/player.h>
#include <game/server/gamecontext.h>
#include <game/mapitems.h>

#include "tourndm.h"

enum
{
    TOURN_SPECTATING=0,
    TOURN_PARTICIPATING=1,
    TOURN_WAITING=2,
    TOURN_DEFEATED=4
};

CGameControllerTournDM::CGameControllerTournDM(class CGameContext *pGameServer, int Subtype)
: IGameController(pGameServer)
{
    m_GameType = GAMETYPE_TOURNDM;
    m_SubType = Subtype;

    switch (m_SubType)
    {
    case SUBTYPE_VANILLA:
        m_pGameType = "TournDM";
        break;
    case SUBTYPE_INSTAGIB:
        m_pGameType = "iTournDM";
        break;
    case SUBTYPE_GRENADE:
        m_pGameType = "gTournDM";
        break;
    default:
    {
        m_pGameType = "TournDM";
        m_SubType = SUBTYPE_VANILLA;
    }
    }

    m_OldArenaMode = g_Config.m_SvArenas;

    m_Warmup = Server()->TickSpeed()*g_Config.m_SvStartWarmUp;

    m_NumMatches = 0;
    m_JoinCount = 0;
    m_NumParticipants = 0;
    m_NumActiveParticipants = 0;
    m_NumActiveArenas = 0;
    m_TourneyStarted = false;
    m_HandlingOdds = false;

    // every arena gets an own controller
    // create always 8 in case someone changes settings...
    for(int i = 0; i < NUM_ARENAS; i++)
        m_apArenas[i] = new CGameControllerArena(this);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        m_apBracketPlayers[i] = 0;
        m_aTPInfo[i].m_TourneyState = 0;
        m_aTPInfo[i].m_Victories = 0;
        m_aTPInfo[i].m_Losses = 0;
        m_apTBInfo[i] = 0;
    }

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
    if(GameServer()->m_apPlayers[CID]->m_Participating)
        return;

    if(!m_Warmup)
    {
        GameServer()->SendChatTarget(CID, "You need to wait until the tournament is over");
        return;
    }

    if(m_NumParticipants >= m_NumActiveArenas*2)
    {
        GameServer()->SendChatTarget(CID, "You can't join, all arenas are full");
        return;
    }

    m_NumParticipants++;
    char aBuf[128];
    str_format(aBuf, sizeof(aBuf), "'%s' joined the tournament !", Server()->ClientName(CID));
    GameServer()->SendChatTarget(-1, aBuf);
    GameServer()->m_apPlayers[CID]->m_Score = 0;

    // TID is gonna be corrected at tourney start, this here is only to save the join order
    GameServer()->m_apPlayers[CID]->m_TID = m_JoinCount;
    m_JoinCount++;

    GameServer()->m_apPlayers[CID]->m_Participating = true;
    if(m_NumParticipants == 2)
    {
        GameServer()->SendChatTarget(-1, "You can join the tourney until warmup is over");
        GameServer()->SendChatTarget(-1, "GO GO GO !!!");
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

    if(GameServer()->m_apPlayers[CID]->m_Arena != -1)
        Arena(GameServer()->m_apPlayers[CID]->m_Arena)->OnPlayerArenaLeave(CID);

    GameServer()->m_apPlayers[CID]->m_Arena = ID;

    if(!g_Config.m_SvArenas)
    {
         if(ID == -1)
             GameServer()->m_apPlayers[CID]->SetTeam(-1, false);
         else if(GameServer()->m_apPlayers[CID]->GetTeam() == -1)
             GameServer()->m_apPlayers[CID]->SetTeam(0, false);
         else
             GameServer()->m_apPlayers[CID]->KillCharacter();
    }
    else
    {
        if(GameServer()->m_apPlayers[CID]->GetTeam() == -1)
            GameServer()->m_apPlayers[CID]->SetTeam(0, false);
        else
            GameServer()->m_apPlayers[CID]->KillCharacter();
    }

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
        GameServer()->m_World.m_ArenaResetRequested[i] = Arena(i)->m_ResetRequested;
        GameServer()->m_World.m_ArenaPaused[i] = Arena(i)->m_Paused;

        // no players ? --> no tick !
        if(!m_apArenas[i]->m_NumPlayers)
            continue;

        m_apArenas[i]->Tick();
    }

    if(!GameServer()->m_World.m_Paused)
    {
        if(m_Warmup > Server()->TickSpeed()*g_Config.m_SvStartWarmUp)
            m_Warmup = Server()->TickSpeed()*g_Config.m_SvStartWarmUp;

        // do warmup
        if(m_Warmup || (m_NumParticipants < 2 && !m_TourneyStarted))
        {
            if(m_NumParticipants >= 2)
                m_Warmup--;
            else
                m_Warmup = Server()->TickSpeed()*g_Config.m_SvStartWarmUp;
            if(!m_Warmup)
                StartTourney();
        }
        else
        {
            if(m_NumActiveParticipants > 1)
                HandleBracket();
        }
    }

    if(m_GameOverTick != -1)
    {
        // game over.. wait for restart
        if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*20)
        {
            StartRound();
            m_RoundCount++;
        }
    }

    // game is Paused
    if(GameServer()->m_World.m_Paused)
        ++m_RoundStartTick;

    DoWincheck();
}

void CGameControllerTournDM::HandleBracket()
{
    switch (m_BracketMode)
    {
    case 1:
    case 3:
    {
        if(m_HandlingOdds)
        {
            if(m_NumMatches)
                m_HandlingOdds = false;
            return;
        }

        // iterate through all tees and check wether they are ready for next fight
        for(int i = 0; i < m_NumParticipants; i++)
        {
            if(m_apTBInfo[i]->m_TourneyState != (TOURN_PARTICIPATING|TOURN_DEFEATED))
            {
                // search partner(s)   2^victories = sum(2^nextplayers[i]->victories) ---> need to fight
                int MagicNumber = 0;
                for(int j = i + 1; j < m_NumParticipants; j++)
                {
                    if(m_apTBInfo[j]->m_TourneyState != (TOURN_PARTICIPATING|TOURN_DEFEATED))
                        MagicNumber += 1 << m_apTBInfo[j]->m_Victories;

                    if(MagicNumber == 1 << m_apTBInfo[i]->m_Victories)
                    {
                        if(m_apTBInfo[i]->m_Victories == m_apTBInfo[j]->m_Victories && m_apTBInfo[i]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_WAITING) && m_apTBInfo[j]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_WAITING))
                            for(int n = 0; n < NUM_ARENAS; n++)
                                if(!Arena(n)->m_NumPlayers && m_aActiveArenas[n])
                                {
                                    if(m_apBracketPlayers[i] && m_apBracketPlayers[j])
                                    {
                                        if(m_apBracketPlayers[i]->m_Arena == -1 && m_apBracketPlayers[j]->m_Arena == -1)
                                        {
                                            ChangeArena(m_apBracketPlayers[i]->GetCID(), n);
                                            ChangeArena(m_apBracketPlayers[j]->GetCID(), n);
                                        }
                                    }
                                    else if(m_apBracketPlayers[i] || m_apBracketPlayers[j])
                                    {
                                        if(m_apBracketPlayers[i] && m_apBracketPlayers[i]->m_Arena == -1)
                                            ChangeArena(m_apBracketPlayers[i]->GetCID(), n);
                                        else if(m_apBracketPlayers[j] && m_apBracketPlayers[j]->m_Arena == -1)
                                            ChangeArena(m_apBracketPlayers[j]->GetCID(), n);
                                    }
                                    else
                                    {
                                        m_apTBInfo[i]->m_Victories++;
                                        m_apTBInfo[j]->m_Losses++;
                                        m_apTBInfo[j]->m_TourneyState = (TOURN_PARTICIPATING|TOURN_DEFEATED);
                                        m_NumActiveParticipants--;
                                        m_NumMatches++;
                                    }
                                    i = j+1;
                                    break;
                                }
                        break;
                    }
                }
            }
        }
    }
        break;
    case 2:
    {
        int minVictories = 4;
        for(int Victories = 0; Victories <= 4; Victories++)
        {
            int WaitingID = -1;
            for(int i = 0; i < m_NumParticipants; i++)
            {
                if(m_apBracketPlayers[i] && m_apTBInfo[i]->m_Victories == Victories && m_apTBInfo[i]->m_TourneyState != (TOURN_PARTICIPATING|TOURN_DEFEATED))
                    if(Victories < minVictories)
                        minVictories = Victories;

                if(m_apBracketPlayers[i] && m_apTBInfo[i]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_WAITING) && m_apBracketPlayers[i]->m_Arena == -1)
                {
                    if(m_apTBInfo[i]->m_Victories == Victories)
                    {
                        if(WaitingID == -1)
                            WaitingID = i;
                        else
                        {
                            for(int n = 0; n < NUM_ARENAS; n++)
                                if(!Arena(n)->m_NumPlayers && m_aActiveArenas[n])
                                {
                                    ChangeArena(m_apBracketPlayers[i]->GetCID(), n);
                                    ChangeArena(m_apBracketPlayers[WaitingID]->GetCID(), n);
                                    break;
                                }
                            WaitingID = -1;
                        }
                    }
                }
            }
        }

        int NumPlayers = 0;
        int ID = -1;

        for(int i = 0; i < m_NumParticipants; i++)
            if(m_apBracketPlayers[i] && m_apTBInfo[i]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_WAITING))
            {
                if(m_apTBInfo[i]->m_Victories == minVictories)
                {
                    NumPlayers++;
                    ID = i;
                }
            }
        if(NumPlayers == 1)
            m_apTBInfo[ID]->m_Victories++;
    }
    }
}

void CGameControllerTournDM::HandleOddPlayers()
{
    if(m_BracketMode != 1 && m_BracketMode != 3)
        return;

    int n = 0;
    for(int i = 2; i <= m_NumParticipants; i *= 2)
        n++;
    int OptimalMatches = 1<<n;
    int OddMatches = m_NumParticipants - OptimalMatches;
    int OddPlayers = OddMatches*2;
    int LuckyPlayers = (m_NumParticipants - OddPlayers) % m_NumParticipants;
    m_HandlingOdds = LuckyPlayers;

    int LastFreeArena = -1;
    for(int i = 0; i < LuckyPlayers; i++)
    {
        // all those luckyplayers get an Arena without enemies and thus a free win
        if(m_apBracketPlayers[i] && m_apTBInfo[i]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_WAITING))
        {
            for(int n = 0; n < NUM_ARENAS; n++)
                if(!Arena(n)->m_NumPlayers && ChangeArena(m_apBracketPlayers[i]->GetCID(), n))
                {
                    LastFreeArena = n;
                    break;
                }
        }
    }

    // now real fights
    for(int i = LuckyPlayers; i < m_NumParticipants; i++)
    {
        if(m_apBracketPlayers[i] && m_apTBInfo[i]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_WAITING))
            for(int n = LastFreeArena + 1; n < NUM_ARENAS; n++)
                if(ChangeArena(m_apBracketPlayers[i]->GetCID(), n))
                    break;
    }
}

bool CGameControllerTournDM::CanSpawn(int Team, vec2 *pOutPos, int CID)
{
    CSpawnEval Eval;

    // spectators can't spawn
    if(Team == TEAM_SPECTATORS)
        return false;

    // cant spawn if game is over
    if(m_GameOverTick != -1)
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
    else if(m_SubType == SUBTYPE_VANILLA)
    {
        if(Index == ENTITY_ARMOR)
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
    }

    UpdateArenaStates();
    return false;
}

void CGameControllerTournDM::OnPlayerLeave(int CID)
{
    if(GameServer()->m_apPlayers[CID]->m_Participating)
    {
        // once started dont change this and keep it for stats
        if(!m_TourneyStarted)
            m_NumParticipants--;
        else
            m_apBracketPlayers[GameServer()->m_apPlayers[CID]->m_TID] = 0;
    }

    if(GameServer()->m_apPlayers[CID]->m_Arena != -1)
        Arena(GameServer()->m_apPlayers[CID]->m_Arena)->OnPlayerLeave(CID);
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
    switch (m_SubType)
    {
    case SUBTYPE_VANILLA:
    {
        // default health
        pChr->IncreaseHealth(10);

        // give default weapons
        pChr->GiveWeapon(WEAPON_HAMMER, -1);
        pChr->GiveWeapon(WEAPON_GUN, 10);
    }
        break;
    case SUBTYPE_INSTAGIB:
    {
        // default health
        pChr->IncreaseHealth(10);

        // give default laser
        pChr->GiveWeapon(WEAPON_RIFLE, -1);
    }
        break;
    case SUBTYPE_GRENADE:
    {
        // default health
        pChr->IncreaseHealth(10);

        // give default grenade
        pChr->GiveWeapon(WEAPON_GRENADE, -1);
    }
    }

    if(!pChr->GetPlayer()->m_Participating)
        pChr->GetPlayer()->m_Score = -1000;
    else if(pChr->GetPlayer()->m_Arena == -1)
    {
        if(m_TourneyStarted && pChr->GetPlayer()->m_TID != -1)
        {
            if(m_apTBInfo[pChr->GetPlayer()->m_TID]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_DEFEATED))
                pChr->GetPlayer()->m_Score = -1;
            else
                pChr->GetPlayer()->m_Score = m_apTBInfo[pChr->GetPlayer()->m_TID]->m_Victories;
        }
    }
}

void CGameControllerTournDM::DoWincheck()
{
    if(m_GameOverTick == -1 && !m_Warmup && !GameServer()->m_World.m_ResetRequested)
    {
       if(m_NumActiveParticipants == 1)
       {
           for(int i = 0; i < m_NumParticipants; i++)
               if(m_apTBInfo[i]->m_TourneyState == (TOURN_PARTICIPATING|TOURN_WAITING))
               {
                   if(m_apBracketPlayers[i] && m_apBracketPlayers[i]->m_Arena == -1)
                   {
                       char aBuf[256];
                       str_format(aBuf, sizeof(aBuf), "'%s' wins the tournament !", Server()->ClientName(m_apBracketPlayers[i]->GetCID()));
                       GameServer()->SendChatTarget(-1, aBuf);
                       GameServer()->SendChatTarget(-1, "Congratulations !");
                       EndRound();
                       break;
                   }
                   else if(!m_apBracketPlayers[i])
                   {
                       EndRound();
                       break;
                   }
               }
       }
    }
}

void CGameControllerTournDM::StartTourney()
{
    ResetGame();

    m_BracketMode = g_Config.m_SvBracket;

    // random you want random here you go
    if(m_BracketMode == 3)
    {
        int randomArray[MAX_CLIENTS];
        for (int i = 0; i < MAX_CLIENTS; i++)
            randomArray[i] = i;

        for (int i = 0; i < MAX_CLIENTS - 1; i++)
        {
            int j = i + rand() / (RAND_MAX / (MAX_CLIENTS - i) + 1);
            int t = randomArray[j];
            randomArray[j] = randomArray[i];
            randomArray[i] = t;
        }

        int n = 0;
        for(int i = 0; i < MAX_CLIENTS; i++)
            if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_Participating)
                GameServer()->m_apPlayers[i]->m_TID = randomArray[n++];
    }

    // correct TIDs
    for(int n = 0; n < m_NumParticipants; n++)
    {
        int min = -1;
        int minID = 0;
        for(int i = 0; i < MAX_CLIENTS; i++)
        {
            if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_Participating)
            {
                if((min > GameServer()->m_apPlayers[i]->m_TID || min == -1) && GameServer()->m_apPlayers[i]->m_TID >= n)
                {
                    min = GameServer()->m_apPlayers[i]->m_TID;
                    minID = i;
                }
            }
        }
        if(GameServer()->m_apPlayers[minID])
        {
            GameServer()->m_apPlayers[minID]->m_TID = n;
            m_apBracketPlayers[n] = GameServer()->m_apPlayers[minID];
            m_apTBInfo[n] = &m_aTPInfo[minID];
            m_apTBInfo[n]->m_TourneyState = (TOURN_PARTICIPATING|TOURN_WAITING);
        }
    }

    for(int i = 0; i < NUM_ARENAS; i++)
    {
        m_TourneyStarted = true;
        Arena(i)->m_TourneyStarted = true;
        Arena(i)->m_Warmup = Server()->TickSpeed()*g_Config.m_SvRoundWarmUp;
    }

    m_NumActiveParticipants = m_NumParticipants;

    GameServer()->SendBroadcast("Tournament started, Good Luck !", -1);

    // get rid of odd players and let them fight first
    HandleOddPlayers();
}

void CGameControllerTournDM::StartRound()
{
    for(int i = 0; i < MAX_CLIENTS; i++)
        if(GameServer()->m_apPlayers[i])
            ChangeArena(i, -1);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(GameServer()->m_apPlayers[i])
        {
            // reset tourney stuff
            m_aTPInfo[i].m_TourneyState = 0;
            m_aTPInfo[i].m_Victories = 0;
            m_aTPInfo[i].m_Losses = 0;
            m_apTBInfo[i] = 0;
            GameServer()->m_apPlayers[i]->m_TID = -1;
            GameServer()->m_apPlayers[i]->m_Participating = false;
        }
        m_apBracketPlayers[i] = 0;
    }

    m_NumMatches = 0;
    m_JoinCount = 0;
    m_NumParticipants = 0;
    m_NumActiveParticipants = 0;
    m_TourneyStarted = false;
    m_HandlingOdds = false;
    for(int i = 0; i < NUM_ARENAS; i++)
        Arena(i)->m_TourneyStarted = false;

    IGameController::StartRound();
}

void CGameControllerTournDM::EndRound()
{
    if(m_Warmup) // game can't end when we are running warmup
        return;

    GameServer()->m_World.m_Paused = true;
    m_GameOverTick = Server()->Tick();
    m_SuddenDeath = 0;

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(GameServer()->m_apPlayers[i])
        {
            if(GameServer()->m_apPlayers[i]->m_Participating)
                GameServer()->m_apPlayers[i]->m_Score = m_apTBInfo[GameServer()->m_apPlayers[i]->m_TID]->m_Victories;
            else
                GameServer()->m_apPlayers[i]->m_Score = -1000;

            if(GameServer()->m_apPlayers[i]->m_Participating)
                GameServer()->m_apPlayers[i]->SetTeam(0, false);
            else
                GameServer()->m_apPlayers[i]->SetTeam(TEAM_SPECTATORS, false);

        }
    }
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
            if(Arena(ArenaID)->m_GameOverTick != -1 || m_GameOverTick != -1)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
            if(Arena(ArenaID)->m_SuddenDeath)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
            if(Arena(ArenaID)->m_Paused || GameServer()->m_World.m_Paused)
                pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
            if(m_GameOverTick == -1)
                pGameInfoObj->m_RoundStartTick = Arena(ArenaID)->m_RoundStartTick;
            else
                pGameInfoObj->m_RoundStartTick = m_RoundStartTick;

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
        if(Arena(ArenaID)->m_GameOverTick != -1 || m_GameOverTick != -1)
            pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_GAMEOVER;
        if(Arena(ArenaID)->m_SuddenDeath)
            pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_SUDDENDEATH;
        if(Arena(ArenaID)->m_Paused || GameServer()->m_World.m_Paused)
            pGameInfoObj->m_GameStateFlags |= GAMESTATEFLAG_PAUSED;
        if(m_GameOverTick == -1)
            pGameInfoObj->m_RoundStartTick = Arena(ArenaID)->m_RoundStartTick;
        else
            pGameInfoObj->m_RoundStartTick = m_RoundStartTick;
        pGameInfoObj->m_WarmupTimer = Arena(ArenaID)->m_Warmup;
        pGameInfoObj->m_TimeLimit = g_Config.m_SvTimelimit;
    }

    pGameInfoObj->m_ScoreLimit = g_Config.m_SvScorelimit;

    pGameInfoObj->m_RoundNum = (str_length(g_Config.m_SvMaprotation) && g_Config.m_SvRoundsPerMap) ? g_Config.m_SvRoundsPerMap : 0;
    pGameInfoObj->m_RoundCurrent = m_RoundCount+1;
}

const char* CGameControllerTournDM::GetTourneyState()
{
    char aBuf[32];
    if(!m_TourneyStarted && m_NumParticipants < 2)
        str_format(aBuf, sizeof(aBuf), " - [waiting for players]");
    else if (!m_TourneyStarted && m_NumParticipants >= 2)
        str_format(aBuf, sizeof(aBuf), " - [starting in %d seconds]", m_Warmup/Server()->TickSpeed());
    else if (m_TourneyStarted && m_GameOverTick == -1)
        str_format(aBuf, sizeof(aBuf), " - [%d players left]", m_NumActiveParticipants);
    else if (m_TourneyStarted && m_GameOverTick != -1)
        str_format(aBuf, sizeof(aBuf), " - [restarting]");
    else
        str_format(aBuf, sizeof(aBuf), "");


    const char* r = aBuf;
    return r;
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

    m_GameOverTick = -1;
    m_SuddenDeath = 0;
    m_RoundStartTick = Server()->Tick();

    m_TourneyStarted = false;

    m_Warmup = Server()->TickSpeed()*g_Config.m_SvRoundWarmUp;
}

void CGameControllerArena::OnPlayerEnter(int CID)
{
    Controller()->m_apTBInfo[GameServer()->m_apPlayers[CID]->m_TID]->m_TourneyState = TOURN_PARTICIPATING;

    if(m_NumPlayers == 1)
    {
        m_NumPlayers++;
        if(m_apOpponents[0])
            m_apOpponents[1] = GameServer()->m_apPlayers[CID];
        else
            m_apOpponents[0] = GameServer()->m_apPlayers[CID];
    }
    else if(m_NumPlayers == 0)
    {
        m_NumPlayers++;
        m_apOpponents[0] = GameServer()->m_apPlayers[CID];
        StartRound();
    }
}

void CGameControllerArena::OnPlayerLeave(int CID)
{ 
    int ID = OnPlayerArenaLeave(CID);
    if(m_NumPlayers == 1 && ID != -1)
        EndRound(!ID, true);
}

int CGameControllerArena::OnPlayerArenaLeave(int CID)
{
    for(int i = 0; i < MAX_OPPONENTS; i++)
        if(m_apOpponents[i] && m_apOpponents[i]->GetCID() == CID)
        {
            m_apOpponents[i] = 0;
            m_NumPlayers--;
            return i;
        }
    return -1;
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
    if(!m_TourneyStarted || !m_NumPlayers)
        return;

    if(m_GameOverTick == -1 && !m_Warmup && !m_ResetRequested)
    {
        if(m_NumPlayers == 1)
        {
            EndRound(m_apOpponents[0] ? 0 : 1);
            return;
        }

        int TopID = m_apOpponents[0]->m_Score > m_apOpponents[1]->m_Score ? 0 : 1;
        int Topscore = m_apOpponents[TopID]->m_Score;

        if((g_Config.m_SvScorelimit > 0 && Topscore >= g_Config.m_SvScorelimit) ||
            (g_Config.m_SvTimelimit > 0 && (Server()->Tick()-m_RoundStartTick) >= g_Config.m_SvTimelimit*Server()->TickSpeed()*60))
        {
            if(m_apOpponents[0]->m_Score != m_apOpponents[1]->m_Score)
            {
                EndRound(TopID);
            }
            else
                m_SuddenDeath = true;
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
        {
            m_apOpponents[i]->SetTeam(0, false);
        }

    m_RoundRunning = true;
}

void CGameControllerArena::EndRound(int winnerID, bool Left)
{
    if(!m_RoundRunning)
        return;

    m_RoundRunning = false;

    char aBuf[256];
    if(m_NumPlayers == 2)
        str_format(aBuf, sizeof(aBuf), "'%s' defeated '%s' !", Server()->ClientName(m_apOpponents[winnerID]->GetCID()), Server()->ClientName(m_apOpponents[!winnerID]->GetCID()));
    else if(m_apOpponents[winnerID])
        str_format(aBuf, sizeof(aBuf), "'%s' won a round !", Server()->ClientName(m_apOpponents[winnerID]->GetCID()));

    if(m_NumPlayers)
        GameServer()->SendChatTarget(-1, aBuf);

    if(m_NumPlayers == 2)
    {
        Controller()->m_apTBInfo[m_apOpponents[winnerID]->m_TID]->m_TourneyState = TOURN_PARTICIPATING|TOURN_WAITING;
        Controller()->m_apTBInfo[m_apOpponents[!winnerID]->m_TID]->m_TourneyState = TOURN_PARTICIPATING|TOURN_DEFEATED;
        Controller()->m_apTBInfo[m_apOpponents[winnerID]->m_TID]->m_Victories++;
        Controller()->m_apTBInfo[m_apOpponents[!winnerID]->m_TID]->m_Losses++;

        Controller()->m_NumActiveParticipants--;
    }
    else if(m_apOpponents[winnerID])
    {
        Controller()->m_apTBInfo[m_apOpponents[winnerID]->m_TID]->m_TourneyState = TOURN_PARTICIPATING|TOURN_WAITING;
        Controller()->m_apTBInfo[m_apOpponents[winnerID]->m_TID]->m_Victories++;

        // this is not an odd round but a player left --> decrease m_NumActiveParticipants
        if(Left)
            Controller()->m_NumActiveParticipants--;
    }

    m_Paused = true;
    m_GameOverTick = Server()->Tick();
    m_SuddenDeath = 0;

    Controller()->m_NumMatches++;
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
        m_Warmup--;
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
        if(Server()->Tick() > m_GameOverTick+Server()->TickSpeed()*5)
        {
            for(int i = 0; i < MAX_OPPONENTS; i++)
                if(m_apOpponents[i])
                    m_pController->ChangeArena(m_apOpponents[i]->GetCID(), -1);
            StartRound();
        }
    }

    // game is Paused
    if(m_Paused || GameServer()->m_World.m_Paused)
        ++m_RoundStartTick;

    DoWincheck();
}
