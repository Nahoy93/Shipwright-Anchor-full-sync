#include <nlohmann/json.hpp>
#include <set>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/Notification/Notification.h"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "seqcmd.h"
#include "src/overlays/actors/ovl_Door_Warp1/z_door_warp1.h"
extern PlayState* gPlayState;
}

/**
 * ENEMY_DEFEATED / BOSS_DEFEATED / WARP_SPAWNED
 *
 * Enemies are matched across clients by scene, room, actor id, params and home position. Those all come from the
 * scene's actor list, so they're identical on every client for enemies placed in the scene. Enemies spawned at runtime
 * (by spawners, other enemies, ...) usually won't match and are simply left alone.
 *
 * Bosses are killed on teammates that are in the same boss room, and the blue warp that appears after the boss fight is
 * spawned for them too, so nobody gets stuck in a locked boss room.
 */

static bool sSpawningRemoteWarp = false;

struct PendingRoomKill {
    s8 roomNum;
    s16 actorId;
    s16 params;
    Vec3f home;
    uint32_t killer;
    s16 sceneNum;
};
static std::vector<PendingRoomKill> sPendingRoomKills;

static void KillActorWithEffect(Actor* actor) {
    static Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
    Vec3f pos = actor->world.pos;
    pos.y += 20.0f;

    EffectSsDeadDb_Spawn(gPlayState, &pos, &zeroVec, &zeroVec, 100, 0, 255, 255, 255, 255, 0, 0, 255, 1, 9, true);
    Actor_Kill(actor);
}

// Don't kill an enemy that is holding the player (like likes, redeads, wallmasters...), the player would stay stuck
static bool IsHoldingPlayer(Actor* actor) {
    Player* player = GET_PLAYER(gPlayState);
    return player->actor.parent == actor;
}

// MARK: - Enemies

void Anchor::SendPacket_EnemyDefeated(Actor* actor) {
    if (!IsSaveLoaded() || !roomState.syncEnemies || isProcessingIncomingPacket || actor == nullptr) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = ENEMY_DEFEATED;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"] = actor->room;
    payload["actorId"] = actor->id;
    payload["params"] = actor->params;
    payload["home"] = actor->home.pos;

    // Enemies tracked by the movement sync are found by their network id instead (works for spawned enemies too)
    uint32_t netAuthority, netId;
    if (EnemySync_GetNetId(actor, &netAuthority, &netId)) {
        payload["netAuthority"] = netAuthority;
        payload["netId"] = netId;
    }

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_EnemyDefeated(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncEnemies) {
        return;
    }

    s16 sceneNum = payload.at("sceneNum").get<s16>();
    if (sceneNum != gPlayState->sceneNum) {
        return;
    }

    if (payload.contains("netId")) {
        Actor* netActor =
            EnemySync_FindByNetId(payload.at("netAuthority").get<uint32_t>(), payload.at("netId").get<uint32_t>());
        if (netActor != nullptr) {
            if (!IsHoldingPlayer(netActor)) {
                KillActorWithEffect(netActor);
            }
            return;
        }
    }

    s8 roomNum = payload.at("roomNum").get<s8>();
    s16 actorId = payload.at("actorId").get<s16>();
    s16 params = payload.at("params").get<s16>();
    Vec3f home = payload.at("home").get<Vec3f>();

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head;
    while (actor != nullptr) {
        if (actor->update != nullptr && actor->id == actorId && actor->params == params && actor->room == roomNum &&
            Math_Vec3f_DistXYZ(&actor->home.pos, &home) < 1.0f) {
            if (!IsHoldingPlayer(actor)) {
                KillActorWithEffect(actor);
            }
            return;
        }
        actor = actor->next;
    }

    // Not here: the teammate is in a room we don't have loaded. Remember the kill so the enemy is already dead when we
    // enter that room, for as long as the teammate stays there (rooms refill with enemies once everyone has left).
    sPendingRoomKills.push_back(
        { roomNum, actorId, params, home, payload.value("clientId", (uint32_t)0), gPlayState->sceneNum });
}

