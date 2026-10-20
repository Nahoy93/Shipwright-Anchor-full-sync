#include <nlohmann/json.hpp>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include "Anchor.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/nametag.h"
#include "soh/ObjectExtension/ObjectExtension.h"

extern "C" {
#include "macros.h"
#include "variables.h"
#include "functions.h"
extern PlayState* gPlayState;
}

// MARK: - Overrides

/**
 * Threads
 *
 * The network thread (Network.cpp) connects, receives and sends. Everything that touches the game (packet handlers,
 * hooks, the client list) runs on the game thread, in GameThreadPump at the start of every game update:
 *  - connection events from the network thread are only flags here; the handshake, hook registration and the team
 *    state request happen in the pump
 *  - received packets wait in a queue and are handled in the pump, before the actors update, so they're used in the
 *    same update instead of the next one
 */

void Anchor::Enable() {
    if (isEnabled) {
        return;
    }

    std::string host = CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
    uint16_t port = (uint16_t)CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
    ownClientId = CVarGetInteger(CVAR_REMOTE_ANCHOR("LastClientId"), 0);
    roomState.ownerClientId = 0;
    handshakeSent = false;
    pendingConnectEvent = false;
    pendingDisconnectEvent = false;

    useTls = CVarGetInteger(CVAR_REMOTE_ANCHOR("UseTls"), 0) != 0;
    {
        std::lock_guard<std::mutex> lock(tlsMutex);
        tlsHostKey = host + ":" + std::to_string(port);
        std::string pinned = CVarGetString(CVAR_REMOTE_ANCHOR("TlsFingerprint"), "");
        tlsExpectedIsPinned = !pinned.empty();
        tlsExpectedFingerprint = tlsExpectedIsPinned ? pinned : KnownTlsFingerprint(tlsHostKey);
        tlsLearnedFingerprint.clear();
        tlsError.clear();
        tlsWarning.clear();
        tlsWarned = false;
    }

    UpdateNetworkSnapshot();

    if (pumpHookId == 0) {
        pumpHookId = GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameStateMainStart>(
            [this]() { GameThreadPump(); });
    }

    Network::Enable(host.c_str(), port);
}

void Anchor::Disable() {
    Network::Disable();

    if (pumpHookId != 0) {
        GameInteractor::Instance->UnregisterGameHook<GameInteractor::OnGameStateMainStart>(pumpHookId);
        pumpHookId = 0;
    }
    pendingConnectEvent = false;
    pendingDisconnectEvent = false;
    handshakeSent = false;
    // Not connected any more, so this removes the connection's hooks
    RegisterHooks();

    {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        incomingPacketQueue.clear();
        incomingQueueLength = 0;
    }
    {
        std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
        outgoingPacketQueue.clear();
        outgoingQueueLength = 0;
    }

    clients.clear();
    ResetCoopState();
    RefreshClientActors();
}

// Network thread
void Anchor::OnConnected() {
    ResetTransportState();

    // Nothing queued for the old connection may go out before this connection's handshake. Under the queue lock, so
    // SendJsonToRemote either queues before the clear or sees the pending event and waits for the handshake.
    std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
    pendingConnectEvent = true;
    outgoingPacketQueue.clear();
    outgoingQueueLength = 0;
}

// Network thread
void Anchor::OnDisconnected() {
    ResetTransportState();
    pendingDisconnectEvent = true;
}

// Game thread, at the start of every game update
void Anchor::GameThreadPump() {
    if (pendingDisconnectEvent.exchange(false)) {
        handshakeSent = false;
        RegisterHooks();
    }

    if (pendingConnectEvent.exchange(false) && isConnected) {
        // The handshake must be the first packet the server gets
        handshakeSent = false;
        SendPacket_Handshake();
        handshakeSent = true;
        RegisterHooks();

        if (IsSaveLoaded()) {
            SendPacket_RequestTeamState();
        }
    }

    UpdateNetworkSnapshot();
    ProcessTlsEvents();
    ProcessIncomingPacketQueue();
}

// Game thread: settings and game state the network thread needs
void Anchor::UpdateNetworkSnapshot() {
    settingUseUdp = CVarGetInteger(CVAR_REMOTE_ANCHOR("UseUdp"), 1) != 0;
    simLatencyMs = CVarGetInteger(CVAR_REMOTE_ANCHOR("Debug.SimLatency"), 0);
    simJitterMs = CVarGetInteger(CVAR_REMOTE_ANCHOR("Debug.SimJitter"), 0);
    simLossPercent = CVarGetInteger(CVAR_REMOTE_ANCHOR("Debug.SimLoss"), 0);

    if (IsSaveLoaded()) {
        statusHealth = gSaveContext.health;
        statusHealthCapacity = gSaveContext.healthCapacity;
    } else {
        statusHealth = -1;
        statusHealthCapacity = -1;
    }
    statusDown = coop.isDown;
}

