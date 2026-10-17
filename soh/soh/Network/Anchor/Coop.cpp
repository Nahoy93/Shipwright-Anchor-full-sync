#include "Anchor.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Notification/Notification.h"
#include "soh/ResourceManagerHelpers.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
#include "seqcmd.h"
#include "src/overlays/actors/ovl_Door_Warp1/z_door_warp1.h"
extern PlayState* gPlayState;
}

/**
 * CO-OP
 *
 * Extra synchronisation on top of the regular Anchor item/flag sync:
 *
 *  - Team death:   a player whose health reaches zero stays down on the ground instead of getting a game over.
 *                  A teammate standing next to them for ~2 seconds revives them (with a fairy animation). When every
 *                  online teammate is down at the same time, everybody gets the normal game over and continues from
 *                  their checkpoint / last save.
 *  - Rupees:       rupees found by a teammate are added to everyone's wallet ("shared income"). Spending is
 *                  individual, it only affects your own wallet.
 *  - Enemies:      enemies and bosses defeated by a teammate die for everyone in the same area (see EnemyDefeated.cpp)
 *  - Cutscenes:    cutscenes triggered by a teammate are shown to the rest of the team (see CutsceneStart.cpp)
 *
 * All of these are room settings that the room owner can toggle in the Anchor menu.
 */

// Distance (in world units) a teammate has to be within to revive a downed player
static constexpr float REVIVE_DISTANCE = 80.0f;
// How many player updates (20 per second) a teammate has to stay close to revive a downed player
static constexpr uint16_t REVIVE_FRAMES = 40;
// Health given back on revive (0x10 = one heart)
static constexpr s16 REVIVE_HEALTH = 0x30;

void Anchor::ResetCoopState() {
    coop = CoopState{};
    EnemySync_Reset();
}

bool Anchor::Coop_IsTeammate(const AnchorClient& client) {
    std::string ownTeamId = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    return !client.self && client.online && client.isSaveLoaded && client.teamId == ownTeamId;
}

bool Anchor::Coop_IsAnyTeammateAlive() {
    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client) && !client.isDown) {
            return true;
        }
    }
    return false;
}

// MARK: - Hooks

void Anchor::RegisterCoopHooks() {
    COND_HOOK(OnGameFrameUpdate, isConnected, [&]() { Coop_OnGameFrame(); });

    COND_HOOK(OnPlayerUpdate, isConnected, [&]() { Coop_OnPlayerUpdate(); });

    COND_HOOK(OnLoadGame, isConnected, [&](s16 fileNum) {
        AnchorSessions_RecordSaveFile(fileNum);
        coop.isDown = false;
        coop.allowGameOver = false;
        coop.isReviving = false;
        coop.rupeesInitialized = false;
        coop.pendingRemoteRupees = 0;
        coop.outgoingRupees = 0;
        coop.framesSinceLoad = 0;
        coop.hasPendingCutscene = false;
        coop.isPlayingRemoteCutscene = false;
    });

    COND_HOOK(OnSceneSpawnActors, isConnected, [&]() {
        coop.framesInScene = 0;
        EnemySync_Reset();
        Interactions_Reset();
    });

    COND_HOOK(OnActorUpdate, isConnected, [&](void* refActor) { EnemySync_OnActorUpdate((Actor*)refActor); });

    COND_HOOK(ShouldActorUpdate, isConnected,
              [&](void* refActor, bool* should) { Interactions_ShouldActorUpdate((Actor*)refActor, should); });

    COND_HOOK(OnActorKill, isConnected, [&](void* refActor) {
        Interactions_OnActorKill((Actor*)refActor);
        Drops_OnActorKill((Actor*)refActor);
    });

    COND_HOOK(OnActorInit, isConnected, [&](void* refActor) { Drops_OnActorInit((Actor*)refActor); });

    COND_VB_SHOULD(VB_SILVER_COLLECT, isConnected, {
        Actor* silverRupee = va_arg(args, Actor*);
        if (Anchor::Instance->Interactions_ForceSilverCollect(silverRupee)) {
            *should = true;
        }
    });

    COND_HOOK(OnWarpSongLeave, isConnected, [&]() { SendPacket_WarpSong(); });

    COND_HOOK(OnEnemyDefeat, isConnected, [&](void* refActor) { SendPacket_EnemyDefeated((Actor*)refActor); });

    COND_HOOK(OnBossDefeat, isConnected, [&](void* refActor) { SendPacket_BossDefeated((Actor*)refActor); });

    COND_ID_HOOK(OnActorInit, ACTOR_DOOR_WARP1, isConnected,
                 [&](void* refActor) { SendPacket_WarpSpawned((Actor*)refActor); });
}

