// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright(C) 2013 Ioan Chera
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/
//
// Additional terms and conditions compatible with the GPLv3 apply. See the
// file COPYING-EE for details.
//
//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//      Main bot thinker
//
//-----------------------------------------------------------------------------

#include <queue>
#include "../z_zone.h"

#include "b_think.h"
#include "b_trace.h"
#include "b_util.h"
#include "../cam_sight.h"
#include "../d_event.h"
#include "../d_gi.h"
//#include "../d_ticcmd.h"
//#include "../doomdef.h"
#include "../doomstat.h"
#include "../e_edf.h"
#include "../e_player.h"
#include "../e_things.h"
#include "../ev_specials.h"
#include "../g_dmflag.h"
#include "../metaapi.h"
#include "../m_compare.h"
#include "../p_maputl.h"
#include "../p_setup.h"
#include "../p_spec.h"
#include "../r_state.h"

// The commands that the bots will send to the players to be added in G_Ticker
Bot bots[MAXPLAYERS];

//
// Bot::mapInit
//
// Initialize bot for new map. Mostly cleanup stuff from previous session
//
void Bot::mapInit()
{
   B_EmptyTableAndDelete(goalTable);   // remove all objectives
   B_EmptyTableAndDelete(goalEvents);  // remove all previously listed events
   prevCtr = 0;
   m_searchstage = 0;

   m_finder.SetMap(botMap);
   m_finder.SetPlayerHeight(pl->mo->height);
   m_hasPath = false;
    
    m_lastPathSS = nullptr;

   m_deepTriedLines.clear();
   m_deepSearchMode = DeepNormal;
   m_deepAvailSsectors.clear();
   m_deepRepeat = nullptr;
    m_justGotLost = false;
    m_goalTimer = 0;
    m_dropSS.clear();
}

//
// Bot::capCommands
//
// Limits all movement tic commands within "legal" values, to prevent human's
// tic command from being added to bot's, resulting in an otherwise impossible
// to achieve running speed
//
void Bot::capCommands()
{
   fixed_t fm = pl->pclass->forwardmove[1];
   
   if(cmd->forwardmove > fm)      cmd->forwardmove = fm;
   else if(cmd->forwardmove < -fm)      cmd->forwardmove = -fm;
   
   if(cmd->sidemove > fm)      cmd->sidemove = fm;
   else if(cmd->sidemove < -fm)      cmd->sidemove = -fm;
}

//
// Bot::goalAchieved
//
// Returns true if current goal has been noticed in event table. Removes all
// unsought events
//
bool Bot::goalAchieved()
{
   v2fixed_t goalcoord;
   MetaV2Fixed *metaob = nullptr;
   
   if(!goalTable.getNumItems())
      return true;   // no goal existing, so just cancel trip
   
   while ((metaob = goalEvents.getNextTypeEx<MetaV2Fixed>(nullptr)))
   {
      goalcoord = goalTable.getV2Fixed(metaob->getKey(), B_MakeV2Fixed(D_MAXINT, D_MAXINT));

      if(goalcoord == metaob->getValue())
      {
         // found a goal with the event's key and type
         // now do a case-by-case test
         B_EmptyTableAndDelete(goalEvents);
         B_EmptyTableAndDelete(goalTable);
         return true;
      }
      goalEvents.removeObject(metaob);
      delete metaob;
   }
   return false;
}

PathResult Bot::reachableItem(const BSubsec& ss, void* v)
{
    Bot& self = *(Bot*)v;
    v2fixed_t dummy;

    bool result = objOfInterest(ss, dummy, &self);

    return result ? (self.m_deepSearchMode == DeepBeyond ? PathDone : PathAdd) : PathNo;
}