// Network thread
void Anchor::ProcessOutgoingPackets() {
    // Copy all queued packets while holding the lock, then send them after releasing
    std::deque<AnchorOutgoingPacket> packetsToSend;
    {
        std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
        packetsToSend.swap(outgoingPacketQueue);
        outgoingQueueLength = 0;
    }

    // Everything that goes over TCP is written in one go
    std::string batch;
    for (auto& packet : packetsToSend) {
        std::string text = packet.json.dump();
        if (!packet.json.contains("quiet")) {
            SPDLOG_DEBUG("[Anchor] Sending payload:\n{}", text);
        }
        packetsSent++;

        if (packet.realtime && SendUdpPayload(text)) {
            continue;
        }

        batch.append(text);
        batch.push_back('\0');
        if (batch.size() >= 64 * 1024) {
            SendRawToRemote(batch.data(), batch.size());
            batch.clear();
        }
    }

    if (!batch.empty()) {
        SendRawToRemote(batch.data(), batch.size());
    }
}

// Game thread
void Anchor::SendJsonToRemote(nlohmann::json payload) {
    if (!isConnected) {
        return;
    }

    std::string packetType =
        payload.contains("type") && payload["type"].is_string() ? payload["type"].get<std::string>() : "";
    bool isHandshake = packetType == HANDSHAKE;

    payload["clientId"] = ownClientId;
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Queuing payload:\n{}", payload.dump());
    }

    bool realtime = Anchor_IsRealtimePacket(packetType);

    // Queue the packet to be sent on the network thread
    std::lock_guard<std::mutex> lock(outgoingPacketQueueMutex);
    // Until this connection's handshake is queued, the server would drop anything else anyway
    if (!isHandshake && (!handshakeSent || pendingConnectEvent)) {
        return;
    }
    outgoingPacketQueue.push_back({ std::move(payload), realtime });
    outgoingQueueLength = outgoingPacketQueue.size();
}

void Anchor::SendToClients(nlohmann::json& payload, const std::vector<uint32_t>& clientIds) {
    if (clientIds.empty()) {
        return;
    }

    // One packet with a list of targets, the server makes the copies
    if (serverSupportsTargets && clientIds.size() > 1) {
        payload.erase("targetClientId");
        payload["targetClientIds"] = clientIds;
        SendJsonToRemote(payload);
        return;
    }

    for (uint32_t clientId : clientIds) {
        payload["targetClientId"] = clientId;
        SendJsonToRemote(payload);
    }
}

// Network thread
void Anchor::OnIncomingJson(nlohmann::json payload) {
    // If it doesn't contain a type, it's not a valid payload
    if (!payload.contains("type") || !payload["type"].is_string()) {
        return;
    }

    std::string packetType = payload["type"].get<std::string>();

    // Transport packets are handled right here on the network thread
    if (packetType == SERVER_INFO) {
        HandleServerInfo(payload);
        return;
    }
    if (packetType == PONG) {
        HandlePongTiming(payload);
    }
    if (packetType == "HEARTBEAT") {
        return;
    }

    // If it's not a quiet payload, log it
    if (!payload.contains("quiet")) {
        SPDLOG_DEBUG("[Anchor] Received payload:\n{}", payload.dump());
    }

    // Debug: simulated network problems
    auto releaseAt = std::chrono::steady_clock::now();
    if (!SimulateIncomingPacket(packetType, releaseAt)) {
        return;
    }

    // Queue all packets to be processed on the game thread
    std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
    incomingPacketQueue.push_back({ std::move(payload), releaseAt });
    incomingQueueLength = incomingPacketQueue.size();
}

