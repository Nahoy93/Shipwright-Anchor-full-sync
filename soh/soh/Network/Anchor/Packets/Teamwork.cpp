#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"
#include "soh/OTRGlobals.h"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "variables.h"
extern PlayState* gPlayState;
}

/**
 * SONG_LEARNED / BOSS_ROOM / AGE_CHANGED and the spectator camera
 *
 *  - Learning songs: when a teammate learns a song, everyone else gets it too, along with the teacher's demonstration
 *    (the notes on screen and the melody), as soon as they are free.
 *  - Boss intros: when a teammate walks into a boss room, everyone in the same dungeon is brought to the boss room
 *    entrance, so the whole team sees the intro and fights together.
 *  - Shared age: when a teammate becomes a child or an adult in the Temple of Time, everyone else changes age with
 *    them and is brought to the Temple of Time.
 *  - Spectator camera: while you are down, the camera follows a teammate in the same area (L switches teammate).
 */

static constexpr uint32_t PENDING_FRAMES = 20 * 60;

static std::string NameOf(Anchor* anchor, uint32_t clientId) {
    auto it = anchor->clients.find(clientId);
    return it != anchor->clients.end() ? it->second.name : "A teammate";
}

// MARK: - Learning songs

static const u16 sTeachActions[] = {
    OCARINA_ACTION_TEACH_MINUET,  OCARINA_ACTION_TEACH_BOLERO,   OCARINA_ACTION_TEACH_SERENADE,
    OCARINA_ACTION_TEACH_REQUIEM, OCARINA_ACTION_TEACH_NOCTURNE, OCARINA_ACTION_TEACH_PRELUDE,
    OCARINA_ACTION_TEACH_LULLABY, OCARINA_ACTION_TEACH_EPONA,    OCARINA_ACTION_TEACH_SARIA,
    OCARINA_ACTION_TEACH_SUNS,    OCARINA_ACTION_TEACH_TIME,     OCARINA_ACTION_TEACH_STORMS,
};
static const char* sSongNames[] = {
    "Minuet of Forest",   "Bolero of Fire",   "Serenade of Water", "Requiem of Spirit",
    "Nocturne of Shadow", "Prelude of Light", "Zelda's Lullaby",   "Epona's Song",
    "Saria's Song",       "Sun's Song",       "Song of Time",      "Song of Storms",
};
static constexpr s32 SONG_COUNT = 12;

static u32 SongBits() {
    return (gSaveContext.inventory.questItems >> QUEST_SONG_MINUET) & ((1 << SONG_COUNT) - 1);
}

static u32 sKnownSongs = 0;
static bool sSongsInitialized = false;

struct PendingLesson {
    s32 song;
    uint32_t from;
    uint32_t expires;
};
static std::vector<PendingLesson> sPendingLessons;

enum class LessonState { None, Demo };
static LessonState sLessonState = LessonState::None;
static uint32_t sLessonTimeout = 0;
static PendingLesson sCurrentLesson;

