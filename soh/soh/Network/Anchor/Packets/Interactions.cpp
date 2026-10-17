#include <nlohmann/json.hpp>
#include <set>
#include <unordered_map>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/ObjectExtension/ObjectExtension.h"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "src/overlays/actors/ovl_En_Bom/z_en_bom.h"
#include "src/overlays/actors/ovl_En_Kusa/z_en_kusa.h"
#include "src/overlays/actors/ovl_Obj_Syokudai/z_obj_syokudai.h"
extern PlayState* gPlayState;
void EnKusa_SetupCut(EnKusa* this_);
void EnKusa_CutWaitRegrow(EnKusa* this_, PlayState* play);
void EnKusa_SpawnFragments(EnKusa* this_, PlayState* play);
}

/**
 * PROJECTILES / WORLD_OBJECTS / OBJECT_BROKEN / TORCH_LIT
 *
 * Makes what players do to the world visible and shared for teammates in the same scene:
 *
 *  - Projectiles (arrows incl. fire/ice/light, seeds, deku nuts, bombs, bombchus, boomerang, hookshot) and magic
 *    (Din's Fire, Farore's Wind, Nayru's Love) are mirrored: a copy is spawned on every teammate's game and follows
 *    the owner's one. The copies are only visuals (they don't run their own logic); the damage they do already reaches
 *    everyone through the enemy health sync. Bomb explosions are shown. Copies that draw themselves around "the
 *    player" (hookshot chain, Nayru's Love) are drawn around the owner's figure instead.
 *  - Pots, crates, rocks and bushes that are picked up, thrown or broken, grass that is cut, and push blocks (and other
 *    pushable objects) follow whoever moves them.
 *  - Torches lit by a teammate light up for everyone, so torch puzzles can be solved together.
 *
 * Objects placed in the scene are matched by actor id, params and home position, which are the same everywhere.
 * Objects spawned during gameplay are matched to the closest one of the same kind, and spawned for teammates that
 * don't have them.
 */

struct ProjectileNet {
    uint32_t projId = 0;
    uint32_t owner = 0;
    bool isMirror = false;
};
static ObjectExtension::Register<ProjectileNet> ProjectileNetRegister;

struct WorldObjectNet {
    Vec3f lastSent = { 0.0f, 0.0f, 0.0f };
    uint32_t puppetUntil = 0;
    s16 lastTorchTimer = 0;
    bool wasCut = false;
    bool initialized = false;
};
static ObjectExtension::Register<WorldObjectNet> WorldObjectNetRegister;

static uint32_t sNextProjId = 1;
static size_t sLastProjectileCount = 0;
static uint32_t sSendTimer = 0;
static bool sApplyingRemote = false;
static std::set<uint64_t> sShownExplosions;

static bool IsProjectile(s16 id) {
    return id == ACTOR_EN_ARROW || id == ACTOR_EN_BOM || id == ACTOR_EN_BOM_CHU || id == ACTOR_EN_BOOM ||
           id == ACTOR_ARMS_HOOK || id == ACTOR_MAGIC_FIRE || id == ACTOR_MAGIC_WIND || id == ACTOR_MAGIC_DARK;
}

// Fire/ice/light arrows are separate actors attached to the arrow
static bool IsArrowElement(s16 id) {
    return id == ACTOR_ARROW_FIRE || id == ACTOR_ARROW_ICE || id == ACTOR_ARROW_LIGHT;
}

static bool IsMirrorable(s16 id) {
    return IsProjectile(id) || IsArrowElement(id);
}

// Original draw/destroy of mirrors we wrap
struct MirrorFuncs {
    ActorFunc draw;
    ActorFunc destroy;
    uint32_t owner;
};
static std::unordered_map<Actor*, MirrorFuncs> sMirrorFuncs;

// Magic actors reset the local player's magic (and Nayru's Love) when they are created or removed. Mirrors must not
// touch the local player's state, so save and restore it around them.
struct LocalMagicState {
    u8 magicState;
    u8 prevMagicState;
    u16 nayrusLoveTimer;
};
static LocalMagicState SaveLocalMagic() {
    return { gSaveContext.magicState, gSaveContext.prevMagicState, gSaveContext.nayrusLoveTimer };
}
static void RestoreLocalMagic(const LocalMagicState& state) {
    gSaveContext.magicState = state.magicState;
    gSaveContext.prevMagicState = state.prevMagicState;
    gSaveContext.nayrusLoveTimer = state.nayrusLoveTimer;
}