bool Bot::shouldUseSpecial(const line_t& line, const BSubsec& liness)
{
    VanillaLineSpecial vls = static_cast<VanillaLineSpecial>(line.special);
    switch (vls)
    {
        // sure goals
    case VLS_S1ExitLevel:
    case VLS_WRExitLevel:
        return m_searchstage >= 2;
    case VLS_S1SecretExit:
    case VLS_WRSecretExit:
        return m_searchstage >= 1;

        // would only block or cause harm
    case VLS_W1CloseDoor:
//    case VLS_W1FastCeilCrushRaise:
    case VLS_W1CloseDoor30:
//    case VLS_W1CeilingCrushAndRaise:
    case VLS_SRCloseDoor:
    case VLS_SRCeilingLowerToFloor:
    case VLS_W1CeilingLowerAndCrush:
//    case VLS_S1CeilingCrushAndRaise:
    case VLS_S1CloseDoor:
    case VLS_WRCeilingLowerAndCrush:
//    case VLS_WRCeilingCrushAndRaise:
    case VLS_WRCloseDoor:
    case VLS_WRCloseDoor30:
//    case VLS_WRFastCeilCrushRaise:
    case VLS_WRDoorBlazeClose:
    case VLS_W1DoorBlazeClose:
    case VLS_S1DoorBlazeClose:
    case VLS_SRDoorBlazeClose:
//    case VLS_W1SilentCrushAndRaise:
    case VLS_W1CeilingLowerToFloor:
//    case VLS_WRSilentCrushAndRaise:
    case VLS_WRCeilingLowerToFloor:
//    case VLS_S1FastCeilCrushRaise:
//    case VLS_S1SilentCrushAndRaise:
    case VLS_S1CeilingLowerAndCrush:
    case VLS_S1CloseDoor30:
//    case VLS_SRFastCeilCrushRaise:
//    case VLS_SRCeilingCrushAndRaise:
//    case VLS_SRSilentCrushAndRaise:
    case VLS_SRCeilingLowerAndCrush:
    case VLS_SRCloseDoor30:
        return false;

        // more complex, so for now they aren't targetted
    case VLS_W1PlatStop:
    case VLS_W1CeilingCrushStop:
    case VLS_WRCeilingCrushStop:
    case VLS_SRChangeOnlyNumeric:
    case VLS_WRPlatStop:
    case VLS_W1ChangeOnly:
    case VLS_WRChangeOnly:
    case VLS_S1PlatStop:
    case VLS_S1CeilingCrushStop:
    case VLS_SRCeilingCrushStop:
    case VLS_S1ChangeOnly:
    case VLS_SRChangeOnly:
    case VLS_W1ChangeOnlyNumeric:
    case VLS_WRChangeOnlyNumeric:
    case VLS_S1ChangeOnlyNumeric:
    case VLS_WRStartLineScript1S:
    case VLS_W1StartLineScript:
    case VLS_W1StartLineScript1S:
    case VLS_SRStartLineScript:
    case VLS_S1StartLineScript:
    case VLS_GRStartLineScript:
    case VLS_G1StartLineScript:
    case VLS_WRStartLineScript:
        return false;

        // useless
    case VLS_W1LightTurnOn: 
    case VLS_W1LightTurnOn255:
    case VLS_W1StartLightStrobing:
    case VLS_W1LightsVeryDark:
    case VLS_WRLightsVeryDark:
    case VLS_WRLightTurnOn:
    case VLS_WRLightTurnOn255:
    case VLS_W1TurnTagLightsOff:
    case VLS_SRLightTurnOn255:
    case VLS_SRLightsVeryDark:
    case VLS_WRStartLightStrobing:
    case VLS_WRTurnTagLightsOff:
    case VLS_S1LightTurnOn:
    case VLS_S1LightsVeryDark:
    case VLS_S1LightTurnOn255:
    case VLS_S1StartLightStrobing:
    case VLS_S1TurnTagLightsOff:
    case VLS_SRLightTurnOn:
    case VLS_SRStartLightStrobing:
    case VLS_SRTurnTagLightsOff:
    case VLS_W1TeleportMonsters:
    case VLS_WRTeleportMonsters:
    case VLS_W1SilentLineTRMonsters:
    case VLS_WRSilentLineTRMonsters:
    case VLS_W1SilentLineTeleMonsters:
    case VLS_WRSilentLineTeleMonsters:
    case VLS_W1SilentTeleportMonsters:
    case VLS_WRSilentTeleportMonsters:
        return false;

        // personnel teleportation: already handled in the path finder
    case VLS_W1Teleport:
    case VLS_WRTeleport:
    case VLS_S1Teleport:
    case VLS_SRTeleport:
    case VLS_W1SilentTeleport:
    case VLS_WRSilentTeleport:
    case VLS_S1SilentTeleport:
    case VLS_SRSilentTeleport:
        return false;

    case VLS_W1SilentLineTeleport:
    case VLS_WRSilentLineTeleport:
    case VLS_W1SilentLineTeleportReverse:
    case VLS_WRSilentLineTeleportReverse:
        return false;

    default:
        break;
    }
    
    // now that we got some lines out of the way, decide quickly to use once-
    // only types
    const ev_action_t* action = EV_ActionForSpecial(line.special);
    if(action && (action->type == &S1ActionType
                  || action->type == &W1ActionType))
    {
        bool result = LevelStateStack::Push(line, *pl);
        LevelStateStack::Clear();
        return result;
        // just push them, as long as they're not the blocking type and have any
        // effect
    }

    if (m_deepSearchMode == DeepNormal)
    {
        LevelStateStack::Clear();
        m_deepTriedLines.clear();
        m_deepAvailSsectors.clear();

        m_deepTriedLines.insert(&line);

        m_deepSearchMode = DeepAvail;
        m_finder.AvailableGoals(*ss, &m_deepAvailSsectors, reachableItem, this);
        m_deepSearchMode = DeepNormal;

        // Now apply the change
        if (!LevelStateStack::Push(line, *pl))
            return false;

        m_deepSearchMode = DeepBeyond;
        bool result;
        const BSubsec* repsave = nullptr;
        do
        {
            m_deepRepeat = nullptr;
            result = m_finder.AvailableGoals(repsave ? *repsave : liness,
                                             nullptr, reachableItem, this);
            repsave = m_deepRepeat;
        } while (result && m_deepRepeat);
        m_deepRepeat = nullptr;
        
        m_deepSearchMode = DeepNormal;

        LevelStateStack::Clear();
        return result;
    }

    return false;
}