void Anchor::Coop_UpdateSongLearning() {
    if (!sSongsInitialized || coop.framesSinceLoad < 2) {
        sKnownSongs = SongBits();
        sSongsInitialized = true;
        sPendingLessons.clear();
        sLessonState = LessonState::None;
        return;
    }

    // Songs we learned ourselves
    u32 bits = SongBits();
    u32 learned = bits & ~sKnownSongs;
    sKnownSongs = bits;
    if (learned != 0 && roomState.syncSongTeaching) {
        for (s32 song = 0; song < SONG_COUNT; song++) {
            if (learned & (1 << song)) {
                nlohmann::json payload;
                payload["type"] = SONG_LEARNED;
                payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
                payload["addToQueue"] = true;
                payload["song"] = song;
                SendJsonToRemote(payload);
            }
        }
    }

    Player* player = GET_PLAYER(gPlayState);

    // A demonstration that's playing
    if (sLessonState == LessonState::Demo) {
        bool done = Message_GetState(&gPlayState->msgCtx) == TEXT_STATE_SONG_DEMO_DONE;
        if (done || coop.frameCounter > sLessonTimeout) {
            Message_CloseTextbox(gPlayState);
            Player_SetCsActionWithHaltedActors(gPlayState, NULL, PLAYER_CSACTION_7);
            sLessonState = LessonState::None;
            Notification::Emit({ .prefix = NameOf(this, sCurrentLesson.from),
                                 .message = std::string("taught you ") + sSongNames[sCurrentLesson.song] });
        }
        return;
    }

    if (sPendingLessons.empty()) {
        return;
    }
    if (coop.frameCounter > sPendingLessons.front().expires) {
        sPendingLessons.erase(sPendingLessons.begin());
        return;
    }
    // Wait until we're standing somewhere calm
    if (!Coop_CanStartRemoteCutscene() || !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) ||
        (player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_WATER | PLAYER_STATE1_CARRYING_ACTOR |
                                PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_HANGING_OFF_LEDGE)) ||
        gPlayState->msgCtx.ocarinaMode != OCARINA_MODE_00 || coop.framesInScene < 40) {
        return;
    }

    sCurrentLesson = sPendingLessons.front();
    sPendingLessons.erase(sPendingLessons.begin());
    Player_SetCsActionWithHaltedActors(gPlayState, NULL, PLAYER_CSACTION_1);
    func_8010BD58(gPlayState, sTeachActions[sCurrentLesson.song]);
    sLessonState = LessonState::Demo;
    sLessonTimeout = coop.frameCounter + 20 * 30;
}

void Anchor::HandlePacket_SongLearned(nlohmann::json payload) {
    if (!roomState.syncSongTeaching || !IsSaveLoaded()) {
        return;
    }

    s32 song = payload.at("song").get<s32>();
    if (song < 0 || song >= SONG_COUNT || (SongBits() & (1 << song))) {
        return;
    }

    Item_Give(gPlayState, (u8)(ITEM_SONG_MINUET + song));
    sKnownSongs |= 1 << song; // don't send it back
    sPendingLessons.push_back({ song, payload.value("clientId", (uint32_t)0), coop.frameCounter + PENDING_FRAMES });
}

// MARK: - Boss rooms

struct BossRoom {
    s16 dungeonScene;
    s16 bossScene;
};
static const BossRoom sBossRooms[] = {
    { SCENE_DEKU_TREE, SCENE_DEKU_TREE_BOSS },         { SCENE_DODONGOS_CAVERN, SCENE_DODONGOS_CAVERN_BOSS },
    { SCENE_JABU_JABU, SCENE_JABU_JABU_BOSS },         { SCENE_FOREST_TEMPLE, SCENE_FOREST_TEMPLE_BOSS },
    { SCENE_FIRE_TEMPLE, SCENE_FIRE_TEMPLE_BOSS },     { SCENE_WATER_TEMPLE, SCENE_WATER_TEMPLE_BOSS },
    { SCENE_SPIRIT_TEMPLE, SCENE_SPIRIT_TEMPLE_BOSS }, { SCENE_SHADOW_TEMPLE, SCENE_SHADOW_TEMPLE_BOSS },
};

static s16 DungeonOfBossScene(s16 bossScene) {
    for (auto& room : sBossRooms) {
        if (room.bossScene == bossScene) {
            return room.dungeonScene;
        }
    }
    return -1;
}

static bool sHasPendingBossRoom = false;
static nlohmann::json sPendingBossRoom;
static uint32_t sPendingBossRoomExpires = 0;

