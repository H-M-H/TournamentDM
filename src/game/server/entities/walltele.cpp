#include <game/server/gamecontext.h>
#include <game/mapitems.h>
#include "walltele.h"

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

    m_StartTick = Server()->Tick();
}

void CWallTele::Tick()
{
    for(int i = 0; i < MAX_CLIENTS; i++)
        if(GameServer()->m_apPlayers[i] && GameServer()->m_apPlayers[i]->m_Arena == -1 &&
                GameServer()->m_apPlayers[i]->GetCharacter() &&
                distance(GameServer()->m_apPlayers[i]->GetCharacter()->m_Pos, m_Pos) < 64 &&
                !GameServer()->Collision()->IntersectLine(m_Pos, GameServer()->m_apPlayers[i]->GetCharacter()->m_Pos, 0x0, 0x0))
            GameServer()->m_apPlayers[i]->GetCharacter()->Teleport(m_PosOut);
}

void CWallTele::Snap(int SnappingClient)
{
    if(!((GameServer()->m_apPlayers[SnappingClient]->m_Arena == -1 && GameServer()->m_apPlayers[SnappingClient]->GetTeam() != TEAM_SPECTATORS) ||
         (GameServer()->m_apPlayers[SnappingClient]->GetTeam() == TEAM_SPECTATORS &&
          GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID != -1 &&
          GameServer()->m_apPlayers[GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID] &&
          GameServer()->m_apPlayers[GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID]->m_Arena == -1)))
        return;

    CNetObj_Laser *pObj1 = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(NETOBJTYPE_LASER, m_ID, sizeof(CNetObj_Laser)));
    if(!pObj1)
        return;

    pObj1->m_X = (int)m_Pos.x;
    pObj1->m_Y = (int)m_Pos.y;
    pObj1->m_FromX = (int)m_Pos.x;
    pObj1->m_FromY = (int)m_Pos.y;
    pObj1->m_StartTick = m_StartTick;
}

void CWallTele::SetPosOut()
{
    if(!GameServer()->Collision()->ThroughWall(m_Pos, m_Direction, &m_PosOut))
        GameWorld()->DestroyEntity(this);
}