bool Bot::objOfInterest(const BSubsec& ss, v2fixed_t& coord, void* v)
{

    Bot& self = *(Bot*)v;

    if (self.m_deepSearchMode == DeepBeyond
        && self.m_deepAvailSsectors.count(&ss))
    {
        return false;
    }

    const Mobj* item;
    fixed_t fh;
    const Mobj& plmo = *self.pl->mo;
    std::unordered_map<spritenum_t, PlayerStats>::const_iterator effect, nopick;
    for (auto it = ss.mobjlist.begin(); it != ss.mobjlist.end(); ++it)
    {
        item = *it;
        if (item == &plmo)
            continue;
        fh = ss.msector->getFloorHeight();
        if (self.m_deepSearchMode == DeepNormal && (fh + plmo.height < item->z
                                                    || fh > item->z
                                                    + item->height))
        {
            continue;
        }
        if (item->flags & MF_SPECIAL)
        {
            if (item->sprite < 0 || item->sprite >= NUMSPRITES)
                continue;

            effect = self.effectStats.find(item->sprite);
            nopick = self.nopickStats.find(item->sprite);

            if (effect == self.effectStats.cend())
            {
                // unknown (new) item
                // Does it have nopick stats?
                if (nopick == self.nopickStats.cend())
                {
                    // no. Totally unknown
                    if (self.m_deepSearchMode == DeepNormal)
                    {
                        self.goalTable.setV2Fixed(BOT_PICKUP,
                                                  coord = B_CoordXY (*item));
                    }
                    return true;
                }
                else
                {
                    // yes. Is it greater than current status?
                    if (nopick->second.greaterThan(*self.pl))
                    {
                        // Yes. It might be pickable now
                        if (self.m_deepSearchMode == DeepNormal)
                        {
                            self.goalTable.setV2Fixed(BOT_PICKUP,
                                                      coord = B_CoordXY(*item));
                        }
                        return true;
                    }
                }
            }
            else
            {
                // known item.
                // currently just try to pick it up
                if (nopick == self.nopickStats.cend() ||
                    effect->second.fillsGap(*self.pl, nopick->second))
                {
                    if (self.m_deepSearchMode == DeepNormal)
                    {
                        self.goalTable.setV2Fixed(BOT_PICKUP,
                                                  coord = B_CoordXY(*item));
                    }
                    return true;
                }
            }
        }
    }

    const line_t* line;
    const ev_action_t* action;
    for (auto it = ss.linelist.begin(); it != ss.linelist.end(); ++it)
    {
        line = *it;
        action = EV_ActionForSpecial(line->special);
        if (action && (action->type == &W1ActionType
                       || action->type == &WRActionType
                       || action->type == &S1ActionType
                       || action->type == &SRActionType
                       || action->type == &DRActionType))
        {
            // OK, this might be viable. But check.
            if (self.m_deepSearchMode == DeepAvail)
            {
                self.m_deepTriedLines.insert(line);
                return true;
            }
            else if (self.m_deepSearchMode == DeepBeyond)
            {
                if (!self.m_deepTriedLines.count(line))
                {
                    if (self.shouldUseSpecial(*line, ss))
                        return true;
                    self.m_deepTriedLines.insert(line);
                    LevelStateStack::Push(*line, *self.pl);
                    self.m_deepRepeat = &ss;
                    return true;
                }
                //return false;
            }
            else if (self.shouldUseSpecial(*line, ss))
            {
                coord.x = (line->v1->x + line->v2->x) / 2;
                coord.y = (line->v1->y + line->v2->y) / 2;
                v2fixed_t crd = B_CoordXY(*line->v1);
                //crd.x += self.random.range(-16, 16) * FRACUNIT;
                //crd.y += self.random.range(-16, 16) * FRACUNIT;
                self.goalTable.setV2Fixed(BOT_WALKTRIG,crd);
                return true;
            }
        }
    }

    return false;
}