static void MirrorDraw(Actor* actor, PlayState* play) {
    auto it = sMirrorFuncs.find(actor);
    if (it == sMirrorFuncs.end() || it->second.draw == nullptr) {
        return;
    }

    // Draw as if the owner's figure was the player
    Player* owner = nullptr;
    if (Anchor::Instance->clients.contains(it->second.owner)) {
        owner = Anchor::Instance->clients[it->second.owner].player;
    }
    if (owner == nullptr || owner->actor.update == nullptr) {
        return;
    }

    Actor* realPlayer = play->actorCtx.actorLists[ACTORCAT_PLAYER].head;
    u8 rightHandType = owner->rightHandType;
    if (actor->id == ACTOR_ARMS_HOOK) {
        owner->rightHandType = PLAYER_MODELTYPE_RH_HOOKSHOT;
    }
    play->actorCtx.actorLists[ACTORCAT_PLAYER].head = &owner->actor;
    it->second.draw(actor, play);
    play->actorCtx.actorLists[ACTORCAT_PLAYER].head = realPlayer;
    owner->rightHandType = rightHandType;
}

static void MirrorDestroy(Actor* actor, PlayState* play) {
    auto it = sMirrorFuncs.find(actor);
    if (it == sMirrorFuncs.end()) {
        return;
    }
    ActorFunc destroy = it->second.destroy;
    sMirrorFuncs.erase(it);

    if (destroy != nullptr) {
        LocalMagicState magic = SaveLocalMagic();
        destroy(actor, play);
        RestoreLocalMagic(magic);
    }
}

static Actor* SpawnMirror(Actor* parent, s16 actorId, Vec3f pos, s16 rotX, s16 rotY, s16 params, uint32_t owner,
                          uint32_t projId) {
    LocalMagicState magic = SaveLocalMagic();
    Actor* mirror;
    if (parent != nullptr) {
        mirror = Actor_SpawnAsChild(&gPlayState->actorCtx, parent, gPlayState, actorId, pos.x, pos.y, pos.z, rotX, rotY,
                                    0, params);
    } else {
        mirror = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, pos.x, pos.y, pos.z, rotX, rotY, 0, params);
    }
    RestoreLocalMagic(magic);

    if (mirror == nullptr) {
        return nullptr;
    }

    // Some magic sets its position to the (local) player when created
    mirror->world.pos = pos;
    ObjectExtension::GetInstance().Set<ProjectileNet>(mirror, ProjectileNet{ projId, owner, true });

    bool drawsAroundPlayer = actorId == ACTOR_ARMS_HOOK || actorId == ACTOR_MAGIC_DARK;
    sMirrorFuncs[mirror] = MirrorFuncs{ mirror->draw, mirror->destroy, owner };
    if (drawsAroundPlayer) {
        mirror->draw = MirrorDraw;
    }
    mirror->destroy = MirrorDestroy;
    return mirror;
}

static bool IsBreakable(s16 id) {
    return id == ACTOR_OBJ_TSUBO || id == ACTOR_OBJ_KIBAKO || id == ACTOR_OBJ_KIBAKO2 || id == ACTOR_EN_ISHI ||
           id == ACTOR_EN_KUSA;
}

static bool IsMovableObject(s16 id) {
    return IsBreakable(id) || id == ACTOR_OBJ_OSHIHIKI || id == ACTOR_BG_SPOT15_RRBOX || id == ACTOR_BG_HAKA ||
           id == ACTOR_BG_HIDAN_ROCK;
}

static bool IsInCurrentRoom(Actor* actor) {
    return actor->room == -1 || actor->room == gPlayState->roomCtx.curRoom.num;
}

// Objects placed in the scene have the same home position everywhere. Objects spawned during gameplay usually spawn
// in (nearly) the same spot, so fall back to the closest object of the same kind.
static constexpr float RUNTIME_MATCH_DISTANCE = 100.0f;

