#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "src/overlays/actors/ovl_Obj_Warp2block/z_obj_warp2block.h"
extern PlayState* gPlayState;
void ObjWarp2block_Spawn(ObjWarp2block* self, PlayState* play);
void func_80BA2610(ObjWarp2block* self, PlayState* play);
}

/**
 * World extras
 *
 *  - Song of Time warp blocks (Spirit Temple): when a teammate swaps a block, it swaps for you as well.
 *  - WEATHER: rain, storms, lightning and dark skies follow the teammate with the lowest id in the area.
 *
 * (Webs, red ice, bombable walls, boulders and hammer pillars opened by a teammate already open right away through
 * the switch flag hooks in HookHandlers.cpp, with their real animations.)
 */

// MARK: - Song of Time warp blocks

void Anchor::Coop_UpdateWarpBlocks() {
    if (!roomState.syncInteractions || !roomState.syncSongs || !Coop_AnyTeammateInScene()) {
        return;
    }

    for (Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_ITEMACTION].head; actor != nullptr;
         actor = actor->next) {
        if (actor->id != ACTOR_OBJ_WARP2BLOCK || actor->update == nullptr || actor->child == nullptr) {
            continue;
        }
        ObjWarp2block* block = (ObjWarp2block*)actor;
        if (block->actionFunc != func_80BA2610 || block->unk_170 > 0 || block->unk_16C > 0) {
            continue;
        }

        // The block remembers being swapped with its switch flag; a teammate's swap only set the flag so far
        bool swapped = Math3D_Vec3fDistSq(&actor->world.pos, &actor->home.pos) >= 0.01f;
        bool shouldBeSwapped = Flags_GetSwitch(gPlayState, actor->params & 0x3F);
        if (swapped == shouldBeSwapped) {
            continue;
        }
        // Not while someone stands on one of them
        if (DynaPolyActor_IsPlayerAbove(&block->dyna) || DynaPolyActor_IsPlayerAbove((DynaPolyActor*)actor->child)) {
            continue;
        }

        ObjWarp2block_Spawn(block, gPlayState);
        block->unk_16C = 0xA0;
        block->unk_170 = 0xC;
    }
}

// MARK: - Weather

struct WeatherState {
    u8 rain = 0;
    u8 stormRain = 0;
    u8 lightning = 0;
    u8 gloomy = 0;

    bool operator==(const WeatherState& o) const {
        return rain == o.rain && stormRain == o.stormRain && lightning == o.lightning && gloomy == o.gloomy;
    }
};

static WeatherState sLastWeather;
static uint32_t sWeatherTimer = 0;
static size_t sWeatherTeammates = 0;

static WeatherState CurrentWeather() {
    WeatherState state;
    state.rain = gPlayState->envCtx.unk_EE[0];
    state.stormRain = gPlayState->envCtx.unk_F2[0];
    state.lightning = gPlayState->envCtx.lightningMode;
    state.gloomy = gPlayState->envCtx.gloomySkyMode;
    return state;
}

void Anchor::Coop_UpdateWeather() {
    if (!roomState.syncWeather || ++sWeatherTimer < 10) {
        return;
    }
    sWeatherTimer = 0;

    if (!Coop_AnyTeammateInScene() || !Coop_IsLowestIdInScene()) {
        sWeatherTeammates = 0;
        return;
    }

    size_t teammates = 0;
    for (auto& [clientId, client] : clients) {
        if (Coop_IsTeammate(client) && client.sceneNum == gPlayState->sceneNum) {
            teammates++;
        }
    }

    WeatherState state = CurrentWeather();
    if (state == sLastWeather && teammates <= sWeatherTeammates) {
        sWeatherTeammates = teammates;
        return;
    }
    sLastWeather = state;
    sWeatherTeammates = teammates;

    nlohmann::json payload;
    payload["type"] = WEATHER;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["weather"] = { state.rain, state.stormRain, state.lightning, state.gloomy };
    Coop_SendToTeammatesInScene(payload);
}

void Anchor::HandlePacket_Weather(nlohmann::json payload) {
    if (!roomState.syncWeather || !IsSaveLoaded() || payload.at("sceneNum").get<s16>() != gPlayState->sceneNum ||
        gPlayState->csCtx.state != CS_STATE_IDLE) {
        return;
    }

    auto& v = payload.at("weather");
    EnvironmentContext* envCtx = &gPlayState->envCtx;
    envCtx->unk_EE[0] = v.at(0).get<u8>();
    envCtx->unk_F2[0] = v.at(1).get<u8>();
    envCtx->lightningMode = v.at(2).get<u8>();
    u8 gloomy = v.at(3).get<u8>();
    // Dark skies fade in and out by themselves, only start or end them
    if (gloomy != 0 && envCtx->gloomySkyMode != gloomy) {
        envCtx->gloomySkyMode = gloomy;
        envCtx->unk_DE = 0;
    }
}