//
// Bot::enemyVisible()
//
// Returns true if an enemy is visible now
// Code inspired from P_LookForMonsters
//
void Bot::enemyVisible(Target& nearest)
{
    // P_BlockThingsIterator is safe to use outside of demo correctness
    camsightparams_t cam;
    cam.setLookerMobj(pl->mo);

    memset(&nearest, 0, sizeof(nearest));
    
    fixed_t mindist;
    fixed_t dist;
    if (!botMap->livingMonsters.empty())
    {

restart:
        mindist = D_MAXINT;
        for (auto it = botMap->livingMonsters.begin();
             it != botMap->livingMonsters.end(); ++it)
        {
            const Mobj* m = *it;
            if (m->health <= 0 || !(m->flags & MF_SHOOTABLE))
            {
                botMap->livingMonsters.erase(m);
                goto restart;
            }
            cam.setTargetMobj(m);
            if (CAM_CheckSight(cam))
            {
                dist = P_AproxDistance(m->x - pl->mo->x, m->y - pl->mo->y);
                if (dist < mindist && dist < MISSILERANGE / 2)
                {
                    mindist = dist;
                    nearest.mobj = m;
                    nearest.coord = B_CoordXY(*m);
                }
            }
        }
    }
    
    v2fixed_t lvec;
    angle_t lang;
    const sector_t* sector;
    fixed_t bulletheight = pl->mo->z + 32 * FRACUNIT;
    for(const line_t* line : botMap->gunLines)
    {
        sector = line->frontsector;
        if(!sector || sector->floorheight > bulletheight
           || sector->ceilingheight < bulletheight)
        {
            continue;
        }
        lvec.x = (line->v1->x + line->v2->x) / 2;
        lvec.y = (line->v1->y + line->v2->y) / 2;
        lang = P_PointToAngle(line->v1->x, line->v1->y,
                              line->v2->x, line->v2->y);
        lang -= ANG90;
        lang >>= ANGLETOFINESHIFT;
        lvec.x += FixedMul(FRACUNIT, finecosine[lang]);
        lvec.y += FixedMul(FRACUNIT, finesine[lang]);
        cam.tgroupid = line->frontsector->groupid;
        cam.tx = lvec.x;
        cam.ty = lvec.y;
        cam.tz = sector->floorheight;
        cam.theight = sector->ceilingheight - sector->floorheight;
        
        if(CAM_CheckSight(cam) && LevelStateStack::Push(*line, *pl))
        {
            LevelStateStack::Pop();
            dist = P_AproxDistance(lvec.x - pl->mo->x, lvec.y - pl->mo->y);
            if(dist < mindist)
            {
                mindist = dist;
                nearest.gline = line;
                nearest.isLine = true;
                nearest.coord = lvec;
            }
        }
    }
}

