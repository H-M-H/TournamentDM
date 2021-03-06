/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_SERVER_ENTITIES_PICKUP_H
#define GAME_SERVER_ENTITIES_PICKUP_H

#include <game/server/entity.h>

const int PickupPhysSize = 14;

class CPickup : public CEntity
{
public:
    CPickup(CGameWorld *pGameWorld, int Type, int SubType = 0, int Arena = -1); // Arena == -2: no arena yet, will be determined when picked up

	virtual void Reset();
	virtual void Tick();
	virtual void TickPaused();
	virtual void Snap(int SnappingClient);

private:
	int m_Type;
	int m_Subtype;
	int m_SpawnTick;
};

#endif
