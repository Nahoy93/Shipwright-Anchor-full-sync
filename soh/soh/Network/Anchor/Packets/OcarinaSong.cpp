#include <nlohmann/json.hpp>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include "soh/Notification/Notification.h"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "src/overlays/actors/ovl_Obj_Timeblock/z_obj_timeblock.h"
extern PlayState* gPlayState;
s32 ObjTimeblock_WaitForOcarina(ObjTimeblock* this_, PlayState* play);
}

/**
 * OCARINA_SONG / WARP_SONG
 *
 * The notes themselves are already heard through OCARINA_SFX. These packets share what the song *does*:
 *
 *  - Sun's Song:       the time skip happens for every teammate (wherever they are, so the time of day stays aligned)
 *  - Song of Storms:   it starts raining for teammates in the same area
 *  - Song of Time:     Song of Time blocks near the player who played it move for teammates in the same area
 *  - Warp songs:       the whole team warps along (room setting)
 *  - Everything else:  teammates get a notification. What those songs unlock (doors, the windmill, Epona...) is saved
 *                      as flags, which are already synced.
 */

static const char* sSongNames[] = {
    "Minuet of Forest", "Bolero of Fire", "Serenade of Water", "Requiem of Spirit", "Nocturne of Shadow",
    "Prelude of Light", "Saria's Song",   "Epona's Song",      "Zelda's Lullaby",   "Sun's Song",
    "Song of Time",     "Song of Storms", "Scarecrow's Song",
};

static s16 sWarpSongEntrances[] = {
    ENTR_SACRED_FOREST_MEADOW_WARP_PAD,
    ENTR_DEATH_MOUNTAIN_CRATER_WARP_PAD,
    ENTR_LAKE_HYLIA_WARP_PAD,
    ENTR_DESERT_COLOSSUS_WARP_PAD,
    ENTR_GRAVEYARD_WARP_PAD,
    ENTR_TEMPLE_OF_TIME_WARP_PAD,
};

static u8 sLastMsgMode = 0;
static bool sReplayingWarp = false;
static bool sHasPendingWarp = false;
static uint8_t sPendingWarpSong = 0;
static uint32_t sPendingWarpClient = 0;
static uint32_t sPendingWarpExpires = 0;

static std::string SongName(uint8_t song) {
    return song < ARRAY_COUNT(sSongNames) ? sSongNames[song] : "a song";
}

// MARK: - Song played

void Anchor::Coop_UpdateSongWatch() {
    u8 msgMode = gPlayState->msgCtx.msgMode;
    if (msgMode != sLastMsgMode && msgMode == MSGMODE_SETUP_DISPLAY_SONG_PLAYED) {
        uint8_t song = gPlayState->msgCtx.lastPlayedSong;
        // Warp songs are sent once the warp actually happens (the player can still say no)
        if (song > OCARINA_SONG_PRELUDE) {
            SendPacket_OcarinaSong(song);
        }
    }
    sLastMsgMode = msgMode;
}

void Anchor::SendPacket_OcarinaSong(uint8_t song) {
    if (!IsSaveLoaded() || !roomState.syncSongs) {
        return;
    }

    Player* player = GET_PLAYER(gPlayState);

    nlohmann::json payload;
    payload["type"] = OCARINA_SONG;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["song"] = song;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["pos"] = player->actor.world.pos;

    SendJsonToRemote(payload);
}

// Makes a Song of Time block react as if the song was played right next to it
static s32 TimeblockSongPlayedRemotely(ObjTimeblock* timeblock, PlayState* play) {
    timeblock->songObserverFunc = ObjTimeblock_WaitForOcarina;
    return true;
}

static void PlaySongOfTimeAt(Vec3f* pos) {
    static const f32 ranges[] = { 60.0f, 100.0f, 140.0f, 180.0f, 220.0f, 260.0f, 300.0f, 300.0f };

    for (int category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = gPlayState->actorCtx.actorLists[category].head;
        while (actor != nullptr) {
            if (actor->id == ACTOR_OBJ_TIMEBLOCK && actor->update != nullptr) {
                ObjTimeblock* timeblock = (ObjTimeblock*)actor;
                // Blocks driven by a switch flag already follow the flag sync, toggling them here would undo it
                if (timeblock->unk_177 == 0 && timeblock->demoEffectTimer <= 0 &&
                    Math_Vec3f_DistXZ(&actor->world.pos, pos) <= ranges[(actor->params >> 11) & 7]) {
                    timeblock->songObserverFunc = TimeblockSongPlayedRemotely;
                }
            }
            actor = actor->next;
        }
    }
}