void Anchor::Coop_UpdateBossRoom() {
    if (!roomState.syncBossIntros) {
        sHasPendingBossRoom = false;
        return;
    }

    // We walked into a boss room where the boss is still alive
    if (coop.framesInScene == 30 && DungeonOfBossScene(gPlayState->sceneNum) >= 0 &&
        gPlayState->actorCtx.actorLists[ACTORCAT_BOSS].head != nullptr && !coop.isPlayingRemoteCutscene) {
        nlohmann::json payload;
        payload["type"] = BOSS_ROOM;
        payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        payload["sceneNum"] = gPlayState->sceneNum;
        payload["entranceIndex"] = gSaveContext.entranceIndex;
        payload["linkAge"] = gSaveContext.linkAge;
        SendJsonToRemote(payload);
    }

    if (!sHasPendingBossRoom) {
        return;
    }
    if (coop.frameCounter > sPendingBossRoomExpires ||
        gPlayState->sceneNum != DungeonOfBossScene(sPendingBossRoom.at("sceneNum").get<s16>())) {
        sHasPendingBossRoom = false;
        return;
    }
    if (!Coop_CanStartRemoteCutscene() || coop.framesInScene < 20) {
        return;
    }

    sHasPendingBossRoom = false;
    gPlayState->nextEntranceIndex = sPendingBossRoom.at("entranceIndex").get<s16>();
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_BLACK;
    Notification::Emit({ .prefix = NameOf(this, sPendingBossRoom.value("clientId", (uint32_t)0)),
                         .message = "found the boss, bringing you there" });
}

void Anchor::HandlePacket_BossRoom(nlohmann::json payload) {
    if (!roomState.syncBossIntros || !IsSaveLoaded()) {
        return;
    }
    // Only teammates in the same dungeon, with the same age
    if (gPlayState->sceneNum != DungeonOfBossScene(payload.at("sceneNum").get<s16>()) ||
        payload.value("linkAge", gSaveContext.linkAge) != gSaveContext.linkAge) {
        return;
    }
    sHasPendingBossRoom = true;
    sPendingBossRoom = payload;
    sPendingBossRoomExpires = coop.frameCounter + PENDING_FRAMES;
}

// MARK: - Shared age

static s32 sReportedAge = -1;
static bool sHasPendingAge = false;
static nlohmann::json sPendingAge;
static uint32_t sPendingAgeExpires = 0;

void Anchor::Coop_UpdateSharedAge() {
    if (sReportedAge < 0 || coop.framesSinceLoad < 2) {
        sReportedAge = gSaveContext.linkAge;
        sHasPendingAge = false;
        return;
    }

    // We changed age in the Temple of Time (pulling or returning the Master Sword)
    if (gPlayState->sceneNum == SCENE_TEMPLE_OF_TIME && gSaveContext.linkAge != sReportedAge &&
        coop.framesInScene > 20) {
        sReportedAge = gSaveContext.linkAge;
        if (roomState.syncAge) {
            nlohmann::json payload;
            payload["type"] = AGE_CHANGED;
            payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
            payload["linkAge"] = gSaveContext.linkAge;
            payload["entranceIndex"] = gSaveContext.entranceIndex;
            SendJsonToRemote(payload);
        }
    }

    if (!sHasPendingAge) {
        return;
    }
    s32 age = sPendingAge.at("linkAge").get<s32>();
    if (coop.frameCounter > sPendingAgeExpires || age == gSaveContext.linkAge) {
        sHasPendingAge = false;
        return;
    }
    if (!Coop_CanStartRemoteCutscene() || coop.framesInScene < 20) {
        return;
    }

    sHasPendingAge = false;
    sReportedAge = age; // don't send it back
    gPlayState->nextEntranceIndex = sPendingAge.at("entranceIndex").get<s16>();
    gPlayState->linkAgeOnLoad = (u8)age;
    gPlayState->transitionTrigger = TRANS_TRIGGER_START;
    gPlayState->transitionType = TRANS_TYPE_FADE_WHITE;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_WHITE_SLOW;
    Notification::Emit({ .prefix = NameOf(this, sPendingAge.value("clientId", (uint32_t)0)),
                         .message = age == LINK_AGE_ADULT ? "became an adult, the team follows"
                                                          : "became a child, the team follows" });
}

void Anchor::HandlePacket_AgeChanged(nlohmann::json payload) {
    if (!roomState.syncAge || !IsSaveLoaded()) {
        return;
    }
    if (payload.at("linkAge").get<s32>() == gSaveContext.linkAge) {
        return;
    }
    sHasPendingAge = true;
    sPendingAge = payload;
    sPendingAgeExpires = coop.frameCounter + PENDING_FRAMES;
}

