#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/ObjectExtension/ObjectExtension.h"

extern "C" {
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * ENEMY_SYNC / ENEMY_HIT  (experimental)
 *
 * Keeps enemies (and boss health) in sync between teammates standing in the same room.
 *
 * One player per room is the "authority": the teammate with the lowest client id among those in the same scene and
 * room. The authority gives every enemy in the room a network id and sends a snapshot of all of them ten times a
 * second (ENEMY_SYNC): actor id, params, position, rotation and health.
 *
 * The other players ("followers") match each snapshot entry to one of their own enemies (same actor id and params,
 * closest one nearby) and from then on:
 *  - pull that enemy towards the authority's position and rotation after it has updated (its own AI and hitboxes keep
 *    running, so it can still attack and be attacked)
 *  - lower its health to the authority's health
 *  - report damage they deal to it (ENEMY_HIT) so the authority, and through its snapshots everyone else, applies it
 *  - spawn enemies the authority has but they don't (enemies spawned during gameplay), and remove enemies the
 *    authority no longer has
 *
 * Bosses only share their health, moving them around would break their scripted phases.
 *
 * Deaths still go through ENEMY_DEFEATED, which uses the network id when there is one.
 */

struct EnemyNet {
    uint32_t netId = 0;
    uint32_t authority = 0;
    u8 lastHealth = 0;
};
static ObjectExtension::Register<EnemyNet> EnemyNetRegister;

struct EnemyTarget {
    s16 actorId = 0;
    s16 params = 0;
    Vec3f pos = { 0.0f, 0.0f, 0.0f };
    s16 rotY = 0;
    u8 health = 0;
    bool isBoss = false;
    bool hasParent = false;
    u8 missing = 0;
    uint32_t lastSnapshot = 0;
};

static std::unordered_map<uint32_t, EnemyTarget> sTargets;
static uint32_t sTargetAuthority = 0;
static uint32_t sSnapshotSeq = 0;
static uint32_t sNextNetId = 1;
static uint32_t sSendTimer = 0;
static uint32_t sCurrentAuthority = 0;
static bool sIsFollower = false;

static constexpr float MATCH_DISTANCE_MAX = 100000.0f;
static constexpr float SNAP_DISTANCE = 250.0f;
static constexpr float FOLLOW_FACTOR = 0.3f;
static constexpr int MAX_SPAWNS_PER_SNAPSHOT = 3;
static constexpr u8 SNAPSHOTS_BEFORE_SPAWN = 5;

static bool IsSyncedBossActor(Actor* actor) {
    return actor->id != ACTOR_BOSS_GANON && actor->id != ACTOR_BOSS_GANON2;
}

static bool IsInRoom(Actor* actor) {
    return actor->room == -1 || actor->room == gPlayState->roomCtx.curRoom.num;
}

static std::vector<Actor*> GetSyncableEnemies() {
    std::vector<Actor*> result;
    for (int category : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->update != nullptr && IsInRoom(actor) &&
                (category != ACTORCAT_BOSS || IsSyncedBossActor(actor))) {
                result.push_back(actor);
            }
            actor = actor->next;
        }
    }
    return result;
}

static bool IsHoldingPlayerActor(Actor* actor) {
    return GET_PLAYER(gPlayState)->actor.parent == actor;
}

static void ClearFollowerState() {
    sTargets.clear();
    sTargetAuthority = 0;
}

void Anchor::EnemySync_Reset() {
    ClearFollowerState();
    sSendTimer = 0;
    sCurrentAuthority = 0;
    sIsFollower = false;
}

uint32_t Anchor::EnemySync_GetAuthority() {
    if (!IsSaveLoaded() || ownClientId == 0) {
        return 0;
    }

    s8 room = gPlayState->roomCtx.curRoom.num;
    uint32_t best = ownClientId;
    bool anyTeammate = false;

    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client) && client.sceneNum == gPlayState->sceneNum && client.curRoomNum == room) {
            anyTeammate = true;
            best = MIN(best, clientId);
        }
    }

    return anyTeammate ? best : 0;
}

bool Anchor::EnemySync_GetNetId(Actor* actor, uint32_t* authority, uint32_t* netId) {
    EnemyNet* net = ObjectExtension::GetInstance().Get<EnemyNet>(actor);
    if (net == nullptr || net->netId == 0) {
        return false;
    }
    *authority = net->authority;
    *netId = net->netId;
    return true;
}

Actor* Anchor::EnemySync_FindByNetId(uint32_t authority, uint32_t netId) {
    for (int category : { ACTORCAT_ENEMY, ACTORCAT_BOSS }) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->update != nullptr) {
                EnemyNet* net = ObjectExtension::GetInstance().Get<EnemyNet>(actor);
                if (net != nullptr && net->authority == authority && net->netId == netId) {
                    return actor;
                }
            }
            actor = actor->next;
        }
    }
    return nullptr;
}

