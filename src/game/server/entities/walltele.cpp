#include <game/server/gamecontext.h>
#include <game/mapitems.h>
#include "walltele.h"

CTeleDisp::CTeleDisp(CGameWorld *pGameWorld, vec2 Pos)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_TELEDISP)
{
    m_Pos = Pos;
    m_StartTick = Server()->Tick();

    GameWorld()->InsertEntity(this);
}

void CTeleDisp::Snap(int SnappingClient)
{
    CPlayer* pP = GameServer()->m_apPlayers[SnappingClient];

    if(pP->m_Arena != -1 ||
            (pP->GetTeam() == TEAM_SPECTATORS &&
             (pP->m_SpectatorID == -1 ||
              !GameServer()->m_apPlayers[pP->m_SpectatorID] ||
              GameServer()->m_apPlayers[pP->m_SpectatorID]->m_Arena != -1)))
        return;

    if(distance(pP->m_ViewPos, m_Pos) > 380)
        return;

    CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
    if(pProj)
    {
        pProj->m_X = (int)m_Pos.x;
        pProj->m_Y = (int)m_Pos.y;
        pProj->m_VelX = 0;
        pProj->m_VelY = 0;
        pProj->m_StartTick = m_StartTick;
        pProj->m_Type = WEAPON_NINJA;
    }
}

// ///////////////////////////////////////////// //

CWallTele::CWallTele(CGameWorld *pGameWorld, vec2 Pos)
: CEntity(pGameWorld, CGameWorld::ENTTYPE_WALLTELE)
{
    m_Pos = Pos;

    int Flags = GameServer()->Collision()->GetTileFlags(m_Pos);

    switch (Flags)
    {
    case TILEFLAG_HFLIP:
    case TILEFLAG_HFLIP|TILEFLAG_VFLIP:
        m_Direction = vec2(0,1);
        break;

    case TILEFLAG_ROTATE:
    case TILEFLAG_ROTATE|TILEFLAG_VFLIP:
        m_Direction = vec2(1,0);
        break;

    case TILEFLAG_HFLIP|TILEFLAG_ROTATE:
    case TILEFLAG_VFLIP|TILEFLAG_HFLIP|TILEFLAG_ROTATE:
        m_Direction = vec2(-1,0);
        break;

    default:
        m_Direction = vec2(0,-1);
        break;
    }

    GameWorld()->InsertEntity(this);
    SetPosOut();

    // create a nice looking arrow :3
    for(int i = -1; i < 2; i +=2)
        new CTeleDisp(GameWorld(), m_Pos + m_Direction*i*10);

    for(int i = -1; i < 2; i +=2)
        new CTeleDisp(GameWorld(), m_Pos + vec2(m_Direction.y, m_Direction.x)*i*10 + m_Direction*7);


    m_StartTick = Server()->Tick();
}

void CWallTele::Tick()
{
    for(int i = 0; i < MAX_CLIENTS; i++)
        if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_Arena == -1 &&
                GameServer()->m_apPlayers[i]->GetCharacter() &&
                abs(GameServer()->m_apPlayers[i]->GetCharacter()->m_Pos.x - m_Pos.x) < 16 &&
                abs(GameServer()->m_apPlayers[i]->GetCharacter()->m_Pos.y - m_Pos.y) < 16)
            GameServer()->m_apPlayers[i]->GetCharacter()->Teleport(m_PosOut);
}

void CWallTele::SetPosOut()
{
    if(!GameServer()->Collision()->ThroughWall(m_Pos, m_Direction, &m_PosOut))
        GameWorld()->DestroyEntity(this);
}

void CWallTele::Snap(int SnappingClient)
{
    CPlayer* pP = GameServer()->m_apPlayers[SnappingClient];

    if(pP->m_Arena != -1 ||
            (pP->GetTeam() == TEAM_SPECTATORS &&
             (pP->m_SpectatorID == -1 ||
              !GameServer()->m_apPlayers[pP->m_SpectatorID] ||
              GameServer()->m_apPlayers[pP->m_SpectatorID]->m_Arena != -1)))
        return;

    if(distance(pP->m_ViewPos, m_Pos) > 380)
        return;

    CNetObj_Projectile *pProj = static_cast<CNetObj_Projectile *>(Server()->SnapNewItem(NETOBJTYPE_PROJECTILE, m_ID, sizeof(CNetObj_Projectile)));
    if(pProj)
    {
        pProj->m_X = (int)m_Pos.x;
        pProj->m_Y = (int)m_Pos.y;
        pProj->m_VelX = 0;
        pProj->m_VelY = 0;
        pProj->m_StartTick = m_StartTick;
        pProj->m_Type = WEAPON_NINJA;
    }
}