void Anchor::HandlePacket_OcarinaSong(nlohmann::json payload) {
    if (!roomState.syncSongs || !IsSaveLoaded()) {
        return;
    }

    uint32_t clientId = payload.value("clientId", (uint32_t)0);
    std::string name = clients.contains(clientId) ? clients[clientId].name : "A teammate";
    uint8_t song = payload.at("song").get<uint8_t>();
    bool sameScene = payload.at("sceneNum").get<s16>() == gPlayState->sceneNum;
    Vec3f pos = payload.at("pos").get<Vec3f>();

    Notification::Emit({ .prefix = name, .message = "played", .suffix = SongName(song) });

    if (coop.isDown) {
        return;
    }

    switch (song) {
        case OCARINA_SONG_SUNS:
            // Applies wherever the teammate is, even where they couldn't play the song themselves
            if (gSaveContext.sunsSongState == SUNSSONG_INACTIVE && gPlayState->transitionTrigger == TRANS_TRIGGER_OFF &&
                gPlayState->csCtx.state == CS_STATE_IDLE) {
                gSaveContext.sunsSongState = SUNSSONG_START;
            }
            break;
        case OCARINA_SONG_STORMS:
            if (sameScene) {
                Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_EN_OKARINA_EFFECT, pos.x, pos.y - 30.0f, pos.z, 0,
                            0, 0, 1);
            }
            break;
        case OCARINA_SONG_TIME:
            if (sameScene) {
                PlaySongOfTimeAt(&pos);
            }
            break;
        default:
            break;
    }
}

// MARK: - Warp songs

void Anchor::SendPacket_WarpSong() {
    if (!IsSaveLoaded() || !roomState.syncWarpSongs || sReplayingWarp) {
        return;
    }

    uint8_t song = gSaveContext.respawn[RESPAWN_MODE_RETURN].data;
    if (song > OCARINA_SONG_PRELUDE) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = WARP_SONG;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["song"] = song;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_WarpSong(nlohmann::json payload) {
    if (!roomState.syncWarpSongs || !IsSaveLoaded()) {
        return;
    }

    uint8_t song = payload.at("song").get<uint8_t>();
    if (song > OCARINA_SONG_PRELUDE) {
        return;
    }

    sHasPendingWarp = true;
    sPendingWarpSong = song;
    sPendingWarpClient = payload.value("clientId", (uint32_t)0);
    sPendingWarpExpires = coop.frameCounter + 20 * 60;
}

void Anchor::Coop_ProcessPendingWarp() {
    if (!sHasPendingWarp) {
        return;
    }

    if (!roomState.syncWarpSongs || coop.frameCounter > sPendingWarpExpires) {
        sHasPendingWarp = false;
        return;
    }

    // Wait until we're free (not talking, in a cutscene, down, ...)
    if (!Coop_CanStartRemoteCutscene()) {
        return;
    }

    sHasPendingWarp = false;
    std::string name = clients.contains(sPendingWarpClient) ? clients[sPendingWarpClient].name : "A teammate";
    Notification::Emit({ .prefix = name, .message = "warped the team with", .suffix = SongName(sPendingWarpSong) });

    // Same setup as the player's own warp (Player_Action_8084E3C4), then leave like DemoKankyo does
    gSaveContext.respawn[RESPAWN_MODE_RETURN].entranceIndex = sWarpSongEntrances[sPendingWarpSong];
    gSaveContext.respawn[RESPAWN_MODE_RETURN].playerParams = 0x5FF;
    gSaveContext.respawn[RESPAWN_MODE_RETURN].data = sPendingWarpSong;
    gSaveContext.seqId = (u8)NA_BGM_DISABLED;
    gSaveContext.natureAmbienceId = NATURE_ID_DISABLED;

    sReplayingWarp = true;
    Environment_WarpSongLeave(gPlayState);
    sReplayingWarp = false;
}
