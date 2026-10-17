#include <nlohmann/json.hpp>

#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "src/overlays/effects/ovl_Effect_Ss_Fhg_Flash/z_eff_ss_fhg_flash.h"
#include "seqcmd.h"
extern PlayState* gPlayState;
}

/**
 * Player effects and TIME_SYNC
 *
 *  - Teammates' figures show their mask, flames when they're on fire, sparks when they're shocked and the flame of a
 *    burning Deku Stick (the figure doesn't run the player's own update, which is where the game spawns those).
 *  - Lens of Truth: when a teammate in the same room has the lens active, hidden things show for you as well (without
 *    using your magic). Your own lens works as usual.
 *  - Time of day: the teammate with the lowest client id (usually whoever joined first) keeps the time, everyone else
 *    follows when they drift too far apart.
 *  - Farore's Wind: the warp point is shared by the team (per age, like the game keeps it), so anyone can warp to the
 *    point a teammate set, and using it uses it up for everyone.
 *  - Music: in each area the teammate with the lowest id picks the music (boss music, minigames, silence...) and the
 *    others play the same.
 */

// MARK: - Teammate figure

void Anchor_DummyPlayerEffects(Player* player, AnchorClient& client) {
    if (!Anchor::Instance->roomState.syncPlayerEffects) {
        player->currentMask = PLAYER_MASK_NONE;
        return;
    }

    player->currentMask = client.currentMask;

    // Burning Deku Stick (same effect as Player_UpdateBurningDekuStick)
    if (client.heldItemAction == PLAYER_IA_DEKU_STICK && client.stickFlame > 0) {
        static Vec3f velocity = { 0.0f, 0.5f, 0.0f };
        static Vec3f accel = { 0.0f, 0.5f, 0.0f };
        static Color_RGBA8 primColor = { 255, 255, 100, 255 };
        static Color_RGBA8 envColor = { 255, 50, 0, 0 };
        f32 scale = client.stickFlame < 20 ? client.stickFlame / 20.0f : 1.0f;
        func_8002836C(gPlayState, &player->meleeWeaponInfo[0].tip, &velocity, &accel, &primColor, &envColor,
                      scale * 200.0f, 0, 8);
    }

    // On fire: flames on a couple of random body parts, positioned relative to the figure so they follow it
    if (client.isBurning) {
        for (int n = 0; n < 2; n++) {
            s32 part = (s32)Rand_ZeroFloat((f32)PLAYER_BODYPART_MAX - 0.1f);
            Vec3f offset;
            Math_Vec3f_Diff(&player->bodyPartsPos[part], &player->actor.world.pos, &offset);
            EffectSsFireTail_SpawnFlame(gPlayState, &player->actor, &offset, 0.2f, -1, 1.0f);
        }
        Player_PlaySfx(&player->actor, NA_SE_EV_TORCH - SFX_FLAG);
    }

    // Shocked: sparks around the body
    if (client.isShocked) {
        s32 part = (s32)Rand_ZeroFloat((f32)PLAYER_BODYPART_MAX - 0.1f);
        Vec3f pos = player->bodyPartsPos[part];
        pos.x += Rand_CenteredFloat(10.0f);
        pos.y += Rand_CenteredFloat(15.0f);
        pos.z += Rand_CenteredFloat(10.0f);
        EffectSsFhgFlash_SpawnShock(gPlayState, &player->actor, &pos, 40, FHGFLASH_SHOCK_NO_ACTOR);
    }
}

// MARK: - Lens of Truth

static bool sLensForcedByTeammate = false;

void Anchor::Coop_UpdateLens() {
    bool ownLens = gSaveContext.magicState == MAGIC_STATE_CONSUME_LENS;
    bool teammateLens = false;

    if (roomState.syncPlayerEffects) {
        for (auto& [clientId, client] : clients) {
            if (Coop_IsTeammate(client) && client.lensActive && client.sceneNum == gPlayState->sceneNum &&
                client.curRoomNum == gPlayState->roomCtx.curRoom.num) {
                teammateLens = true;
                break;
            }
        }
    }

    if (ownLens) {
        // The player's own lens takes over (and turns it off again itself)
        sLensForcedByTeammate = false;
        return;
    }

    if (teammateLens && !gPlayState->actorCtx.lensActive) {
        gPlayState->actorCtx.lensActive = true;
        sLensForcedByTeammate = true;
    } else if (!teammateLens && sLensForcedByTeammate) {
        gPlayState->actorCtx.lensActive = false;
        sLensForcedByTeammate = false;
    }
}

// MARK: - Time of day

static constexpr u16 TIME_SYNC_TOLERANCE = 0x200; // about 11 in-game minutes
static uint32_t sTimeSyncTimer = 0;