static Actor* FindPlacedActor(s16 actorId, s16 params, const Vec3f& home) {
    Actor* closest = nullptr;
    float closestDist = RUNTIME_MATCH_DISTANCE;
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->update != nullptr && actor->id == actorId && actor->params == params) {
                Vec3f actorHome = actor->home.pos;
                float dist = Math_Vec3f_DistXYZ(&actorHome, (Vec3f*)&home);
                if (dist < 2.0f) {
                    return actor;
                }
                if (dist < closestDist) {
                    closest = actor;
                    closestDist = dist;
                }
            }
            actor = actor->next;
        }
    }
    return closest;
}

static nlohmann::json HomeOf(Actor* actor) {
    return { (s32)actor->home.pos.x, (s32)actor->home.pos.y, (s32)actor->home.pos.z };
}

static Vec3f ReadVec(const nlohmann::json& j, int offset) {
    return { (f32)j.at(offset).get<s32>(), (f32)j.at(offset + 1).get<s32>(), (f32)j.at(offset + 2).get<s32>() };
}

static bool IsHeldByLocalPlayer(Actor* actor) {
    Player* player = GET_PLAYER(gPlayState);
    return player->heldActor == actor || actor->parent == &player->actor;
}

static void ShowBombExplosion(Vec3f pos) {
    static Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
    static Vec3f bomb2Accel = { 0.0f, 0.1f, 0.0f };
    Vec3f effPos = pos;
    effPos.y += 10.0f;
    EffectSsBomb2_SpawnLayered(gPlayState, &effPos, &zeroVec, &bomb2Accel, 100, 19);
    EffectSsBlast_SpawnWhiteShockwave(gPlayState, &pos, &zeroVec, &zeroVec);
    SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &pos, 40, NA_SE_IT_BOMB_EXPLOSION);
}

static void ShowBreakEffect(Actor* actor) {
    u16 sfx = NA_SE_EV_POT_BROKEN;
    if (actor->id == ACTOR_OBJ_KIBAKO || actor->id == ACTOR_OBJ_KIBAKO2) {
        sfx = NA_SE_EV_WOODBOX_BREAK;
    } else if (actor->id == ACTOR_EN_ISHI) {
        sfx = NA_SE_EV_ROCK_BROKEN;
    }
    EffectSsHahen_SpawnBurst(gPlayState, &actor->world.pos, 10.0f, 0, 15, 10, 10, -1, 10, NULL);
    SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->world.pos, 20, sfx);
}

bool Anchor::Coop_AnyTeammateInScene() {
    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client) && client.sceneNum == gPlayState->sceneNum) {
            return true;
        }
    }
    return false;
}

void Anchor::Coop_SendToTeammatesInScene(nlohmann::json& payload) {
    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client) && client.sceneNum == gPlayState->sceneNum) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void Anchor::Interactions_Reset() {
    sLastProjectileCount = 0;
    sShownExplosions.clear();
}

// MARK: - Hooks

// Mirrors and objects being moved by a teammate don't run their own logic
void Anchor::Interactions_ShouldActorUpdate(Actor* actor, bool* should) {
    if (IsMirrorable(actor->id)) {
        ProjectileNet* net = ObjectExtension::GetInstance().Get<ProjectileNet>(actor);
        if (net != nullptr && net->isMirror) {
            *should = false;
        }
    } else if (IsMovableObject(actor->id)) {
        WorldObjectNet* net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
        if (net != nullptr && net->puppetUntil > coop.frameCounter) {
            *should = false;
        }
    }
}

void Anchor::Interactions_OnActorKill(Actor* actor) {
    if (sApplyingRemote || !IsBreakable(actor->id) || !roomState.syncInteractions || !IsSaveLoaded()) {
        return;
    }

    // Actors are also killed when their room is unloaded or the scene changes, those aren't breaks
    if (!IsInCurrentRoom(actor) || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF ||
        gPlayState->roomCtx.status != 0 || actor->xyzDistToPlayerSq > SQ(1500.0f) || !Coop_AnyTeammateInScene()) {
        return;
    }

    WorldObjectNet* net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
    if (net != nullptr && net->puppetUntil > coop.frameCounter) {
        // A teammate is moving this one, they'll send the break
        return;
    }

    nlohmann::json payload;
    payload["type"] = OBJECT_BROKEN;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["actorId"] = actor->id;
    payload["params"] = actor->params;
    payload["home"] = HomeOf(actor);
    payload["cut"] = false;
    Coop_SendToTeammatesInScene(payload);
}