void Bot::doCombatAI(const Target& target)
{
    fixed_t mx, my, nx, ny;
    mx = pl->mo->x;
    my = pl->mo->y;
    nx = target.coord.x;
    ny = target.coord.y;
    angle_t dangle, tangle;
    tangle = P_PointToAngle(mx, my, nx, ny);
    dangle = tangle - pl->mo->angle;

    int16_t angleturn = (int16_t)(tangle >> 16) - (int16_t)(pl->mo->angle >> 16);
    angleturn >>= 2;
    //if (!angleturn)
    {
            angleturn += random.range(-128, 128);

    }

    if (angleturn > 1500)
        angleturn = 1500;
    if (angleturn < -1500)
        angleturn = -1500;

    cmd->angleturn += angleturn;

    RTraversal rt;
    rt.SafeAimLineAttack(pl->mo, pl->mo->angle, MISSILERANGE / 2, 0);
    if (rt.m_clip.linetarget)
    {
        cmd->buttons |= BT_ATTACK;
    }

    if(target.isLine)
    {
        angle_t vang[2];
        vang[0] = P_PointToAngle(mx, my, target.gline->v1->x,
                                 target.gline->v1->y);
        vang[1] = P_PointToAngle(mx, my, target.gline->v2->x,
                                 target.gline->v2->y);
        if(vang[1] - vang[0] > pl->mo->angle - vang[0])
        {
            cmd->buttons |= BT_ATTACK;
            static const int hitscans[] = {wp_pistol, wp_shotgun, wp_chaingun,
                wp_supershotgun};
            switch (pl->readyweapon)
            {
                case wp_fist:
                case wp_missile:
                case wp_plasma:
                case wp_bfg:
                case wp_chainsaw:
                    cmd->buttons |= BT_CHANGE;
                    cmd->buttons |= hitscans[random() % earrlen(hitscans)];
                    break;
                    
                default:
                    break;
            }
        }
    }
    else if (pl->readyweapon == wp_missile && emax(D_abs(mx - nx),
                                              D_abs(my - ny)) <= 128 * FRACUNIT)
    {
        cmd->buttons |= BT_CHANGE;
        cmd->buttons |= random.range(wp_pistol, wp_supershotgun)
        << BT_WEAPONSHIFT;
    }
    else if (random.range(1, 100) == 1)
    {
        cmd->buttons |= BT_CHANGE;
        cmd->buttons |= random.range(wp_shotgun, wp_supershotgun)
        << BT_WEAPONSHIFT;
    }

    if (pl->readyweapon == wp_fist || pl->readyweapon == wp_chainsaw)
    {
        if (!target.isLine && target.mobj->info->dehnum == MT_BARREL)
        {
            cmd->buttons |= BT_CHANGE;
            cmd->buttons |= random.range(wp_pistol, wp_supershotgun)
            << BT_WEAPONSHIFT;
        }
        cmd->forwardmove = FixedMul(2 * pl->pclass->forwardmove[1],
            B_AngleCosine(dangle));
        cmd->sidemove = -FixedMul(2 * pl->pclass->sidemove[1],
                                  B_AngleSine(dangle));
    }
    else
    {
        if (P_AproxDistance(nx - mx, ny - my) < 384 * FRACUNIT)
        {
            //cmd->sidemove /= -1;
            if (cmd->forwardmove > 0)
            {
                cmd->forwardmove = 0;

                cmd->sidemove += random.range(-pl->pclass->sidemove[0],
                                              pl->pclass->sidemove[0]) * 8;
                cmd->forwardmove += random.range(-pl->pclass->forwardmove[0],
                                                 pl->pclass->forwardmove[0]) * 8;
            }
        }
        cmd->sidemove += random.range(-pl->pclass->sidemove[0],
            pl->pclass->sidemove[0]);
        cmd->forwardmove += random.range(-pl->pclass->forwardmove[0],
            pl->pclass->forwardmove[0]);

    }

    
}