// Game thread
void Anchor::ProcessIncomingPacketQueue() {
    // Take the packets that are due while holding the lock, then process them after releasing
    std::vector<AnchorIncomingPacket> packetsToProcess;
    {
        std::lock_guard<std::mutex> lock(incomingPacketQueueMutex);
        auto now = std::chrono::steady_clock::now();
        while (!incomingPacketQueue.empty() && incomingPacketQueue.front().releaseAt <= now) {
            packetsToProcess.push_back(std::move(incomingPacketQueue.front()));
            incomingPacketQueue.pop_front();
        }
        incomingQueueLength = incomingPacketQueue.size();
    }

    // Process packets without holding the lock
    for (auto& packet : packetsToProcess) {
        // A packet (DISABLE_ANCHOR) or a refused server may have turned Anchor off in the meantime
        if (!isEnabled) {
            break;
        }

        nlohmann::json& payload = packet.json;
        std::string packetType = payload["type"].get<std::string>();

        // Ignore packets from mismatched clients, except for ALL_CLIENT_STATE, UPDATE_CLIENT_STATE, and PLAYER_UPDATE
        if (packetType != ALL_CLIENT_STATE && packetType != UPDATE_CLIENT_STATE && packetType != PLAYER_UPDATE &&
            payload.contains("clientId") && payload["clientId"].is_number()) {
            auto it = clients.find(payload["clientId"].get<uint32_t>());
            if (it != clients.end() && it->second.clientVersion != clientVersion) {
                continue;
            }
        }

        isProcessingIncomingPacket = true;

        try {
            // packetType here is a string so we can't use a switch statement
            if (packetType == ALL_CLIENT_STATE)
                HandlePacket_AllClientState(payload);
            else if (packetType == DAMAGE_PLAYER)
                HandlePacket_DamagePlayer(payload);
            else if (packetType == DISABLE_ANCHOR)
                HandlePacket_DisableAnchor(payload);
            else if (packetType == ENTRANCE_DISCOVERED)
                HandlePacket_EntranceDiscovered(payload);
            else if (packetType == GAME_COMPLETE)
                HandlePacket_GameComplete(payload);
            else if (packetType == GIVE_ITEM)
                HandlePacket_GiveItem(payload);
            else if (packetType == OCARINA_SFX)
                HandlePacket_OcarinaSfx(payload);
            else if (packetType == PLAYER_UPDATE)
                HandlePacket_PlayerUpdate(payload);
            else if (packetType == PLAYER_SFX)
                HandlePacket_PlayerSfx(payload);
            else if (packetType == UPDATE_TEAM_STATE)
                HandlePacket_UpdateTeamState(payload);
            else if (packetType == REQUEST_TEAM_STATE)
                HandlePacket_RequestTeamState(payload);
            else if (packetType == REQUEST_TELEPORT)
                HandlePacket_RequestTeleport(payload);
            else if (packetType == SERVER_MESSAGE)
                HandlePacket_ServerMessage(payload);
            else if (packetType == SET_CHECK_STATUS)
                HandlePacket_SetCheckStatus(payload);
            else if (packetType == SET_FLAG)
                HandlePacket_SetFlag(payload);
            else if (packetType == TELEPORT_TO)
                HandlePacket_TeleportTo(payload);
            else if (packetType == UNSET_FLAG)
                HandlePacket_UnsetFlag(payload);
            else if (packetType == UPDATE_BEANS_COUNT)
                HandlePacket_UpdateBeansCount(payload);
            else if (packetType == UPDATE_CLIENT_STATE)
                HandlePacket_UpdateClientState(payload);
            else if (packetType == UPDATE_ROOM_STATE)
                HandlePacket_UpdateRoomState(payload);
            else if (packetType == UPDATE_DUNGEON_ITEMS)
                HandlePacket_UpdateDungeonItems(payload);
            // Co-op
            else if (packetType == BOSS_DEFEATED)
                HandlePacket_BossDefeated(payload);
            else if (packetType == CUTSCENE_START)
                HandlePacket_CutsceneStart(payload);
            else if (packetType == ENEMY_DEFEATED)
                HandlePacket_EnemyDefeated(payload);
            else if (packetType == ENEMY_SYNC)
                HandlePacket_EnemySync(payload);
            else if (packetType == ENEMY_HIT)
                HandlePacket_EnemyHit(payload);
            else if (packetType == OCARINA_SONG)
                HandlePacket_OcarinaSong(payload);
            else if (packetType == WARP_SONG)
                HandlePacket_WarpSong(payload);
            else if (packetType == PROJECTILES)
                HandlePacket_Projectiles(payload);
            else if (packetType == WORLD_OBJECTS)
                HandlePacket_WorldObjects(payload);
            else if (packetType == OBJECT_BROKEN)
                HandlePacket_ObjectBroken(payload);
            else if (packetType == TORCH_LIT)
                HandlePacket_TorchLit(payload);
            else if (packetType == TIME_SYNC)
                HandlePacket_TimeSync(payload);
            else if (packetType == FARORES_WIND)
                HandlePacket_FaroresWind(payload);
            else if (packetType == MUSIC)
                HandlePacket_Music(payload);
            else if (packetType == SILVER_COLLECTED)
                HandlePacket_SilverCollected(payload);
            else if (packetType == DROP_SPAWN)
                HandlePacket_DropSpawn(payload);
            else if (packetType == DROP_MOVE)
                HandlePacket_DropMove(payload);
            else if (packetType == DROP_GONE)
                HandlePacket_DropGone(payload);
            else if (packetType == GAME_OVER_CHOICE)
                HandlePacket_GameOverChoice(payload);
            else if (packetType == TRADE_TIMER)
                HandlePacket_TradeTimer(payload);
            else if (packetType == WEATHER)
                HandlePacket_Weather(payload);
            else if (packetType == SONG_LEARNED)
                HandlePacket_SongLearned(payload);
            else if (packetType == BOSS_ROOM)
                HandlePacket_BossRoom(payload);
            else if (packetType == AGE_CHANGED)
                HandlePacket_AgeChanged(payload);
            else if (packetType == PLAYER_DOWN)
                HandlePacket_PlayerDown(payload);
            else if (packetType == PLAYER_REVIVED)
                HandlePacket_PlayerRevived(payload);
            else if (packetType == RUPEE_INCOME)
                HandlePacket_RupeeIncome(payload);
            else if (packetType == WARP_SPAWNED)
                HandlePacket_WarpSpawned(payload);
            else if (packetType == PONG)
                HandlePacket_Pong(payload);
        } catch (const std::exception& e) {
            SPDLOG_ERROR("[Anchor] Exception while processing incoming packet {}", e.what());
            SPDLOG_ERROR("[Anchor] Packet: {}", payload.dump());
        }

        isProcessingIncomingPacket = false;
    }
}