// MARK: - Per frame

void Anchor::Coop_UpdateInteractions() {
    if (!roomState.syncInteractions) {
        return;
    }

    // Mirrors of players that left the scene (or the room) go away
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (IsMirrorable(actor->id) && actor->update != nullptr) {
                ProjectileNet* net = ObjectExtension::GetInstance().Get<ProjectileNet>(actor);
                if (net != nullptr && net->isMirror &&
                    (!clients.contains(net->owner) || clients[net->owner].sceneNum != gPlayState->sceneNum ||
                     !clients[net->owner].online ||
                     (IsArrowElement(actor->id) && (actor->parent == nullptr || actor->parent->update == nullptr)))) {
                    Actor_Kill(actor);
                }
            }
            actor = actor->next;
        }
    }

    if (!Coop_AnyTeammateInScene()) {
        sLastProjectileCount = 0;
        return;
    }

    if (++sSendTimer < RateToInterval(roomState.playerUpdateRate)) {
        return;
    }
    sSendTimer = 0;

    nlohmann::json projectiles = nlohmann::json::array();
    nlohmann::json objects = nlohmann::json::array();

    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->update == nullptr) {
                actor = actor->next;
                continue;
            }

            if (IsProjectile(actor->id)) {
                ProjectileNet* net = ObjectExtension::GetInstance().Get<ProjectileNet>(actor);
                if (net == nullptr) {
                    ObjectExtension::GetInstance().Set<ProjectileNet>(actor, ProjectileNet{ sNextProjId++, 0, false });
                    net = ObjectExtension::GetInstance().Get<ProjectileNet>(actor);
                }
                if (!net->isMirror) {
                    bool exploding = actor->id == ACTOR_EN_BOM && actor->params == BOMB_EXPLOSION;
                    s16 element = 0;
                    if (actor->id == ACTOR_EN_ARROW && actor->child != nullptr && actor->child->update != nullptr &&
                        IsArrowElement(actor->child->id)) {
                        element = actor->child->id;
                    }
                    projectiles.push_back({ net->projId, actor->id, actor->params, (s32)actor->world.pos.x,
                                            (s32)actor->world.pos.y, (s32)actor->world.pos.z, actor->shape.rot.x,
                                            actor->shape.rot.y, exploding ? 1 : 0, element });
                }
            } else if (IsMovableObject(actor->id) && IsInCurrentRoom(actor)) {
                WorldObjectNet* net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
                if (net == nullptr) {
                    ObjectExtension::GetInstance().Set<WorldObjectNet>(actor, WorldObjectNet{});
                    net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
                }
                if (!net->initialized) {
                    net->initialized = true;
                    net->lastSent = actor->world.pos;
                } else if (net->puppetUntil <= coop.frameCounter &&
                           Math_Vec3f_DistXYZ(&actor->world.pos, &net->lastSent) > 1.0f) {
                    net->lastSent = actor->world.pos;
                    nlohmann::json entry = { actor->id, actor->params };
                    for (auto& v : HomeOf(actor)) {
                        entry.push_back(v);
                    }
                    entry.push_back((s32)actor->world.pos.x);
                    entry.push_back((s32)actor->world.pos.y);
                    entry.push_back((s32)actor->world.pos.z);
                    entry.push_back(actor->shape.rot.y);
                    entry.push_back(actor->room);
                    objects.push_back(entry);
                }

                // Grass that was just cut
                if (actor->id == ACTOR_EN_KUSA) {
                    bool isCut = ((EnKusa*)actor)->actionFunc == EnKusa_CutWaitRegrow;
                    if (isCut && !net->wasCut && !sApplyingRemote) {
                        nlohmann::json payload;
                        payload["type"] = OBJECT_BROKEN;
                        payload["sceneNum"] = gPlayState->sceneNum;
                        payload["actorId"] = actor->id;
                        payload["params"] = actor->params;
                        payload["home"] = HomeOf(actor);
                        payload["cut"] = true;
                        Coop_SendToTeammatesInScene(payload);
                    }
                    net->wasCut = isCut;
                }
            } else if (actor->id == ACTOR_OBJ_SYOKUDAI) {
                ObjSyokudai* torch = (ObjSyokudai*)actor;
                WorldObjectNet* net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
                if (net == nullptr) {
                    ObjectExtension::GetInstance().Set<WorldObjectNet>(actor, WorldObjectNet{});
                    net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
                    net->lastTorchTimer = torch->litTimer;
                }
                if (torch->litTimer != 0 && net->lastTorchTimer == 0) {
                    nlohmann::json payload;
                    payload["type"] = TORCH_LIT;
                    payload["sceneNum"] = gPlayState->sceneNum;
                    payload["params"] = actor->params;
                    payload["home"] = HomeOf(actor);
                    payload["litTimer"] = torch->litTimer;
                    Coop_SendToTeammatesInScene(payload);
                }
                net->lastTorchTimer = torch->litTimer;
            }

            actor = actor->next;
        }
    }

    if (!projectiles.empty() || sLastProjectileCount > 0) {
        nlohmann::json payload;
        payload["type"] = PROJECTILES;
        payload["sceneNum"] = gPlayState->sceneNum;
        payload["list"] = projectiles;
        payload["quiet"] = true;
        Coop_SendToTeammatesInScene(payload);
    }
    sLastProjectileCount = projectiles.size();

    if (!objects.empty()) {
        nlohmann::json payload;
        payload["type"] = WORLD_OBJECTS;
        payload["sceneNum"] = gPlayState->sceneNum;
        payload["list"] = objects;
        payload["quiet"] = true;
        Coop_SendToTeammatesInScene(payload);
    }
}