uint32_t Anchor::RateToInterval(u8 perSecond) {
    if (perSecond == 0) {
        perSecond = 1;
    }
    // Round to the nearest whole number of game updates
    return MAX(1, (20 + perSecond / 2) / MIN(perSecond, 20));
}

// MARK: - Per frame

void Anchor::Coop_UpdateEnemySync() {
    if (!roomState.syncEnemyMovement) {
        if (sCurrentAuthority != 0) {
            EnemySync_Reset();
        }
        return;
    }

    uint32_t authority = EnemySync_GetAuthority();
    sCurrentAuthority = authority;
    sIsFollower = authority != 0 && authority != ownClientId;

    if (authority == 0) {
        ClearFollowerState();
        return;
    }

    if (authority == ownClientId) {
        ClearFollowerState();
        // Room setting: snapshots per second
        if (++sSendTimer >= RateToInterval(roomState.enemyUpdateRate)) {
            sSendTimer = 0;
            SendPacket_EnemySync();
        }
        return;
    }

    if (authority != sTargetAuthority) {
        ClearFollowerState();
        sTargetAuthority = authority;
    }
}

void Anchor::EnemySync_OnActorUpdate(Actor* actor) {
    if (!sIsFollower || actor->update == nullptr ||
        (actor->category != ACTORCAT_ENEMY && actor->category != ACTORCAT_BOSS)) {
        return;
    }

    EnemyNet* net = ObjectExtension::GetInstance().Get<EnemyNet>(actor);
    if (net == nullptr || net->authority != sCurrentAuthority) {
        return;
    }

    auto it = sTargets.find(net->netId);
    if (it == sTargets.end()) {
        return;
    }
    EnemyTarget& target = it->second;

    // Health: report damage we did, take damage others did. Never set it to 0 here, deaths go through ENEMY_DEFEATED
    u8 health = actor->colChkInfo.health;
    if (health < net->lastHealth && health > 0) {
        nlohmann::json payload;
        payload["type"] = ENEMY_HIT;
        payload["targetClientId"] = sCurrentAuthority;
        payload["netId"] = net->netId;
        payload["health"] = health;
        SendJsonToRemote(payload);
        target.health = MIN(target.health, health);
    }
    if (target.health > 0 && target.health < health) {
        actor->colChkInfo.health = target.health;
    }
    net->lastHealth = actor->colChkInfo.health;

    // Movement
    if (target.isBoss || IsHoldingPlayerActor(actor)) {
        return;
    }

    float dist = Math_Vec3f_DistXYZ(&actor->world.pos, &target.pos);
    if (dist > SNAP_DISTANCE) {
        actor->world.pos = target.pos;
    } else {
        actor->world.pos.x += (target.pos.x - actor->world.pos.x) * FOLLOW_FACTOR;
        actor->world.pos.y += (target.pos.y - actor->world.pos.y) * FOLLOW_FACTOR;
        actor->world.pos.z += (target.pos.z - actor->world.pos.z) * FOLLOW_FACTOR;
    }
    Math_SmoothStepToS(&actor->world.rot.y, target.rotY, 2, 0x2000, 0);
    actor->shape.rot.y = actor->world.rot.y;
}

// MARK: - Packets

