#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"

extern "C" {
#include "macros.h"
#include "functions.h"
extern PlayState* gPlayState;
void Save_SaveFile(void);
}

/**
 * GAME_OVER_CHOICE / TRADE_TIMER
 *
 *  - Game over (with team death): when the whole team is down everybody gets the game over menu. The first player to
 *    answer "Save?" and "Continue?" answers for the whole team; the others' menus follow that answer.
 *  - Trade quest timers: the Odd Mushroom, Frog and Eye Drops have to be delivered in time. When a teammate's timer
 *    starts, everyone holding the same item gets the same timer, and it stops for everyone when it stops for them.
 */

// Pause menu states of the game over menu (z_kaleido_scope)
static constexpr u16 GAMEOVER_MENU_SAVE_PROMPT = 0xE;
static constexpr u16 GAMEOVER_MENU_SAVED = 0xF;
static constexpr u16 GAMEOVER_MENU_CONTINUE_PROMPT = 0x10;
static constexpr u16 GAMEOVER_MENU_FADE_OUT = 0x11;

static u16 sLastPauseState = 0;
static bool sSkipNextTransition = false;
static bool sHasPendingChoice = false;
static std::string sPendingStage;
static u8 sPendingChoice = 0;
static uint32_t sPendingExpires = 0;
static uint32_t sPendingFrom = 0;

static void SendChoice(Anchor* anchor, const char* stage, u8 choice) {
    nlohmann::json payload;
    payload["type"] = Anchor::GAME_OVER_CHOICE;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["stage"] = stage;
    payload["choice"] = choice;
    anchor->SendJsonToRemote(payload);
}

// Same as what the "Continue?" prompt does on A, see z_kaleido_scope: dying in a boss room continues at the dungeon
static void ApplyContinueRespawnPoint() {
    Play_SaveSceneFlags(gPlayState);
    switch (gSaveContext.entranceIndex) {
        case ENTR_DEKU_TREE_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_DEKU_TREE_ENTRANCE;
            break;
        case ENTR_DODONGOS_CAVERN_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_DODONGOS_CAVERN_ENTRANCE;
            break;
        case ENTR_JABU_JABU_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_JABU_JABU_ENTRANCE;
            break;
        case ENTR_FOREST_TEMPLE_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_FOREST_TEMPLE_ENTRANCE;
            break;
        case ENTR_FIRE_TEMPLE_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_FIRE_TEMPLE_ENTRANCE;
            break;
        case ENTR_WATER_TEMPLE_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_WATER_TEMPLE_ENTRANCE;
            break;
        case ENTR_SPIRIT_TEMPLE_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_SPIRIT_TEMPLE_ENTRANCE;
            break;
        case ENTR_SHADOW_TEMPLE_BOSS_ENTRANCE:
            gSaveContext.entranceIndex = ENTR_SHADOW_TEMPLE_ENTRANCE;
            break;
        case ENTR_GANONDORF_BOSS_0:
            gSaveContext.entranceIndex = ENTR_GANONS_TOWER_0;
            break;
        default:
            break;
    }
}