// MARK: - Incoming

void Anchor::HandlePacket_Projectiles(nlohmann::json payload) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    uint32_t owner = payload.at("clientId").get<uint32_t>();

    // This owner's existing mirrors
    std::unordered_map<uint32_t, Actor*> mirrors;
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (IsProjectile(actor->id) && actor->update != nullptr) {
                ProjectileNet* net = ObjectExtension::GetInstance().Get<ProjectileNet>(actor);
                if (net != nullptr && net->isMirror && net->owner == owner) {
                    mirrors[net->projId] = actor;
                }
            }
            actor = actor->next;
        }
    }

    std::set<uint32_t> present;
    for (auto& entry : payload.at("list")) {
        uint32_t projId = entry.at(0).get<uint32_t>();
        s16 actorId = entry.at(1).get<s16>();
        s16 params = entry.at(2).get<s16>();
        Vec3f pos = ReadVec(entry, 3);
        s16 rotX = entry.at(6).get<s16>();
        s16 rotY = entry.at(7).get<s16>();
        bool exploding = entry.at(8).get<int>() != 0;
        s16 element = entry.size() > 9 ? entry.at(9).get<s16>() : 0;
        present.insert(projId);

        auto it = mirrors.find(projId);

        if (exploding) {
            uint64_t key = ((uint64_t)owner << 32) | projId;
            if (!sShownExplosions.contains(key)) {
                sShownExplosions.insert(key);
                ShowBombExplosion(pos);
            }
            if (it != mirrors.end()) {
                Actor_Kill(it->second);
                mirrors.erase(it);
            }
            continue;
        }

        Actor* mirror = nullptr;
        if (it != mirrors.end()) {
            mirror = it->second;
            mirror->world.pos = pos;
            mirror->shape.rot.x = mirror->world.rot.x = rotX;
            mirror->shape.rot.y = mirror->world.rot.y = rotY;
        } else {
            sApplyingRemote = true;
            mirror = SpawnMirror(nullptr, actorId, pos, rotX, rotY, params, owner, projId);
            sApplyingRemote = false;
        }

        // Fire/ice/light arrow effect on the copy of the arrow
        if (mirror != nullptr && element != 0 &&
            (mirror->child == nullptr || mirror->child->update == nullptr || mirror->child->id != element)) {
            sApplyingRemote = true;
            SpawnMirror(mirror, element, pos, rotX, rotY, 0, owner, 0);
            sApplyingRemote = false;
        }
    }

    // Projectiles the owner no longer has hit something or ran out, remove the copies
    for (auto& [projId, mirror] : mirrors) {
        if (!present.contains(projId)) {
            if (mirror->child != nullptr && IsArrowElement(mirror->child->id)) {
                Actor_Kill(mirror->child);
            }
            Actor_Kill(mirror);
        }
    }
}