void Anchor::SendPacket_EnemySync() {
    nlohmann::json enemies = nlohmann::json::array();

    for (Actor* actor : GetSyncableEnemies()) {
        EnemyNet* net = ObjectExtension::GetInstance().Get<EnemyNet>(actor);
        if (net == nullptr || net->authority != ownClientId) {
            ObjectExtension::GetInstance().Set<EnemyNet>(
                actor, EnemyNet{ sNextNetId++, ownClientId, actor->colChkInfo.health });
            net = ObjectExtension::GetInstance().Get<EnemyNet>(actor);
        }

        enemies.push_back({ net->netId, actor->id, actor->params, (s32)actor->world.pos.x, (s32)actor->world.pos.y,
                            (s32)actor->world.pos.z, actor->shape.rot.y, actor->colChkInfo.health,
                            actor->category == ACTORCAT_BOSS ? 1 : 0, actor->parent != nullptr ? 1 : 0 });
    }

    nlohmann::json payload;
    payload["type"] = ENEMY_SYNC;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"] = gPlayState->roomCtx.curRoom.num;
    payload["enemies"] = enemies;
    payload["quiet"] = true;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_EnemySync(nlohmann::json payload) {
    if (!roomState.syncEnemyMovement || !IsSaveLoaded()) {
        return;
    }

    uint32_t sender = payload.at("clientId").get<uint32_t>();
    if (payload.at("sceneNum").get<s16>() != gPlayState->sceneNum ||
        payload.at("roomNum").get<s8>() != gPlayState->roomCtx.curRoom.num || EnemySync_GetAuthority() != sender) {
        return;
    }

    if (sender != sTargetAuthority) {
        ClearFollowerState();
        sTargetAuthority = sender;
    }

    sSnapshotSeq++;
    for (auto& entry : payload.at("enemies")) {
        uint32_t netId = entry.at(0).get<uint32_t>();
        EnemyTarget& target = sTargets[netId];
        target.actorId = entry.at(1).get<s16>();
        target.params = entry.at(2).get<s16>();
        target.pos = { (f32)entry.at(3).get<s32>(), (f32)entry.at(4).get<s32>(), (f32)entry.at(5).get<s32>() };
        target.rotY = entry.at(6).get<s16>();
        target.health = entry.at(7).get<u8>();
        target.isBoss = entry.at(8).get<int>() != 0;
        target.hasParent = entry.at(9).get<int>() != 0;
        target.lastSnapshot = sSnapshotSeq;
    }

    // Split our own enemies into the ones already matched to this authority and the rest
    std::unordered_map<uint32_t, Actor*> mapped;
    std::vector<Actor*> unmapped;
    for (Actor* actor : GetSyncableEnemies()) {
        EnemyNet* net = ObjectExtension::GetInstance().Get<EnemyNet>(actor);
        if (net != nullptr && net->authority == sender && sTargets.contains(net->netId)) {
            mapped[net->netId] = actor;
        } else {
            unmapped.push_back(actor);
        }
    }

    int spawns = 0;
    for (auto& [netId, target] : sTargets) {
        if (target.lastSnapshot != sSnapshotSeq || mapped.contains(netId)) {
            target.missing = 0;
            continue;
        }

        // Match the closest unmatched enemy of the same kind. Any distance is fine: if the authority's copy wandered
        // off, ours is snapped over to it. Only when we have no such enemy at all is a new one spawned.
        Actor* best = nullptr;
        float bestDist = MATCH_DISTANCE_MAX;
        for (Actor* actor : unmapped) {
            if (actor->id != target.actorId || actor->params != target.params) {
                continue;
            }
            float dist = Math_Vec3f_DistXYZ(&actor->world.pos, &target.pos);
            if (dist < bestDist) {
                best = actor;
                bestDist = dist;
            }
        }

        if (best != nullptr) {
            ObjectExtension::GetInstance().Set<EnemyNet>(best, EnemyNet{ netId, sender, best->colChkInfo.health });
            unmapped.erase(std::find(unmapped.begin(), unmapped.end(), best));
            mapped[netId] = best;
            target.missing = 0;
            continue;
        }

        // The authority has an enemy we don't, spawn it. Only free standing ones: enemies that belong to a parent
        // actor (hands of Dead Hand, Gohma's eggs, ...) can't live without it and are left to their parent.
        if (target.isBoss || target.hasParent || ++target.missing < SNAPSHOTS_BEFORE_SPAWN ||
            spawns >= MAX_SPAWNS_PER_SNAPSHOT) {
            continue;
        }

        target.missing = 0;
        Actor* spawned = Actor_Spawn(&gPlayState->actorCtx, gPlayState, target.actorId, target.pos.x, target.pos.y,
                                     target.pos.z, 0, target.rotY, 0, target.params);
        if (spawned != nullptr) {
            spawns++;
            ObjectExtension::GetInstance().Set<EnemyNet>(spawned,
                                                         EnemyNet{ netId, sender, spawned->colChkInfo.health });
            mapped[netId] = spawned;
        }
    }

    // Enemies the authority no longer has (it despawned or died there): remove ours too
    for (auto it = sTargets.begin(); it != sTargets.end();) {
        if (it->second.lastSnapshot == sSnapshotSeq) {
            ++it;
            continue;
        }
        auto mappedIt = mapped.find(it->first);
        if (mappedIt != mapped.end() && !it->second.isBoss && !IsHoldingPlayerActor(mappedIt->second)) {
            Actor_Kill(mappedIt->second);
        }
        it = sTargets.erase(it);
    }
}

void Anchor::HandlePacket_EnemyHit(nlohmann::json payload) {
    if (!roomState.syncEnemyMovement || !IsSaveLoaded() || EnemySync_GetAuthority() != ownClientId) {
        return;
    }

    Actor* actor = EnemySync_FindByNetId(ownClientId, payload.at("netId").get<uint32_t>());
    u8 health = payload.at("health").get<u8>();
    if (actor != nullptr && health > 0 && health < actor->colChkInfo.health) {
        actor->colChkInfo.health = health;
        // Flash red like a normal hit
        Actor_SetColorFilter(actor, 0x4000, 255, 0, 8);
        EnemyNet* net = ObjectExtension::GetInstance().Get<EnemyNet>(actor);
        if (net != nullptr) {
            net->lastHealth = health;
        }
    }
}
