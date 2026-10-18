#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/ObjectExtension/ObjectExtension.h"

extern "C" {
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * DROP_SPAWN / DROP_MOVE / DROP_GONE
 *
 * Loose things that appear during gameplay are shared between teammates in the same scene:
 *  - hearts, rupees, magic jars, ammo and other pickups that fall out of enemies, pots, grass, rocks, ...
 *  - fairies, bugs and fish (the things you can catch in a bottle)
 *
 * When one appears on a player's game it is spawned on the teammates' games too, it follows the original while it
 * moves, and when anyone picks it up (or it disappears) it disappears for everybody, so only one player gets it.
 * Pickups placed in the scene with their own flag (heart pieces, keys, ...) already sync through flags and are left
 * alone.
 */

struct DropNet {
    uint32_t owner = 0;
    uint32_t dropId = 0;
    Vec3f lastSent = { 0.0f, 0.0f, 0.0f };
};
static ObjectExtension::Register<DropNet> DropNetRegister;

static uint32_t sNextDropId = 1;
static bool sApplyingRemoteDrop = false;
static uint32_t sDropSendTimer = 0;

static constexpr s16 FAIRY_HEAL_TIMED_PARAMS = 2;
static constexpr s16 FAIRY_HEAL_PARAMS = 6;
static constexpr s16 FAIRY_HEAL_BIG_PARAMS = 7;

static bool IsSharedDrop(Actor* actor) {
    switch (actor->id) {
        case ACTOR_EN_ITEM00:
            // Pickups with their own collectible flag are synced through the flag instead
            return ((actor->params & 0x3F00) >> 8) == 0;
        case ACTOR_EN_INSECT:
        case ACTOR_EN_FISH:
            return true;
        case ACTOR_EN_ELF:
            // Only the free healing fairies, never Navi or fairies with a special job
            return actor->params == FAIRY_HEAL_TIMED_PARAMS || actor->params == FAIRY_HEAL_PARAMS ||
                   actor->params == FAIRY_HEAL_BIG_PARAMS;
        default:
            return false;
    }
}

static Actor* FindDrop(uint32_t owner, uint32_t dropId) {
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->update != nullptr) {
                DropNet* net = ObjectExtension::GetInstance().Get<DropNet>(actor);
                if (net != nullptr && net->owner == owner && net->dropId == dropId) {
                    return actor;
                }
            }
            actor = actor->next;
        }
    }
    return nullptr;
}

// MARK: - Local events

void Anchor::Drops_OnActorInit(Actor* actor) {
    if (sApplyingRemoteDrop || !roomState.syncInteractions || !IsSaveLoaded() || !IsSharedDrop(actor)) {
        return;
    }

    // Things placed in the scene exist on every client already, only share what appears during gameplay
    if (coop.framesInScene < 10 || !Coop_AnyTeammateInScene()) {
        return;
    }

    uint32_t dropId = sNextDropId++;
    ObjectExtension::GetInstance().Set<DropNet>(actor, DropNet{ ownClientId, dropId, actor->world.pos });

    nlohmann::json payload;
    payload["type"] = DROP_SPAWN;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["dropId"] = dropId;
    payload["actorId"] = actor->id;
    payload["params"] = actor->params;
    payload["pos"] = actor->world.pos;
    payload["rotY"] = actor->shape.rot.y;
    Coop_SendToTeammatesInScene(payload);
}

void Anchor::Drops_OnActorKill(Actor* actor) {
    if (sApplyingRemoteDrop || !roomState.syncInteractions || !IsSaveLoaded()) {
        return;
    }

    DropNet* net = ObjectExtension::GetInstance().Get<DropNet>(actor);
    if (net == nullptr) {
        return;
    }

    // Unloading a room or leaving the scene isn't anyone picking it up
    if (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || gPlayState->roomCtx.status != 0) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = DROP_GONE;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["owner"] = net->owner;
    payload["dropId"] = net->dropId;
    Coop_SendToTeammatesInScene(payload);
}

// Moving drops (fairies, bugs, fish, a heart that is still falling) follow whoever spawned them
void Anchor::Coop_UpdateDrops() {
    if (!roomState.syncInteractions || !Coop_AnyTeammateInScene()) {
        return;
    }
    if (++sDropSendTimer < RateToInterval(roomState.objectUpdateRate)) {
        return;
    }
    sDropSendTimer = 0;

    nlohmann::json moves = nlohmann::json::array();
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->update != nullptr) {
                DropNet* net = ObjectExtension::GetInstance().Get<DropNet>(actor);
                if (net != nullptr && net->owner == ownClientId &&
                    Math_Vec3f_DistXYZ(&actor->world.pos, &net->lastSent) > 2.0f) {
                    net->lastSent = actor->world.pos;
                    moves.push_back({ net->dropId, (s32)actor->world.pos.x, (s32)actor->world.pos.y,
                                      (s32)actor->world.pos.z, actor->shape.rot.y });
                }
            }
            actor = actor->next;
        }
    }

    if (!moves.empty()) {
        nlohmann::json payload;
        payload["type"] = DROP_MOVE;
        payload["sceneNum"] = gPlayState->sceneNum;
        payload["list"] = moves;
        payload["quiet"] = true;
        Coop_SendToTeammatesInScene(payload);
    }
}

// MARK: - Incoming

void Anchor::HandlePacket_DropSpawn(nlohmann::json payload) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    uint32_t owner = payload.at("clientId").get<uint32_t>();
    uint32_t dropId = payload.at("dropId").get<uint32_t>();
    if (FindDrop(owner, dropId) != nullptr) {
        return;
    }

    Vec3f pos = payload.at("pos").get<Vec3f>();
    sApplyingRemoteDrop = true;
    Actor* actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, payload.at("actorId").get<s16>(), pos.x, pos.y, pos.z,
                               0, payload.at("rotY").get<s16>(), 0, payload.at("params").get<s16>());
    sApplyingRemoteDrop = false;

    if (actor != nullptr) {
        ObjectExtension::GetInstance().Set<DropNet>(actor, DropNet{ owner, dropId, pos });
    }
}

void Anchor::HandlePacket_DropMove(nlohmann::json payload) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    uint32_t owner = payload.at("clientId").get<uint32_t>();
    for (auto& entry : payload.at("list")) {
        Actor* actor = FindDrop(owner, entry.at(0).get<uint32_t>());
        if (actor == nullptr) {
            continue;
        }
        actor->world.pos = { (f32)entry.at(1).get<s32>(), (f32)entry.at(2).get<s32>(), (f32)entry.at(3).get<s32>() };
        actor->shape.rot.y = entry.at(4).get<s16>();
    }
}

void Anchor::HandlePacket_DropGone(nlohmann::json payload) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    Actor* actor = FindDrop(payload.at("owner").get<uint32_t>(), payload.at("dropId").get<uint32_t>());
    if (actor != nullptr) {
        sApplyingRemoteDrop = true;
        Actor_Kill(actor);
        sApplyingRemoteDrop = false;
    }
}