void Anchor::HandlePacket_WorldObjects(nlohmann::json payload) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    for (auto& entry : payload.at("list")) {
        s16 actorId = entry.at(0).get<s16>();
        s16 params = entry.at(1).get<s16>();
        Vec3f home = ReadVec(entry, 2);
        Actor* actor = FindPlacedActor(actorId, params, home);

        // A teammate has an object (spawned during gameplay) that we don't, spawn it. Only in the room we're both in,
        // objects of other rooms simply aren't loaded here.
        s8 room = entry.size() > 9 ? entry.at(9).get<s8>() : -2;
        if (actor == nullptr && IsBreakable(actorId) && (room == -1 || room == gPlayState->roomCtx.curRoom.num)) {
            sApplyingRemote = true;
            actor = Actor_Spawn(&gPlayState->actorCtx, gPlayState, actorId, home.x, home.y, home.z, 0,
                                entry.at(8).get<s16>(), 0, params);
            sApplyingRemote = false;
        }

        if (actor == nullptr || IsHeldByLocalPlayer(actor)) {
            continue;
        }

        actor->world.pos = ReadVec(entry, 5);
        actor->shape.rot.y = actor->world.rot.y = entry.at(8).get<s16>();

        WorldObjectNet* net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
        if (net == nullptr) {
            ObjectExtension::GetInstance().Set<WorldObjectNet>(actor, WorldObjectNet{});
            net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
        }
        net->initialized = true;
        net->lastSent = actor->world.pos;
        // Hand control back to the actor itself shortly after the teammate stops moving it
        net->puppetUntil = coop.frameCounter + 10;
    }
}

void Anchor::HandlePacket_ObjectBroken(nlohmann::json payload) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    Vec3f home = { (f32)payload.at("home").at(0).get<s32>(), (f32)payload.at("home").at(1).get<s32>(),
                   (f32)payload.at("home").at(2).get<s32>() };
    Actor* actor = FindPlacedActor(payload.at("actorId").get<s16>(), payload.at("params").get<s16>(), home);
    if (actor == nullptr || IsHeldByLocalPlayer(actor)) {
        return;
    }

    sApplyingRemote = true;
    if (payload.value("cut", false) && actor->id == ACTOR_EN_KUSA) {
        EnKusa* grass = (EnKusa*)actor;
        if (grass->actionFunc != EnKusa_CutWaitRegrow) {
            EnKusa_SpawnFragments(grass, gPlayState);
            EnKusa_SetupCut(grass);
            WorldObjectNet* net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
            if (net != nullptr) {
                net->wasCut = true;
            }
        }
    } else {
        ShowBreakEffect(actor);
        Actor_Kill(actor);
    }
    sApplyingRemote = false;
}

void Anchor::HandlePacket_TorchLit(nlohmann::json payload) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }

    Vec3f home = { (f32)payload.at("home").at(0).get<s32>(), (f32)payload.at("home").at(1).get<s32>(),
                   (f32)payload.at("home").at(2).get<s32>() };
    Actor* actor = FindPlacedActor(ACTOR_OBJ_SYOKUDAI, payload.at("params").get<s16>(), home);
    if (actor == nullptr) {
        return;
    }

    ObjSyokudai* torch = (ObjSyokudai*)actor;
    if (torch->litTimer == 0) {
        torch->litTimer = payload.at("litTimer").get<s16>();
        SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &actor->world.pos, 20, NA_SE_EV_FLAME_IGNITION);
        WorldObjectNet* net = ObjectExtension::GetInstance().Get<WorldObjectNet>(actor);
        if (net != nullptr) {
            net->lastTorchTimer = torch->litTimer;
        }
    }
}
