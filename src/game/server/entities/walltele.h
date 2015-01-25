#ifndef CWALLTELE_H
#define CWALLTELE_H

#include <game/server/entity.h>

class CTeleDisp : public CEntity
{
public:
    CTeleDisp(CGameWorld *pGameWorld, vec2 Pos);
    virtual void Snap(int SnappingClient);

private:
    int m_StartTick;
};

class CWallTele : public CEntity
{
public:
    CWallTele(CGameWorld *pGameWorld, vec2 Pos);

    virtual void Tick();
    virtual void Snap(int SnappingClient);


private:
    void SetPosOut();

    vec2 m_PosOut;
    vec2 m_Direction;
    int m_StartTick;
};

#endif // CWALLTELE_H
