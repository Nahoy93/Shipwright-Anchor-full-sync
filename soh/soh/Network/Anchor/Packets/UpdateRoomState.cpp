#include <nlohmann/json.hpp>

#include "soh/Network/Anchor/Anchor.h"

extern "C" {
extern PlayState* gPlayState;
}

/**
 * UPDATE_ROOM_STATE
 */

nlohmann::json Anchor::PrepRoomState() {
    nlohmann::json payload;
    payload["ownerClientId"] = ownClientId;
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));

    if (isGlobalRoom) {
        // Global room uses hardcoded settings
        payload["pvpMode"] = 0;
        payload["showLocationsMode"] = 0;
        payload["teleportMode"] = 0;
        payload["syncItemsAndFlags"] = 0;
        payload["shareRupees"] = 0;
        payload["teamDeath"] = 0;
        payload["syncEnemies"] = 0;
        payload["syncCutscenes"] = 0;
        payload["syncEnemyMovement"] = 0;
        payload["playerUpdateRate"] = 20;
        payload["enemyUpdateRate"] = 10;
        payload["syncSongs"] = 0;
        payload["syncWarpSongs"] = 0;
        payload["syncInteractions"] = 0;
    } else {
        payload["pvpMode"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.PvpMode"), 1);
        payload["showLocationsMode"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.ShowLocationsMode"), 1);
        payload["teleportMode"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.TeleportMode"), 1);
        payload["syncItemsAndFlags"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncItemsAndFlags"), 1);
        payload["shareRupees"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.ShareRupees"), 1);
        payload["teamDeath"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.TeamDeath"), 1);
        payload["syncEnemies"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemies"), 1);
        payload["syncCutscenes"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncCutscenes"), 2);
        payload["syncEnemyMovement"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemyMovement"), 1);
        payload["playerUpdateRate"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.PlayerUpdateRate"), 20);
        payload["enemyUpdateRate"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.EnemyUpdateRate"), 10);
        payload["syncSongs"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncSongs"), 1);
        payload["syncWarpSongs"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncWarpSongs"), 1);
        payload["syncInteractions"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncInteractions"), 1);
    }

    return payload;
}

void Anchor::SendPacket_UpdateRoomState() {
    nlohmann::json payload;
    payload["type"] = UPDATE_ROOM_STATE;
    payload["state"] = PrepRoomState();

    Network::SendJsonToRemote(payload);
}

void Anchor::HandlePacket_UpdateRoomState(nlohmann::json payload) {
    if (!payload.contains("state")) {
        return;
    }

    roomState.ownerClientId = payload["state"]["ownerClientId"].get<uint32_t>();
    roomState.pvpMode = payload["state"]["pvpMode"].get<u8>();
    roomState.showLocationsMode = payload["state"]["showLocationsMode"].get<u8>();
    roomState.teleportMode = payload["state"]["teleportMode"].get<u8>();
    roomState.syncItemsAndFlags = payload["state"]["syncItemsAndFlags"].get<u8>();
    // Co-op settings, rooms created by older clients don't have these
    roomState.shareRupees = payload["state"].value("shareRupees", (u8)0);
    roomState.teamDeath = payload["state"].value("teamDeath", (u8)0);
    roomState.syncEnemies = payload["state"].value("syncEnemies", (u8)0);
    roomState.syncCutscenes = payload["state"].value("syncCutscenes", (u8)0);
    roomState.syncEnemyMovement = payload["state"].value("syncEnemyMovement", (u8)0);
    roomState.playerUpdateRate = payload["state"].value("playerUpdateRate", (u8)20);
    roomState.enemyUpdateRate = payload["state"].value("enemyUpdateRate", (u8)10);
    roomState.syncSongs = payload["state"].value("syncSongs", (u8)0);
    roomState.syncWarpSongs = payload["state"].value("syncWarpSongs", (u8)0);
    roomState.syncInteractions = payload["state"].value("syncInteractions", (u8)0);
}
