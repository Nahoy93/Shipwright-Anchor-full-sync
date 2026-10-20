#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Notification/Notification.h"
#include "soh/ResourceManagerHelpers.h"

extern "C" {
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * CUTSCENE_START
 *
 * Sent to the team when a cutscene starts on this client. There are two kinds of cutscenes:
 *
 *  - "entrance": the scene was loaded straight into a cutscene layer (cutsceneIndex 0xFFF0-0xFFFC), for example after
 *                taking a blue warp. Teammates replay it by doing the same transition with the same cutscene index.
 *  - "script":   an actor or an entrance trigger started a scripted cutscene. csCtx.segment then holds the
 *                "__OTR__..." resource path of the cutscene script, which is the same on every client. Teammates in
 *                the same scene start the same script, teammates elsewhere are first teleported next to the player.
 *
 * Teammates that are busy (talking, in another cutscene, down, in a menu...) play it as soon as they're free, for a
 * few seconds. Players of a different age don't get the cutscene, since the scene layouts can differ.
 */

static constexpr uint32_t RECENT_CUTSCENE_FRAMES = 20 * 30;  // 30 seconds
static constexpr uint32_t PENDING_CUTSCENE_FRAMES = 20 * 60; // give up after a minute of being busy

static bool IsRecentCutscene(std::map<std::string, uint32_t>& recent, const std::string& key, uint32_t now) {
    auto it = recent.find(key);
    return it != recent.end() && now - it->second < RECENT_CUTSCENE_FRAMES;
}

static std::string CutsceneKey(const nlohmann::json& payload) {
    if (payload.at("kind").get<std::string>() == "entrance") {
        return "e:" + std::to_string(payload.at("entranceIndex").get<s32>()) + ":" +
               std::to_string(payload.at("cutsceneIndex").get<u16>());
    }
    return "s:" + payload.at("path").get<std::string>();
}

// Same list as CanTeleportTo, the respawn based teleport doesn't work well in these scenes
static bool IsProblematicTeleportScene(s16 sceneNum) {
    return sceneNum == SCENE_ID_MAX || sceneNum == SCENE_GROTTOS || sceneNum == SCENE_MARKET_DAY ||
           sceneNum == SCENE_MARKET_NIGHT || sceneNum == SCENE_MARKET_RUINS || sceneNum == SCENE_MARKET_ENTRANCE_DAY ||
           sceneNum == SCENE_MARKET_ENTRANCE_NIGHT || sceneNum == SCENE_MARKET_ENTRANCE_RUINS ||
           sceneNum == SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY || sceneNum == SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT ||
           sceneNum == SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS || sceneNum == SCENE_BACK_ALLEY_DAY ||
           sceneNum == SCENE_BACK_ALLEY_NIGHT;
}

void Anchor::SendPacket_CutsceneStart() {
    if (!IsSaveLoaded() || !roomState.syncCutscenes || isProcessingIncomingPacket) {
        return;
    }

    // The opening cutscene of a new file is not shared (the credits are: teammates are brought along)
    if (coop.framesSinceLoad < 40) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);
    u16 cutsceneIndex = (u16)gSaveContext.cutsceneIndex;

    nlohmann::json payload;
    payload["type"] = CUTSCENE_START;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomIndex"] = gPlayState->roomCtx.curRoom.num;
    payload["entranceIndex"] = gSaveContext.entranceIndex;
    payload["linkAge"] = gSaveContext.linkAge;
    payload["posRot"] = player->actor.world;

    if (cutsceneIndex >= 0xFFF0 && cutsceneIndex <= 0xFFFC) {
        payload["kind"] = "entrance";
        payload["cutsceneIndex"] = cutsceneIndex;
    } else {
        char* segment = (char*)gPlayState->csCtx.segment;
        // Cutscenes built in code (warp songs, some camera sequences) don't have a resource path, skip those
        if (segment == nullptr || !ResourceMgr_OTRSigCheck(segment)) {
            return;
        }
        payload["kind"] = "script";
        payload["path"] = std::string(segment);
    }

    std::string key = CutsceneKey(payload);
    if (IsRecentCutscene(coop.recentCutscenes, key, coop.frameCounter)) {
        return;
    }
    coop.recentCutscenes[key] = coop.frameCounter;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_CutsceneStart(nlohmann::json payload) {
    if (!roomState.syncCutscenes || !IsSaveLoaded()) {
        return;
    }

    std::string key = CutsceneKey(payload);
    if (IsRecentCutscene(coop.recentCutscenes, key, coop.frameCounter)) {
        return;
    }

    coop.pendingCutscene = payload;
    coop.hasPendingCutscene = true;
    coop.pendingTeleportDone = false;
    coop.pendingCutsceneExpires = coop.frameCounter + PENDING_CUTSCENE_FRAMES;
}

void Anchor::Coop_ProcessPendingCutscene() {
    if (!coop.hasPendingCutscene) {
        return;
    }

    if (!roomState.syncCutscenes || coop.frameCounter > coop.pendingCutsceneExpires) {
        coop.hasPendingCutscene = false;
        return;
    }

    nlohmann::json& payload = coop.pendingCutscene;
    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    std::string name = clients.contains(clientId) ? clients[clientId].name : "A teammate";
    std::string kind = payload.at("kind").get<std::string>();
    s16 sceneNum = payload.at("sceneNum").get<s16>();
    bool sameScene = sceneNum == gPlayState->sceneNum;

    if (payload.at("linkAge").get<s32>() != gSaveContext.linkAge) {
        coop.hasPendingCutscene = false;
        Notification::Emit({
            .prefix = name,
            .message = "is watching a cutscene (you are a different age, so it was skipped)",
        });
        return;
    }

    if (!sameScene && roomState.syncCutscenes != 2) {
        coop.hasPendingCutscene = false;
        return;
    }

    if (!Coop_CanStartRemoteCutscene()) {
        // Busy right now, try again next frame
        return;
    }

    if (kind == "entrance") {
        coop.hasPendingCutscene = false;
        coop.recentCutscenes[CutsceneKey(payload)] = coop.frameCounter;

        gPlayState->nextEntranceIndex = payload.at("entranceIndex").get<s16>();
        gSaveContext.nextCutsceneIndex = payload.at("cutsceneIndex").get<u16>();
        gPlayState->transitionTrigger = TRANS_TRIGGER_START;
        gPlayState->transitionType = TRANS_TYPE_FADE_WHITE;
        coop.isPlayingRemoteCutscene = true;
        coop.remoteCutsceneGrace = 0;

        Notification::Emit({ .prefix = name, .message = "started a cutscene" });
        return;
    }

    // Scripted cutscene
    if (sameScene) {
        // Give the scene a moment to settle after a teleport
        if (coop.framesInScene < 20) {
            return;
        }

        coop.hasPendingCutscene = false;
        coop.recentCutscenes[CutsceneKey(payload)] = coop.frameCounter;
        Coop_StartScriptCutscene(payload.at("path").get<std::string>());

        if (!coop.pendingTeleportDone) {
            Notification::Emit({ .prefix = name, .message = "started a cutscene" });
        }
        return;
    }

    if (coop.pendingTeleportDone) {
        // Already on our way there
        return;
    }

    s32 entranceIndex = payload.at("entranceIndex").get<s32>();
    s8 roomIndex = payload.at("roomIndex").get<s8>();
    if (IsProblematicTeleportScene(sceneNum) || entranceIndex < 0 || roomIndex < 0) {
        coop.hasPendingCutscene = false;
        Notification::Emit({ .prefix = name, .message = "started a cutscene, but you can't be brought there" });
        return;
    }

    // Same teleport as TELEPORT_TO: respawn at the teammate's position in their scene
    PosRot posRot = payload.at("posRot").get<PosRot>();
    gPlayState->nextEntranceIndex = (s16)entranceIndex;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_WHITE;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex = (s16)entranceIndex;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].roomIndex = roomIndex;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].pos = posRot.pos;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].yaw = posRot.rot.y;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].playerParams = 0xDFF;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_WHITE_FAST;
    gSaveContext.respawnFlag = 1;
    // No void damage on arrival (the respawn point may be over a pit). Only one of these may be registered at a time:
    // a second teleport before the first void-out must replace it, or the old one would stay registered for good.
    static HOOK_ID hookId = 0;
    if (hookId != 0) {
        GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(hookId);
    }
    hookId = REGISTER_VB_SHOULD(VB_INFLICT_VOID_DAMAGE, {
        *should = false;
        GameInteractor::Instance->UnregisterGameHookForID<GameInteractor::OnVanillaBehavior>(hookId);
        hookId = 0;
    });

    coop.pendingTeleportDone = true;
    coop.pendingCutsceneExpires = coop.frameCounter + PENDING_CUTSCENE_FRAMES;

    Notification::Emit({ .prefix = name, .message = "started a cutscene, bringing you there" });
}