void Anchor::Coop_OnGameFrame() {
    coop.frameCounter++;

    if (!IsSaveLoaded()) {
        coop.rupeesInitialized = false;
        coop.lastCsState = CS_STATE_IDLE;
        return;
    }

    coop.framesSinceLoad++;
    coop.framesInScene++;

    Coop_UpdateRupees();
    Coop_UpdateCutsceneWatch();
    Coop_ProcessPendingCutscene();
    Coop_ProcessPendingBossKill();
    Coop_UpdateEnemySync();
    Coop_UpdateSongWatch();
    Coop_ProcessPendingWarp();
    Coop_UpdateInteractions();
    Coop_UpdateLens();
    Coop_UpdateTimeSync();
    Coop_UpdateDrops();
    Coop_UpdateFaroresWind();
    Coop_UpdateMusic();
}

void Anchor::Coop_OnPlayerUpdate() {
    if (!IsSaveLoaded()) {
        return;
    }

    Coop_UpdateDownState(GET_PLAYER(gPlayState));
}

// MARK: - Team death

void Anchor::Coop_UpdateDownState(Player* player) {
    GameOverContext* gameOverCtx = &gPlayState->gameOverCtx;
    bool isDead = player->stateFlags1 & PLAYER_STATE1_DEAD;

    if (!roomState.teamDeath) {
        if (coop.isDown) {
            coop.isDown = false;
            SendPacket_UpdateClientState();
        }
        return;
    }

    if (!coop.isDown) {
        // GAMEOVER_DEATH_START is only used when there was no fairy to save us
        if (isDead && gameOverCtx->state >= GAMEOVER_DEATH_START && gameOverCtx->state <= GAMEOVER_DEATH_DELAY_MENU) {
            coop.isDown = true;
            coop.isReviving = false;
            coop.reviveTimer = 0;
            coop.allowGameOver = !Coop_IsAnyTeammateAlive();

            SendPacket_UpdateClientState();
            SendPacket_PlayerDown();

            if (!coop.allowGameOver) {
                Notification::Emit({
                    .prefix = "You are down!",
                    .prefixColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                    .message = "A teammate can revive you by standing next to you",
                    .remainingTime = 6.0f,
                });
            }
        }
        return;
    }

    // Revive animation in progress (the fairy sequence of func_80843AE8)
    if (coop.isReviving) {
        // The fairy sequence fully heals, give back a few hearts instead
        if (!coop.reviveHealthApplied && player->av2.actionVar2 == -1) {
            gSaveContext.healthAccumulator = MIN(REVIVE_HEALTH, gSaveContext.healthCapacity);
            coop.reviveHealthApplied = true;
        }

        if (!isDead) {
            coop.isDown = false;
            coop.isReviving = false;
            Letterbox_SetSizeTarget(0);
            // The game over fanfare stopped the scene music, start it again
            Environment_PlaySceneSequence(gPlayState);
            gSaveContext.seqId = gPlayState->sequenceCtx.seqId;
            gSaveContext.natureAmbienceId = gPlayState->sequenceCtx.natureAmbienceId;

            SendPacket_UpdateClientState();
            SendPacket_PlayerRevived(coop.reviverClientId);
        }
        return;
    }

    // We got up some other way (game over -> continue, soft reset, ...)
    if (!isDead) {
        coop.isDown = false;
        coop.allowGameOver = false;
        SendPacket_UpdateClientState();
        return;
    }

    if (!coop.allowGameOver && !Coop_IsAnyTeammateAlive()) {
        coop.allowGameOver = true;
        Notification::Emit({
            .prefix = "The whole team is down!",
            .prefixColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
            .message = "Game over",
        });
    }

    if (coop.allowGameOver) {
        // Let the vanilla game over continue
        return;
    }

    // Hold the game over: the player action sets DELAY_MENU every frame once the death animation is done
    if (gameOverCtx->state == GAMEOVER_DEATH_DELAY_MENU) {
        gameOverCtx->state = GAMEOVER_DEATH_WAIT_GROUND;
    }

    // Only allow reviving once the death animation is done and we're lying on the ground
    if (gameOverCtx->state != GAMEOVER_DEATH_WAIT_GROUND) {
        return;
    }

    uint32_t reviverId = 0;
    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client) && !client.isDown && client.player != nullptr &&
            client.sceneNum == gPlayState->sceneNum &&
            Math_Vec3f_DistXYZ(&client.player->actor.world.pos, &player->actor.world.pos) < REVIVE_DISTANCE) {
            reviverId = clientId;
            break;
        }
    }

    if (reviverId == 0) {
        coop.reviveTimer = 0;
        return;
    }

    if (coop.reviveTimer == 0) {
        Notification::Emit({
            .prefix = clients[reviverId].name,
            .message = "is reviving you...",
            .remainingTime = 2.0f,
        });
    }

    if (++coop.reviveTimer < REVIVE_FRAMES) {
        return;
    }

    // Revive! This reuses the "revived by a fairy" path of the death action (func_80843AE8)
    coop.isReviving = true;
    coop.reviveHealthApplied = false;
    coop.reviverClientId = reviverId;
    player->av1.actionVar1 = 1;
    player->av2.actionVar2 = 0;
    gameOverCtx->state = GAMEOVER_REVIVE_START;
    SEQCMD_STOP_SEQUENCE(SEQ_PLAYER_FANFARE, 0);
}