void Anchor::Coop_UpdateGameOver() {
    if (!roomState.syncGameOver || !roomState.teamDeath) {
        sHasPendingChoice = false;
        return;
    }

    PauseContext* pauseCtx = &gPlayState->pauseCtx;
    u16 state = pauseCtx->state;
    bool inGameOverMenu =
        gPlayState->gameOverCtx.state >= GAMEOVER_DEATH_MENU && gPlayState->gameOverCtx.state < GAMEOVER_REVIVE_START;

    // Our own answers
    if (inGameOverMenu && state != sLastPauseState) {
        if (sSkipNextTransition) {
            sSkipNextTransition = false;
        } else if (sLastPauseState == GAMEOVER_MENU_SAVE_PROMPT &&
                   (state == GAMEOVER_MENU_SAVED || state == GAMEOVER_MENU_CONTINUE_PROMPT)) {
            SendChoice(this, "save", state == GAMEOVER_MENU_SAVED ? 0 : 1);
        } else if (sLastPauseState == GAMEOVER_MENU_CONTINUE_PROMPT && state == GAMEOVER_MENU_FADE_OUT) {
            SendChoice(this, "continue", (u8)pauseCtx->promptChoice);
        }
    }
    sLastPauseState = state;

    // A teammate's answer, applied once our menu shows the same question
    if (!sHasPendingChoice) {
        return;
    }
    if (coop.frameCounter > sPendingExpires) {
        sHasPendingChoice = false;
        return;
    }
    if (!inGameOverMenu) {
        return;
    }

    std::string name = clients.contains(sPendingFrom) ? clients[sPendingFrom].name : "A teammate";

    if (sPendingStage == "save" && state == GAMEOVER_MENU_SAVE_PROMPT) {
        sHasPendingChoice = false;
        if (sPendingChoice == 0) {
            Play_SaveSceneFlags(gPlayState);
            gSaveContext.savedSceneNum = gPlayState->sceneNum;
            Save_SaveFile();
            Notification::Emit({ .prefix = name, .message = "saved for the team" });
        }
        pauseCtx->promptChoice = 0;
        pauseCtx->state = GAMEOVER_MENU_CONTINUE_PROMPT;
        gPlayState->gameOverCtx.state++;
        sSkipNextTransition = true;
    } else if (sPendingStage == "continue" && state == GAMEOVER_MENU_CONTINUE_PROMPT) {
        sHasPendingChoice = false;
        pauseCtx->promptChoice = sPendingChoice;
        if (sPendingChoice == 0) {
            ApplyContinueRespawnPoint();
        }
        Notification::Emit({ .prefix = name, .message = sPendingChoice == 0 ? "continued for the team" : "quit" });
        pauseCtx->state = GAMEOVER_MENU_FADE_OUT;
        sSkipNextTransition = true;
    }
}

void Anchor::HandlePacket_GameOverChoice(nlohmann::json payload) {
    if (!roomState.syncGameOver || !roomState.teamDeath || !IsSaveLoaded()) {
        return;
    }

    sHasPendingChoice = true;
    sPendingStage = payload.at("stage").get<std::string>();
    sPendingChoice = payload.at("choice").get<u8>();
    sPendingFrom = payload.value("clientId", (uint32_t)0);
    sPendingExpires = coop.frameCounter + 20 * 30;
}

// MARK: - Trade quest timers

static s16 sLastSubTimerState = SUBTIMER_STATE_OFF;
static bool sTradeTimerRunning = false;
static bool sApplyingTimer = false;

static bool HasSpoilingTradeItem(u8 item) {
    return item == ITEM_ODD_MUSHROOM || item == ITEM_FROG || item == ITEM_EYEDROPS;
}

void Anchor::Coop_UpdateTradeTimer() {
    s16 state = gSaveContext.subTimerState;
    if (state == sLastSubTimerState) {
        return;
    }

    u8 item = INV_CONTENT(ITEM_TRADE_ADULT);
    bool started = sLastSubTimerState == SUBTIMER_STATE_OFF && state == SUBTIMER_STATE_DOWN_INIT;
    bool stopped = state == SUBTIMER_STATE_OFF && sTradeTimerRunning;
    sLastSubTimerState = state;

    if (started && HasSpoilingTradeItem(item)) {
        sTradeTimerRunning = true;
        if (!sApplyingTimer && roomState.syncTradeTimers) {
            nlohmann::json payload;
            payload["type"] = TRADE_TIMER;
            payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
            payload["start"] = true;
            payload["item"] = item;
            payload["seconds"] = gSaveContext.subTimerSeconds;
            SendJsonToRemote(payload);
        }
    } else if (stopped) {
        sTradeTimerRunning = false;
        if (!sApplyingTimer && roomState.syncTradeTimers) {
            nlohmann::json payload;
            payload["type"] = TRADE_TIMER;
            payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
            payload["start"] = false;
            SendJsonToRemote(payload);
        }
    }
    sApplyingTimer = false;
}

void Anchor::HandlePacket_TradeTimer(nlohmann::json payload) {
    if (!roomState.syncTradeTimers || !IsSaveLoaded()) {
        return;
    }

    if (payload.at("start").get<bool>()) {
        // Only for teammates carrying the same item (it arrives through the item sync)
        if (gSaveContext.subTimerState != SUBTIMER_STATE_OFF ||
            INV_CONTENT(ITEM_TRADE_ADULT) != payload.at("item").get<u8>()) {
            return;
        }
        sApplyingTimer = true;
        Interface_SetSubTimer(payload.at("seconds").get<s16>());
        gSaveContext.eventInf[1] &= ~1;
    } else if (sTradeTimerRunning) {
        sApplyingTimer = true;
        gSaveContext.subTimerState = SUBTIMER_STATE_OFF;
    }
}
