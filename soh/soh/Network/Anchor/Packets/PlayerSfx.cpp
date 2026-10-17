#include "soh/Network/Anchor/Anchor.h"
#include <nlohmann/json.hpp>

extern "C" {
#include "functions.h"
extern PlayState* gPlayState;
}

/**
 * PLAYER_SFX
 *
 * Sound effects, only sent to other clients in the same scene as the player
 */

void Anchor::SendPacket_PlayerSfx(u16 sfxId) {
    if (!IsSaveLoaded()) {
        return;
    }

    nlohmann::json payload;

    payload["type"] = PLAYER_SFX;
    payload["sfxId"] = sfxId;
    payload["quiet"] = true;

    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            payload["targetClientId"] = clientId;
            SendJsonToRemote(payload);
        }
    }
}

void Anchor::HandlePacket_PlayerSfx(nlohmann::json payload) {
    uint32_t clientId = payload.at("clientId").get<uint32_t>();
    u16 sfxId = payload.at("sfxId").get<u16>();

    if (!clients.contains(clientId) || !clients[clientId].player) {
        return;
    }

    Player_PlaySfx((Actor*)clients[clientId].player, sfxId);

    // Megaton Hammer hitting the ground: show the shockwave and shake the camera a little when close
    if (sfxId == NA_SE_IT_HAMMER_HIT && roomState.syncPlayerEffects) {
        Player* dummy = clients[clientId].player;
        static Vec3f zeroVec = { 0.0f, 0.0f, 0.0f };
        Vec3f pos = dummy->meleeWeaponInfo[0].tip;
        pos.y = dummy->actor.world.pos.y;
        EffectSsBlast_SpawnWhiteShockwave(gPlayState, &pos, &zeroVec, &zeroVec);
        if (dummy->actor.xyzDistToPlayerSq < 400.0f * 400.0f) {
            Actor_RequestQuakeWithSpeed(gPlayState, 27767, 4, 10);
        }
    }
}