// MARK: - Shared rupee income

void Anchor::Coop_UpdateRupees() {
    if (!roomState.shareRupees) {
        coop.rupeesInitialized = false;
        return;
    }

    s16 currentRupees = gSaveContext.rupees;

    if (!coop.rupeesInitialized) {
        coop.rupeesInitialized = true;
        coop.lastRupees = currentRupees;
        coop.pendingRemoteRupees = 0;
        coop.outgoingRupees = 0;
        coop.rupeeFlushTimer = 0;
        return;
    }

    s32 delta = currentRupees - coop.lastRupees;
    coop.lastRupees = currentRupees;

    if (delta > 0) {
        // Rupees coming in from teammates are added to the accumulator and count up one per frame, don't send those
        // back out again
        s32 absorbed = MIN(delta, coop.pendingRemoteRupees);
        coop.pendingRemoteRupees -= absorbed;
        coop.outgoingRupees += delta - absorbed;
    }

    // Once the accumulator is empty (or was eaten up by spending / a full wallet) nothing more can arrive
    if (gSaveContext.rupeeAccumulator <= 0) {
        coop.pendingRemoteRupees = 0;
    }

    if (coop.outgoingRupees > 0) {
        coop.rupeeFlushTimer++;
        // Send once the counting has finished, or at least once a second while it is still going
        if (gSaveContext.rupeeAccumulator <= 0 || coop.rupeeFlushTimer >= 20) {
            SendPacket_RupeeIncome(coop.outgoingRupees);
            coop.outgoingRupees = 0;
            coop.rupeeFlushTimer = 0;
        }
    }
}

// MARK: - Cutscene watch

void Anchor::Coop_UpdateCutsceneWatch() {
    s16 csState = gPlayState->csCtx.state;

    if (coop.lastCsState == CS_STATE_IDLE && csState != CS_STATE_IDLE) {
        if (coop.isPlayingRemoteCutscene) {
            // This is the cutscene a teammate asked us to play, don't send it back
            coop.remoteCutsceneGrace = 0;
        } else {
            SendPacket_CutsceneStart();
        }
    } else if (coop.lastCsState != CS_STATE_IDLE && csState == CS_STATE_IDLE) {
        coop.isPlayingRemoteCutscene = false;
    }

    // Give up on a remote cutscene that never started (scene load failed, cutscene skipped, ...)
    if (coop.isPlayingRemoteCutscene && csState == CS_STATE_IDLE && ++coop.remoteCutsceneGrace > 300) {
        coop.isPlayingRemoteCutscene = false;
    }

    coop.lastCsState = csState;
}

bool Anchor::Coop_CanStartRemoteCutscene() {
    Player* player = GET_PLAYER(gPlayState);

    if (coop.isDown || coop.isPlayingRemoteCutscene) {
        return false;
    }

    if (gPlayState->csCtx.state != CS_STATE_IDLE || gSaveContext.cutsceneTrigger != 0) {
        return false;
    }

    if (gPlayState->transitionTrigger != TRANS_TRIGGER_OFF || gPlayState->transitionMode != TRANS_MODE_OFF) {
        return false;
    }

    if (gPlayState->gameOverCtx.state != GAMEOVER_INACTIVE || gPlayState->pauseCtx.state != 0) {
        return false;
    }

    if (Message_GetState(&gPlayState->msgCtx) != TEXT_STATE_NONE) {
        return false;
    }

    if (player->stateFlags1 &
        (PLAYER_STATE1_DEAD | PLAYER_STATE1_GETTING_ITEM | PLAYER_STATE1_IN_ITEM_CS | PLAYER_STATE1_IN_CUTSCENE)) {
        return false;
    }

    return true;
}

void Anchor::Coop_StartScriptCutscene(const std::string& path) {
    // csCtx.segment holds the "__OTR__..." resource path, which is resolved every frame while the cutscene runs. Keep
    // our own copy of the string alive for that whole time.
    coop.remoteCutscenePath = path;
    gPlayState->csCtx.segment = (void*)coop.remoteCutscenePath.c_str();
    gSaveContext.cutsceneTrigger = 1;
    coop.isPlayingRemoteCutscene = true;
    coop.remoteCutsceneGrace = 0;
}