void Anchor::Coop_UpdateTimeSync() {
    // Room setting: how often the time keeper sends the time (in seconds)
    if (!roomState.syncTime || ++sTimeSyncTimer < 20u * MAX(1, roomState.timeSyncSeconds)) {
        return;
    }
    sTimeSyncTimer = 0;

    // Only the time keeper sends: the teammate with the lowest client id
    bool anyTeammate = false;
    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client)) {
            anyTeammate = true;
            if (clientId < ownClientId) {
                return;
            }
        }
    }
    if (!anyTeammate || ownClientId == 0) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = TIME_SYNC;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["dayTime"] = gSaveContext.dayTime;
    payload["totalDays"] = gSaveContext.totalDays;
    payload["quiet"] = true;
    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_TimeSync(nlohmann::json payload) {
    if (!roomState.syncTime || !IsSaveLoaded()) {
        return;
    }

    // Leave the time alone while it is being changed on purpose or the scene is switching
    if (gSaveContext.sunsSongState != SUNSSONG_INACTIVE || gPlayState->transitionTrigger != TRANS_TRIGGER_OFF ||
        gPlayState->csCtx.state != CS_STATE_IDLE) {
        return;
    }

    u16 dayTime = payload.at("dayTime").get<u16>();
    s16 diff = (s16)(dayTime - gSaveContext.dayTime);
    if (ABS(diff) > TIME_SYNC_TOLERANCE) {
        gSaveContext.dayTime = dayTime;
        gSaveContext.skyboxTime = dayTime;
    }
    gSaveContext.totalDays = payload.value("totalDays", gSaveContext.totalDays);
}

// MARK: - Farore's Wind

static FaroresWindData sLastFw = {};
static bool sFwInitialized = false;

static bool SameFw(const FaroresWindData& a, const FaroresWindData& b) {
    return a.set == b.set && a.entranceIndex == b.entranceIndex && a.roomIndex == b.roomIndex && a.pos.x == b.pos.x &&
           a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.yaw == b.yaw;
}

void Anchor::Coop_UpdateFaroresWind() {
    if (!roomState.syncPlayerEffects) {
        sFwInitialized = false;
        return;
    }
    if (!sFwInitialized || coop.framesSinceLoad < 2) {
        sLastFw = gSaveContext.fw;
        sFwInitialized = true;
        return;
    }
    if (SameFw(sLastFw, gSaveContext.fw)) {
        return;
    }
    sLastFw = gSaveContext.fw;

    const FaroresWindData& fw = gSaveContext.fw;
    nlohmann::json payload;
    payload["type"] = FARORES_WIND;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["linkAge"] = gSaveContext.linkAge;
    payload["fw"] = { fw.pos.x,         fw.pos.y,     fw.pos.z, fw.yaw,           fw.playerParams,
                      fw.entranceIndex, fw.roomIndex, fw.set,   fw.tempSwchFlags, fw.tempCollectFlags };
    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_FaroresWind(nlohmann::json payload) {
    if (!roomState.syncPlayerEffects || !IsSaveLoaded()) {
        return;
    }
    // Each age has its own warp point
    if (payload.at("linkAge").get<s32>() != gSaveContext.linkAge) {
        return;
    }

    auto& v = payload.at("fw");
    FaroresWindData& fw = gSaveContext.fw;
    fw.pos.x = v.at(0).get<s32>();
    fw.pos.y = v.at(1).get<s32>();
    fw.pos.z = v.at(2).get<s32>();
    fw.yaw = v.at(3).get<s32>();
    fw.playerParams = v.at(4).get<s32>();
    fw.entranceIndex = v.at(5).get<s32>();
    fw.roomIndex = v.at(6).get<s32>();
    fw.set = v.at(7).get<s32>();
    fw.tempSwchFlags = v.at(8).get<s32>();
    fw.tempCollectFlags = v.at(9).get<s32>();
    // Don't send it back
    sLastFw = fw;
}

// MARK: - Music

static u16 sLastSentSeq = 0;
static uint32_t sMusicTimer = 0;
static size_t sLastTeammatesInScene = 0;

void Anchor::Coop_UpdateMusic() {
    if (!roomState.syncMusic || ++sMusicTimer < 10) {
        return;
    }
    sMusicTimer = 0;

    if (!Coop_AnyTeammateInScene() || !Coop_IsLowestIdInScene()) {
        sLastTeammatesInScene = 0;
        return;
    }

    size_t teammates = 0;
    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client) && client.sceneNum == gPlayState->sceneNum) {
            teammates++;
        }
    }

    // Send when the music changes, and when someone new arrives in the area
    u16 seqId = func_800FA0B4(SEQ_PLAYER_BGM_MAIN);
    if (seqId == sLastSentSeq && teammates <= sLastTeammatesInScene) {
        sLastTeammatesInScene = teammates;
        return;
    }
    sLastSentSeq = seqId;
    sLastTeammatesInScene = teammates;

    nlohmann::json payload;
    payload["type"] = MUSIC;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["seqId"] = seqId;
    Coop_SendToTeammatesInScene(payload);
}

void Anchor::HandlePacket_Music(nlohmann::json payload) {
    if (!roomState.syncMusic || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum) {
        return;
    }
    // Cutscenes, game over and the ocarina handle their own music
    if (gPlayState->csCtx.state != CS_STATE_IDLE || gPlayState->gameOverCtx.state != GAMEOVER_INACTIVE ||
        gPlayState->msgCtx.ocarinaMode != OCARINA_MODE_00) {
        return;
    }

    u16 seqId = payload.at("seqId").get<u16>();
    if (func_800FA0B4(SEQ_PLAYER_BGM_MAIN) == seqId) {
        return;
    }
    if (seqId == NA_BGM_DISABLED || seqId == NA_BGM_GENERAL_SFX) {
        SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 10);
    } else {
        SEQCMD_PLAY_SEQUENCE(SEQ_PLAYER_BGM_MAIN, 0, 0, seqId);
    }
}