void Anchor::Coop_ApplyPendingRoomKills() {
    if (sPendingRoomKills.empty() || !IsSaveLoaded()) {
        return;
    }

    for (auto it = sPendingRoomKills.begin(); it != sPendingRoomKills.end();) {
        bool killerStillThere = it->sceneNum == gPlayState->sceneNum && clients.contains(it->killer) &&
                                clients[it->killer].online && clients[it->killer].sceneNum == it->sceneNum &&
                                clients[it->killer].curRoomNum == it->roomNum;
        if (!killerStillThere) {
            it = sPendingRoomKills.erase(it);
            continue;
        }

        if (it->roomNum == gPlayState->roomCtx.curRoom.num) {
            Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ENEMY].head;
            while (actor != nullptr) {
                if (actor->update != nullptr && actor->id == it->actorId && actor->params == it->params &&
                    actor->room == it->roomNum && Math_Vec3f_DistXYZ(&actor->home.pos, &it->home) < 1.0f) {
                    // Quietly, it died before we got here
                    Actor_Kill(actor);
                    break;
                }
                actor = actor->next;
            }
        }
        ++it;
    }
}

// MARK: - Bosses

static bool IsSyncedBoss(s16 actorId) {
    // The Ganondorf and Ganon fights are left alone, those run into the ending sequence
    return actorId != ACTOR_BOSS_GANON && actorId != ACTOR_BOSS_GANON2;
}

void Anchor::SendPacket_BossDefeated(Actor* actor) {
    if (!IsSaveLoaded() || !roomState.syncEnemies || isProcessingIncomingPacket || actor == nullptr ||
        !IsSyncedBoss(actor->id)) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = BOSS_DEFEATED;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actorId"] = actor->id;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_BossDefeated(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncEnemies) {
        return;
    }

    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    std::string name = clients.contains(clientId) ? clients[clientId].name : "A teammate";
    s16 sceneNum = payload.at("sceneNum").get<s16>();
    s16 actorId = payload.at("actorId").get<s16>();

    if (!IsSyncedBoss(actorId)) {
        return;
    }

    if (sceneNum != gPlayState->sceneNum) {
        Notification::Emit({ .prefix = name, .message = "defeated a boss!" });
        return;
    }

    Notification::Emit({
        .prefix = name,
        .message = "defeated the boss! A blue warp will appear shortly",
    });

    // Killing a boss in the middle of its intro would leave the camera stuck, wait for the intro to finish
    if (Player_InCsMode(gPlayState) || gPlayState->csCtx.state != CS_STATE_IDLE) {
        coop.pendingBossKillScene = sceneNum;
        return;
    }

    Coop_KillBosses();
}

void Anchor::Coop_ProcessPendingBossKill() {
    if (coop.pendingBossKillScene < 0) {
        return;
    }

    if (coop.pendingBossKillScene != gPlayState->sceneNum || !roomState.syncEnemies) {
        coop.pendingBossKillScene = -1;
        return;
    }

    if (Player_InCsMode(gPlayState) || gPlayState->csCtx.state != CS_STATE_IDLE) {
        return;
    }

    coop.pendingBossKillScene = -1;
    Coop_KillBosses();
}

void Anchor::Coop_KillBosses() {
    // Kill every boss actor (bosses are often made of several actors), then anything that was attached to them
    std::set<Actor*> killed;
    Actor* actor;
    actor = gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head;
    while (actor != nullptr) {
        if (actor->update != nullptr) {
            KillActorWithEffect(actor);
            killed.insert(actor);
        }
        actor = actor->next;
    }

    for (int category = 0; category < ACTORCAT_MAX; category++) {
        if (category == ACTORCAT_PLAYER) {
            continue;
        }
        actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->update != nullptr && actor->parent != nullptr && killed.contains(actor->parent) &&
                !IsHoldingPlayer(actor)) {
                Actor_Kill(actor);
            }
            actor = actor->next;
        }
    }

    // Stop the boss music
    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 1);
}

// MARK: - Blue warp

void Anchor::SendPacket_WarpSpawned(Actor* actor) {
    if (!IsSaveLoaded() || !roomState.syncEnemies || sSpawningRemoteWarp || isProcessingIncomingPacket) {
        return;
    }

    // Only the blue warps that appear after a boss fight
    if (actor->params != WARP_DUNGEON_CHILD && actor->params != WARP_DUNGEON_ADULT) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = WARP_SPAWNED;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["params"] = actor->params;
    payload["posRot"] = actor->world;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_WarpSpawned(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.syncEnemies) {
        return;
    }

    if (payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    // Don't spawn a second warp if this client already has one
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->id == ACTOR_DOOR_WARP1 && actor->update != nullptr) {
                return;
            }
            actor = actor->next;
        }
    }

    PosRot posRot = payload.at("posRot").get<PosRot>();
    s16 params = payload.at("params").get<s16>();

    sSpawningRemoteWarp = true;
    Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_DOOR_WARP1, posRot.pos.x, posRot.pos.y, posRot.pos.z, 0, 0, 0,
                params);
    sSpawningRemoteWarp = false;
}