// MARK: - Misc/Helpers

// Kills all existing anchor actors and respawns them with the new client data

struct DummyPlayerClientId {
    uint32_t clientId = 0;
};
static ObjectExtension::Register<DummyPlayerClientId> DummyPlayerClientIdRegister;

uint32_t Anchor::GetDummyPlayerClientId(const Actor* actor) {
    const DummyPlayerClientId* clientId = ObjectExtension::GetInstance().Get<DummyPlayerClientId>(actor);
    return clientId != nullptr ? clientId->clientId : 0;
}

void Anchor::SetDummyPlayerClientId(const Actor* actor, uint32_t clientId) {
    ObjectExtension::GetInstance().Set<DummyPlayerClientId>(actor, DummyPlayerClientId{ clientId });
}

void Anchor::RefreshClientActors() {
    if (!IsSaveLoaded()) {
        return;
    }

    // The old figures are killed below; nobody may keep pointing at them, including offline clients that don't get a
    // new figure
    for (auto& [clientId, client] : clients) {
        client.player = nullptr;
    }

    Actor* actor = gPlayState->actorCtx.actorLists[ACTORCAT_NPC].head;

    while (actor != NULL) {
        if (actor->id == ACTOR_EN_OE2 && actor->update == DummyPlayer_Update) {
            NameTag_RemoveAllForActor(actor);
            Actor_Kill(actor);
        }
        actor = actor->next;
    }

    for (auto& [clientId, client] : clients) {
        if (!client.online || client.self) {
            continue;
        }

        spawningDummyPlayerForClientId = clientId;
        // We are using a hook `ShouldActorInit` to override the init/update/draw/destroy functions of the Player we
        // spawn We quickly store a mapping of "index" to clientId, then within the init function we use this to get the
        // clientId and store it on player->zTargetActiveTimer (unused s32 for the dummy) for convenience
        auto dummy =
            Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_PLAYER, client.posRot.pos.x, client.posRot.pos.y,
                        client.posRot.pos.z, client.posRot.rot.x, client.posRot.rot.y, client.posRot.rot.z, 0);
        client.player = (Player*)dummy;
    }
    spawningDummyPlayerForClientId = 0;
}

bool Anchor::IsSaveLoaded() {
    if (gPlayState == nullptr) {
        return false;
    }

    if (GET_PLAYER(gPlayState) == nullptr) {
        return false;
    }

    if (gSaveContext.fileNum < 0 || gSaveContext.fileNum > 2) {
        return false;
    }

    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        return false;
    }

    return true;
}
