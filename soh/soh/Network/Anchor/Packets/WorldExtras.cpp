#include <nlohmann/json.hpp>

#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "functions.h"
#include "src/overlays/actors/ovl_Bg_Breakwall/z_bg_breakwall.h"
#include "src/overlays/actors/ovl_Bg_Hidan_Dalm/z_bg_hidan_dalm.h"
#include "src/overlays/actors/ovl_Bg_Ydan_Sp/z_bg_ydan_sp.h"
#include "src/overlays/actors/ovl_Obj_Warp2block/z_obj_warp2block.h"
extern PlayState* gPlayState;
void ObjWarp2block_Spawn(ObjWarp2block* self, PlayState* play);
void func_80BA2610(ObjWarp2block* self, PlayState* play);
}

/**
 * World extras
 *
 *  - Things that are opened once and remembered with a switch flag (spider webs burnt with a stick or fire arrow, red
 *    ice melted with blue fire, bombable walls and boulders, hammer pillars). The flag itself already reaches
 * teammates, but these objects only look at it when the room loads. Now they disappear right away for teammates in the
 * room.
 *  - Song of Time warp blocks (Spirit Temple): when a teammate swaps a block, it swaps for you as well.
 *  - WEATHER: rain, storms, lightning and dark skies follow the teammate with the lowest id in the area.
 */

// MARK: - Objects opened with a switch flag

enum class OpenEffect { Fire, Melt, Rubble };

struct FlagGatedObject {
    s16 actorId;
    OpenEffect effect;
};

static const FlagGatedObject sFlagGatedObjects[] = {
    { ACTOR_BG_YDAN_SP, OpenEffect::Fire },       { ACTOR_BG_ICE_SHELTER, OpenEffect::Melt },
    { ACTOR_BG_BREAKWALL, OpenEffect::Rubble },   { ACTOR_OBJ_BOMBIWA, OpenEffect::Rubble },
    { ACTOR_OBJ_HAMISHI, OpenEffect::Rubble },    { ACTOR_BG_HIDAN_KOWARERUKABE, OpenEffect::Rubble },
    { ACTOR_BG_JYA_BOMBIWA, OpenEffect::Rubble }, { ACTOR_BG_SPOT17_BAKUDANKABE, OpenEffect::Rubble },
    { ACTOR_BG_HIDAN_DALM, OpenEffect::Rubble },
};

// The switch flag that opens this object, or -1
static s32 OpenFlagOf(Actor* actor) {
    switch (actor->id) {
        case ACTOR_BG_YDAN_SP:
            return ((BgYdanSp*)actor)->isDestroyedSwitchFlag;
        case ACTOR_BG_ICE_SHELTER:
            // Some red ice has no flag and comes back every time
            return ((actor->params >> 6) & 1) ? -1 : (actor->params & 0x3F);
        case ACTOR_BG_BREAKWALL:
            // The walls without a model belong to the King Dodongo floor cutscene
            return ((BgBreakwall*)actor)->bombableWallDList == nullptr ? -1 : (actor->params & 0x3F);
        case ACTOR_OBJ_BOMBIWA:
        case ACTOR_OBJ_HAMISHI:
        case ACTOR_BG_JYA_BOMBIWA:
        case ACTOR_BG_SPOT17_BAKUDANKABE:
            return actor->params & 0x3F;
        case ACTOR_BG_HIDAN_KOWARERUKABE:
            return (actor->params >> 8) & 0x3F;
        case ACTOR_BG_HIDAN_DALM:
            return ((BgHidanDalm*)actor)->switchFlag;
        default:
            return -1;
    }
}

static void ShowOpenEffect(Actor* actor, OpenEffect effect) {
    Vec3f pos = actor->world.pos;
    switch (effect) {
        case OpenEffect::Fire: {
            static Vec3f velocity = { 0.0f, 1.0f, 0.0f };
            static Vec3f accel = { 0.0f, 0.5f, 0.0f };
            static Color_RGBA8 primColor = { 255, 255, 100, 255 };
            static Color_RGBA8 envColor = { 255, 50, 0, 0 };
            for (int i = 0; i < 8; i++) {
                Vec3f flamePos = { pos.x + Rand_CenteredFloat(100.0f), pos.y + Rand_ZeroFloat(80.0f),
                                   pos.z + Rand_CenteredFloat(100.0f) };
                func_8002836C(gPlayState, &flamePos, &velocity, &accel, &primColor, &envColor, 400, 0, 20);
            }
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &pos, 40, NA_SE_EV_FLAME_IGNITION);
            break;
        }
        case OpenEffect::Melt: {
            static Vec3f velocity = { 0.0f, 2.0f, 0.0f };
            static Vec3f accel = { 0.0f, 0.0f, 0.0f };
            for (int i = 0; i < 6; i++) {
                Vec3f smokePos = { pos.x + Rand_CenteredFloat(80.0f), pos.y + Rand_ZeroFloat(60.0f),
                                   pos.z + Rand_CenteredFloat(80.0f) };
                EffectSsIceSmoke_Spawn(gPlayState, &smokePos, &velocity, &accel, 200);
            }
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &pos, 40, NA_SE_EV_ICE_MELT);
            break;
        }
        case OpenEffect::Rubble:
            EffectSsHahen_SpawnBurst(gPlayState, &pos, 15.0f, 0, 25, 15, 15, -1, 10, NULL);
            SoundSource_PlaySfxAtFixedWorldPos(gPlayState, &pos, 40, NA_SE_EV_WALL_BROKEN);
            break;
    }
}

void Anchor::Coop_OnRemoteSwitchFlag(s16 sceneNum, s16 flag) {
    if (!roomState.syncInteractions || !IsSaveLoaded() || sceneNum != gPlayState->sceneNum) {
        return;
    }

    for (int category = 0; category < ACTORCAT_MAX; category++) {
        for (Actor* actor = gPlayState->actorCtx.actorLists[category].head; actor != nullptr; actor = actor->next) {
            if (actor->update == nullptr) {
                continue;
            }
            for (auto& object : sFlagGatedObjects) {
                if (actor->id == object.actorId && OpenFlagOf(actor) == flag) {
                    ShowOpenEffect(actor, object.effect);
                    Actor_Kill(actor);
                    break;
                }
            }
        }
    }
}

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
