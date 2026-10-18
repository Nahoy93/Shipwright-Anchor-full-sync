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
        payload["syncPlayerEffects"] = 0;
        payload["syncTime"] = 0;
        payload["syncMusic"] = 0;
        payload["objectUpdateRate"] = 20;
        payload["timeSyncSeconds"] = 5;
        payload["syncRealProjectiles"] = 0;
        payload["syncPlatforms"] = 0;
        payload["syncGameOver"] = 0;
        payload["syncTradeTimers"] = 0;
        payload["syncWeather"] = 0;
        payload["syncSongTeaching"] = 0;
        payload["syncBossIntros"] = 0;
        payload["syncAge"] = 0;
        payload["syncCollision"] = 0;
        payload["syncAnimals"] = 0;
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
        payload["syncPlayerEffects"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncPlayerEffects"), 1);
        payload["syncTime"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncTime"), 1);
        payload["syncMusic"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncMusic"), 1);
        payload["objectUpdateRate"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.ObjectUpdateRate"), 20);
        payload["timeSyncSeconds"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.TimeSyncSeconds"), 5);
        payload["syncRealProjectiles"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncRealProjectiles"), 1);
        payload["syncPlatforms"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncPlatforms"), 1);
        payload["syncGameOver"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncGameOver"), 1);
        payload["syncTradeTimers"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncTradeTimers"), 1);
        payload["syncWeather"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncWeather"), 1);
        payload["syncSongTeaching"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncSongTeaching"), 1);
        payload["syncBossIntros"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncBossIntros"), 1);
        payload["syncAge"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncAge"), 1);
        payload["syncCollision"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncCollision"), 1);
        payload["syncAnimals"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("RoomSettings.SyncAnimals"), 1);
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
    roomState.syncPlayerEffects = payload["state"].value("syncPlayerEffects", (u8)0);
    roomState.syncTime = payload["state"].value("syncTime", (u8)0);
    roomState.syncMusic = payload["state"].value("syncMusic", (u8)0);
    roomState.objectUpdateRate = payload["state"].value("objectUpdateRate", (u8)20);
    roomState.timeSyncSeconds = payload["state"].value("timeSyncSeconds", (u8)5);
    roomState.syncRealProjectiles = payload["state"].value("syncRealProjectiles", (u8)0);
    roomState.syncPlatforms = payload["state"].value("syncPlatforms", (u8)0);
    roomState.syncGameOver = payload["state"].value("syncGameOver", (u8)0);
    roomState.syncTradeTimers = payload["state"].value("syncTradeTimers", (u8)0);
    roomState.syncWeather = payload["state"].value("syncWeather", (u8)0);
    roomState.syncSongTeaching = payload["state"].value("syncSongTeaching", (u8)0);
    roomState.syncBossIntros = payload["state"].value("syncBossIntros", (u8)0);
    roomState.syncAge = payload["state"].value("syncAge", (u8)0);
    roomState.syncCollision = payload["state"].value("syncCollision", (u8)0);
    roomState.syncAnimals = payload["state"].value("syncAnimals", (u8)0);
}