// MARK: - Spectator camera

static s16 sSpectateCamId = -1;
static uint32_t sSpectateTarget = 0;
static Vec3f sSpectateEye;
static s16 sSpectateScene = -1;

static Actor* FindDummy(Anchor* anchor, uint32_t clientId) {
    for (int category = 0; category < ACTORCAT_MAX; category++) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[category].head; actor != nullptr; actor = actor->next) {
            if (actor->id == ACTOR_EN_OE2 && actor->update != nullptr &&
                anchor->GetDummyPlayerClientId(actor) == clientId) {
                return actor;
            }
        }
    }
    return nullptr;
}

static void StopSpectating() {
    if (sSpectateCamId >= 0 && sSpectateScene == gPlayState->sceneNum) {
        Play_ClearCamera(gPlayState, sSpectateCamId);
        Play_ChangeCameraStatus(gPlayState, CAM_ID_MAIN, CAM_STAT_ACTIVE);
    }
    sSpectateCamId = -1;
}

void Anchor::Coop_UpdateSpectator() {
    // The camera belongs to the scene, a new scene starts without it
    if (sSpectateCamId >= 0 && (sSpectateScene != gPlayState->sceneNum || coop.framesInScene < 2)) {
        sSpectateCamId = -1;
    }

    bool wantSpectate = CVarGetInteger(CVAR_REMOTE_ANCHOR("Coop.Spectate"), 1) && coop.isDown &&
                        gPlayState->gameOverCtx.state == GAMEOVER_INACTIVE &&
                        gPlayState->csCtx.state == CS_STATE_IDLE && gPlayState->transitionTrigger == TRANS_TRIGGER_OFF;

    // Teammates we can watch: in the same area and not down themselves
    std::vector<uint32_t> candidates;
    if (wantSpectate) {
        for (auto& [clientId, client] : clients) {
            if (Coop_IsTeammate(client) && client.sceneNum == gPlayState->sceneNum && !client.isDown &&
                FindDummy(this, clientId) != nullptr) {
                candidates.push_back(clientId);
            }
        }
    }
    if (candidates.empty()) {
        StopSpectating();
        return;
    }

    // L switches to the next teammate
    Input* input = &gPlayState->state.input[0];
    auto current = std::find(candidates.begin(), candidates.end(), sSpectateTarget);
    if (current == candidates.end()) {
        sSpectateTarget = candidates.front();
    } else if (CHECK_BTN_ALL(input->press.button, BTN_L)) {
        ++current;
        sSpectateTarget = current == candidates.end() ? candidates.front() : *current;
    }

    Actor* target = FindDummy(this, sSpectateTarget);
    if (target == nullptr) {
        StopSpectating();
        return;
    }

    Vec3f at = target->world.pos;
    at.y += 40.0f;
    s16 yaw = target->shape.rot.y;
    Vec3f wantedEye = { at.x - Math_SinS(yaw) * 220.0f, at.y + 70.0f, at.z - Math_CosS(yaw) * 220.0f };

    if (sSpectateCamId < 0) {
        sSpectateCamId = Play_CreateSubCamera(gPlayState);
        if (sSpectateCamId < 0) {
            return;
        }
        sSpectateScene = gPlayState->sceneNum;
        Play_ChangeCameraStatus(gPlayState, CAM_ID_MAIN, CAM_STAT_WAIT);
        Play_ChangeCameraStatus(gPlayState, sSpectateCamId, CAM_STAT_ACTIVE);
        sSpectateEye = wantedEye;
        Notification::Emit(
            { .prefix = "Spectating", .message = clients[sSpectateTarget].name + " (L: next teammate)" });
    }

    // Follow smoothly behind the teammate
    sSpectateEye.x += (wantedEye.x - sSpectateEye.x) * 0.15f;
    sSpectateEye.y += (wantedEye.y - sSpectateEye.y) * 0.15f;
    sSpectateEye.z += (wantedEye.z - sSpectateEye.z) * 0.15f;
    Play_CameraSetAtEye(gPlayState, sSpectateCamId, &at, &sSpectateEye);
}
