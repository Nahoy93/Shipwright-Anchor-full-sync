#include <nlohmann/json.hpp>

#include "soh/Network/Anchor/Anchor.h"
#include "soh/Notification/Notification.h"

extern "C" {
extern PlayState* gPlayState;
}

/**
 * PLAYER_DOWN / PLAYER_REVIVED
 *
 * Notifications for the team death mode (see Coop.cpp). The authoritative "is this player down" state lives in the
 * client state (UPDATE_CLIENT_STATE, isDown) so it survives reconnects, these packets are only here to tell teammates
 * what just happened.
 */

void Anchor::SendPacket_PlayerDown() {
    if (!IsSaveLoaded()) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = PLAYER_DOWN;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["sceneNum"] = gPlayState->sceneNum;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_PlayerDown(nlohmann::json payload) {
    uint32_t clientId = payload.at("clientId").get<uint32_t>();
    if (!clients.contains(clientId)) {
        return;
    }

    AnchorClient& client = clients[clientId];
    client.isDown = true;

    if (!roomState.teamDeath) {
        return;
    }

    bool sameScene = IsSaveLoaded() && payload.value("sceneNum", (s16)SCENE_ID_MAX) == gPlayState->sceneNum;
    Notification::Emit({
        .prefix = client.name,
        .prefixColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
        .message = sameScene ? "is down! Stand next to them to revive them" : "is down! Go find them to revive them",
        .remainingTime = 6.0f,
    });
}

void Anchor::SendPacket_PlayerRevived(uint32_t reviverClientId) {
    nlohmann::json payload;
    payload["type"] = PLAYER_REVIVED;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["reviverClientId"] = reviverClientId;

    SendJsonToRemote(payload);

    if (clients.contains(reviverClientId)) {
        Notification::Emit({
            .prefix = clients[reviverClientId].name,
            .message = "revived you!",
        });
    }
}

void Anchor::HandlePacket_PlayerRevived(nlohmann::json payload) {
    uint32_t clientId = payload.at("clientId").get<uint32_t>();
    if (!clients.contains(clientId)) {
        return;
    }

    AnchorClient& client = clients[clientId];
    client.isDown = false;

    uint32_t reviverClientId = payload.value("reviverClientId", (uint32_t)0);
    std::string reviverName;
    if (reviverClientId == ownClientId) {
        reviverName = "you";
    } else if (clients.contains(reviverClientId)) {
        reviverName = clients[reviverClientId].name;
    } else {
        reviverName = "a teammate";
    }

    Notification::Emit({
        .prefix = client.name,
        .message = "was revived by",
        .suffix = reviverName,
    });
}