//
// Bot::doNonCombatAI
//
// Does whatever needs to be done when not fighting
//
void Bot::doNonCombatAI()
{
    if (!m_hasPath/* || m_goalTimer > 105*/)
    {
        // TODO: object of interest
        LevelStateStack::SetKeyPlayer(pl);
        if(/*m_goalTimer > 105 || */!m_finder.FindNextGoal(pl->mo->x, pl->mo->y, m_path, objOfInterest, this))
        {
            ++m_searchstage;
            cmd->sidemove += random.range(-pl->pclass->sidemove[0],
                pl->pclass->sidemove[0]);
            cmd->forwardmove += random.range(-pl->pclass->forwardmove[0],
                pl->pclass->forwardmove[0]);
//            if(m_goalTimer > 105)
//            {
//                ++m_goalTimer;
//                if(m_goalTimer > 140)
//                    m_goalTimer = 0;
//            }
            return;
        }
        m_hasPath = true;
    }
    //if(!path.exists())
    //{
    //   if(!routePath())
    //   {
    //      cmd->sidemove += random.range(-pl->pclass->sidemove[0],
    //                                    pl->pclass->sidemove[0]);
    //      cmd->forwardmove += random.range(-pl->pclass->forwardmove[0],
    //                                    pl->pclass->forwardmove[0]);
    //      return;
    //   }
    //}

    // found path to exit
    fixed_t mx, my, nx, ny;
    bool dontMove = false;
    const BSubsec* nextss = nullptr;
    if (ss == m_path.last)
    {
        nx = m_path.end.x;
        ny = m_path.end.y;
        m_lastPathSS = ss;
//        ++m_goalTimer;
    }
    else
    {
//        m_goalTimer = 0;
        const BSeg* seg;
        // from end to path to beginning
        for (const BNeigh** nit = m_path.inv.begin(); nit != m_path.inv.end();
             ++nit)
        {
            seg = (*nit)->seg;
            if (!botMap->canPass(*seg->owner, *(*nit)->ss, pl->mo->height))
            {
                break;
            }
            if (seg->owner == ss)
            {
                v2fixed_t nn = B_ProjectionOnSegment(pl->mo->x, pl->mo->y,
                                                     seg->v[0]->x, seg->v[0]->y,
                                                     seg->dx, seg->dy);
                nx = nn.x;
                ny = nn.y;
                if(!botMap->canPassNow(*seg->owner, *(*nit)->ss, pl->mo->height))
                {
                    dontMove = true;
                }
                {
                    const sector_t* nsector = (*nit)->ss->msector->getCeilingSector();
                    const sector_t* msector = ss->msector->getCeilingSector();
                    
                    if(nsector != msector)
                    {
                        const CeilingThinker* ct = thinker_cast
                        <const CeilingThinker*>
                        (nsector->ceilingdata);
                        
                        if(ct && ct->crush > 0 && ct->direction == plat_down)
                        {
                            dontMove = true;
                        }
                    }
                }
                m_lastPathSS = ss;
                if(random() % 64 == 0 && m_dropSS.count(ss))
                {
                    B_Log("Removed goner %d\n",
                          (int)(ss - &botMap->ssectors[0]));
                    m_dropSS.erase(ss);
                }
                nextss = (*nit)->ss;
                goto moveon;
            }
        }
        // not on path, so reset
        if(m_lastPathSS)
        {
            if(!botMap->canPassNow(*ss, *m_lastPathSS,
                                   pl->mo->height))
            {
                B_Log("Inserted goner %d\n",
                      (int)(m_lastPathSS - &botMap->ssectors[0]));
                m_dropSS.insert(m_lastPathSS);
                for(const BNeigh& n : m_lastPathSS->neighs)
                {
                    if(P_AproxDistance(n.ss->mid.x - m_lastPathSS->mid.x,
                                       n.ss->mid.y - m_lastPathSS->mid.y)
                       < 128 * FRACUNIT)
                    {
                        m_dropSS.insert(n.ss);
                    }
                }
            }
            m_lastPathSS = nullptr;
        }
        m_searchstage = 0;
        m_hasPath = false;
        if(random() % 3 == 0)
            m_justGotLost = true;
        return;
    }
moveon:
    //int nowon = path.straightPathCoordsIndex(pl->mo->x, pl->mo->y);
    //
    //if(nowon < 0 || prevCtr % 100 == 0)
    //{
    //	// Reset if out of the path, or if a pushwall stopped moving
    //     searchstage = 0;
    //	path.reset();
    //	return;
    //}
    //
    mx = pl->mo->x;
    my = pl->mo->y;
    bool intoSwitch = false;
    if (goalTable.hasKey(BOT_WALKTRIG)
        && P_AproxDistance(mx - m_path.end.x, my - m_path.end.y)
        < 2 * pl->mo->radius)
    {
        intoSwitch = true;
        if(prevCtr % 2 == 0)
            cmd->buttons |= BT_USE;
    }
    else if(nextss)
    {
        const sector_t* nextsec = nextss->msector->getCeilingSector();
        if(!nextsec->ceilingdata
           && botMap->sectorFlags[nextsec - ::sectors].isDoor)
        {
            intoSwitch = true;
            if(prevCtr % 2 == 0)
                cmd->buttons |= BT_USE;
        }
    }
    //
    //  int nexton = path.getNextStraightIndex(nowon);


    //if(nexton == -1)
    //   path.getFinalCoord(nx, ny);
    //else
    //   path.getStraightCoords(nexton, nx, ny);

    if (goalAchieved())
    {
        m_searchstage = 0;
        m_hasPath = false;
        return;
        //path.reset();  // only reset if reached destination
    }

    bool moveslow = false;
    if(m_justGotLost)
    {
        moveslow = (m_justGotLost && P_AproxDistance(mx - m_path.start.x,
                                                         my - m_path.start.y)
                                        < pl->mo->radius * 2);
        
        if(!moveslow)
            m_justGotLost = false;
    }
    moveslow |= m_dropSS.count(ss) ? true : false;
    
    angle_t tangle = P_PointToAngle(mx, my, nx, ny);
    angle_t dangle = tangle - pl->mo->angle;

    //   if(dangle < ANG45 || dangle > ANG270 + ANG45)


    if (random() % 128 == 0)
        m_straferunstate = random.range(-1, 1);
    if(!intoSwitch)
        tangle += ANG45 * m_straferunstate;

    int16_t angleturn = (int16_t)(tangle >> 16) - (int16_t)(pl->mo->angle >> 16);
    angleturn >>= 3;

    if (angleturn > 1500)
        angleturn = 1500;
    if (angleturn < -1500)
        angleturn = -1500;

    if (!dontMove && !(P_AproxDistance(m_path.end.x - mx, m_path.end.y - my)
                       < 16 * FRACUNIT
          && D_abs(angleturn) > 300))
    {
        cmd->forwardmove += FixedMul((moveslow ? 1 : 2)
                                     * pl->pclass->forwardmove[moveslow ? 0 : 1],
                                     B_AngleCosine(dangle));
        if(intoSwitch && ss == m_path.last && cmd->forwardmove < 0)
        {
            cmd->forwardmove = 0;
        }
        else
        {
            cmd->sidemove -= FixedMul((moveslow ? 1 : 2)
                                      * pl->pclass->sidemove[moveslow ? 0 : 1],
                                      B_AngleSine(dangle));
        }
    }

   
   cmd->angleturn += angleturn;
//   printf("%d\n", angleturn);
}



//
// Bot::doCommand
//
// Called from G_Ticker right before ticcmd is passed into the player. Gets the
// tic command which may have already been copied to the player, and updates it
// with bot output. Cannot just reset what was produced by G_BuildTiccmd,
// because that also handles unrelated stuff.
//
void Bot::doCommand()
{
   if(!active)
      return;  // do nothing if out of game
   
   ++prevCtr;
   
   // Get current values
   ss = &botMap->pointInSubsector(pl->mo->x, pl->mo->y);
   cmd = &pl->cmd;
   
   // Do non-combat for now

   {
       doNonCombatAI();
   }
    Target target;
    enemyVisible(target);
    if (target.exists)
   {
       //if (!m_hasPath || ss != m_path.last)
       {
           cmd->angleturn = 0;
           doCombatAI(target);
       }
   }
   
   // Limit commands before exiting
   capCommands();
}

//
// Bot::InitBots
//
// Must be called from initialization to set the player references (both bots
// and players are allocated globally). Note that they already start active.
//
void Bot::InitBots()
{
   for (int i = 0; i < MAXPLAYERS; ++i)
   {
      bots[i].pl = players + i;
   }
}

//
// Bot::getNopickStats
//
// Gets the nopick state, creating one if not existing
//
PlayerStats &Bot::getNopickStats(spritenum_t spnum)
{
   auto nopick = nopickStats.find(spnum);
   if(nopick == nopickStats.cend())
   {
      nopickStats.emplace(spnum, PlayerStats(true));
      return nopickStats.find(spnum)->second;
   }

   return nopick->second;
}

//
// Bot::getEffectStats
//
// Gets the effect state, creating one if not existing
//
PlayerStats &Bot::getEffectStats(spritenum_t spnum)
{
   auto effect = effectStats.find(spnum);
   if(effect == effectStats.cend())
   {
      effectStats.emplace(spnum, PlayerStats(false));
      return effectStats.find(spnum)->second;
   }

   return effect->second;
}

// EOF

