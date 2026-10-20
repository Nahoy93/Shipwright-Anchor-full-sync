#pragma once

#ifdef __cplusplus

#include "soh/Network/Network.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/window/gui/GuiWindow.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <chrono>
#include <deque>
#include <queue>
#include <mutex>
#include <vector>
#include "soh/Network/Anchor/UdpChannel.h"

extern "C" {
#include "variables.h"
#include "z64.h"
}

void DummyPlayer_Init(Actor* actor, PlayState* play);
void DummyPlayer_Update(Actor* actor, PlayState* play);
void DummyPlayer_Draw(Actor* actor, PlayState* play);
void DummyPlayer_Destroy(Actor* actor, PlayState* play);

typedef struct {
    uint32_t clientId;
    std::string name;
    Color_RGB8 color;
    std::string clientVersion;
    std::string teamId;
    bool online;
    bool self;
    uint32_t seed;
    bool isSaveLoaded;
    bool isGameComplete;
    bool isDown; // Co-op: player has died and is waiting to be revived by a teammate
    s16 sceneNum;
    s8 curRoomNum;
    s32 entranceIndex;

    // Only available in PLAYER_UPDATE packets
    s32 linkAge;
    PosRot posRot;
    Vec3s jointTable[24];
    u8 movementFlags;
    Vec3s prevTransl;
    Vec3s upperLimbRot;
    s8 currentBoots;
    s8 currentShield;
    s8 currentTunic;
    u32 stateFlags1;
    u32 stateFlags2;
    u8 buttonItem0;
    s8 itemAction;
    s8 heldItemAction;
    u8 modelGroup;
    s8 invincibilityTimer;
    f32 unk_85C;
    s16 unk_862;
    s8 actionVar1;
    u8 ocarinaNote;
    f32 ocarinaModulator;
    s8 ocarinaBend;
    // Co-op player effects (PLAYER_UPDATE)
    u8 currentMask;
    s16 stickFlame;
    bool isBurning;
    bool isShocked;
    bool lensActive;
    // Epona under a riding teammate (PLAYER_UPDATE)
    bool horseActive;
    s16 horseParams;
    Vec3f horsePos;
    s16 horseRotY;
    std::vector<s16> horseJoints;

    // Ping and hearts, from the server every few seconds (PONG); -1 = unknown
    s32 pingMs = -1;
    s16 health = -1;
    s16 healthCapacity = -1;

    // Ptr to the dummy player
    Player* player;
} AnchorClient;

// A packet waiting to be sent. Realtime packets (positions, enemy snapshots, ...) may go over UDP.
struct AnchorOutgoingPacket {
    nlohmann::json json;
    bool realtime;
};

// A received packet, handed to the game at releaseAt (later than now only with the simulated latency)
struct AnchorIncomingPacket {
    nlohmann::json json;
    std::chrono::steady_clock::time_point releaseAt;
};

typedef struct {
    uint32_t ownerClientId;
    u8 pvpMode;           // 0 = off, 1 = on, 2 = on with friendly fire
    u8 showLocationsMode; // 0 = none, 1 = team, 2 = all
    u8 teleportMode;      // 0 = off, 1 = team, 2 = all
    u8 syncItemsAndFlags; // 0 = off, 1 = on
    // Co-op settings
    u8 shareRupees;       // 0 = off, 1 = rupees found by a teammate are added to everyone's own wallet
    u8 teamDeath;         // 0 = off (vanilla), 1 = downed until revived, game over only when the whole team is down
    u8 syncEnemies;       // 0 = off, 1 = enemies and bosses killed by a teammate die for everyone in the same area
    u8 syncCutscenes;     // 0 = off, 1 = teammates in the same area, 2 = all teammates (pulled to the cutscene)
    u8 syncEnemyMovement; // 0 = off, 1 = enemy positions and health follow one player per room (experimental)
    u8 playerUpdateRate;  // Player position/animation updates sent per second (1-20)
    u8 enemyUpdateRate;   // Enemy snapshots sent per second (1-20)
    u8 syncSongs;         // 0 = off, 1 = song effects (Sun's Song, Song of Storms, Song of Time) happen for teammates
    u8 syncWarpSongs;     // 0 = off, 1 = warp songs bring the whole team along
    u8 syncInteractions;  // 0 = off, 1 = projectiles, pots/rocks/grass, push blocks and torches are shared
    u8 syncPlayerEffects; // 0 = off, 1 = masks, burning/shock, burning sticks, Lens of Truth and hammer hits are shared
    u8 syncTime;          // 0 = off, 1 = the whole team has the same time of day
    u8 syncMusic;         // 0 = off, 1 = the music in an area follows the teammate with the lowest id there
    u8 objectUpdateRate;  // Projectile, object and drop updates sent per second (1-20)
    u8 timeSyncSeconds;   // How often the time of day is sent, in seconds (1-30)
    u8 syncRealProjectiles; // 0 = off, 1 = teammates' arrows and boomerangs fly and hit for real in your game
    u8 syncPlatforms;       // 0 = off, 1 = moving platforms and elevators follow the room leader
    u8 syncGameOver;        // 0 = off, 1 = the first answer in the game over menu counts for the whole team
    u8 syncTradeTimers;     // 0 = off, 1 = trade quest timers (Odd Mushroom, Frog, Eye Drops) are shared
    u8 syncWeather;         // 0 = off, 1 = rain, storms and dark skies follow the teammate with the lowest id
    u8 syncSongTeaching;    // 0 = off, 1 = songs a teammate learns are taught to everyone
    u8 syncBossIntros;      // 0 = off, 1 = the team is brought to the boss room when someone enters it
    u8 syncAge;             // 0 = off, 1 = changing age in the Temple of Time changes everyone's age
    u8 syncCollision;       // 0 = off, 1 = teammates' figures block you like a wall
    u8 syncAnimals;         // 0 = off, 1 = Cuccos and horses in the area follow the teammate with the lowest id
} RoomState;

class Anchor : public Network {
  private:
    uint32_t spawningDummyPlayerForClientId = 0;
    bool shouldRefreshActors = false;
    bool justLoadedSave = false;
    bool isHandlingUpdateTeamState = false;
    bool isProcessingIncomingPacket = false;
    std::deque<AnchorIncomingPacket> incomingPacketQueue;
    std::mutex incomingPacketQueueMutex;
    std::deque<AnchorOutgoingPacket> outgoingPacketQueue;
    std::mutex outgoingPacketQueueMutex;

    // Connection events from the network thread, handled on the game thread (see GameThreadPump)
    std::atomic<bool> pendingConnectEvent{ false };
    std::atomic<bool> pendingDisconnectEvent{ false };
    bool handshakeSent = false;
    uint32_t pumpHookId = 0;

    // What the server can do, from SERVER_INFO (network thread)
    std::atomic<bool> serverSupportsTargets{ false };
    std::atomic<bool> serverSupportsPing{ false };

    // UDP channel, network thread only
    AnchorUdp::Codec udpCodec;
    uint32_t udpToken = 0;
    uint64_t udpSendSequence = 0;
    uint64_t udpReceiveSequence = 0;
    bool udpReady = false;
    bool udpWanted = false;
    std::chrono::steady_clock::time_point udpLastHello;
    std::chrono::steady_clock::time_point udpLastReceive;
    std::chrono::steady_clock::time_point lastPingSent;

    // Copied from settings and game state by the game thread every frame, read by the network thread
    std::atomic<bool> settingUseUdp{ true };
    std::atomic<int> simLatencyMs{ 0 };
    std::atomic<int> simJitterMs{ 0 };
    std::atomic<int> simLossPercent{ 0 };
    std::atomic<int> statusHealth{ -1 };
    std::atomic<int> statusHealthCapacity{ -1 };
    std::atomic<bool> statusDown{ false };

    // TLS server trust: the fingerprint to expect (pinned in the menu or remembered from the first connection)
    std::mutex tlsMutex;
    std::string tlsHostKey;
    std::string tlsExpectedFingerprint;
    bool tlsExpectedIsPinned = false;
    std::string tlsLearnedFingerprint;
    std::string tlsError;
    std::string tlsWarning; // a failed handshake, shown once per Enable
    bool tlsWarned = false;

    uint32_t udpHellosUnanswered = 0;

    // Game thread
    void GameThreadPump();
    void UpdateNetworkSnapshot();
    void ProcessTlsEvents();
    void HandlePacket_Pong(nlohmann::json payload);
    // Network thread, see Transport.cpp
    void HandleServerInfo(const nlohmann::json& payload);
    void HandlePongTiming(const nlohmann::json& payload);
    void ResetTransportState();
    bool SendUdpPayload(const std::string& payload);
    void SendUdpHello();
    // Debug: drops or delays a received packet like a bad connection would. False = drop it.
    bool SimulateIncomingPacket(const std::string& packetType, std::chrono::steady_clock::time_point& releaseAt);
    // The certificate fingerprint remembered for "host:port", empty when unknown
    static std::string KnownTlsFingerprint(const std::string& hostKey);
    static void RememberTlsFingerprint(const std::string& hostKey, const std::string& fingerprint);

  protected:
    bool OnTlsCertificate(const std::string& fingerprint) override;
    void OnTlsFailed(const std::string& reason) override;
    void OnNetworkTick() override;
    void OnUdpDatagram(const uint8_t* data, size_t length) override;

  private:
    nlohmann::json PrepClientState();
    nlohmann::json PrepRoomState();
    void RegisterHooks();
    void RefreshClientActors();
    void SetDummyPlayerClientId(const Actor* actor, uint32_t clientId);

    void HandlePacket_AllClientState(nlohmann::json payload);
    void HandlePacket_ConsumeAdultTradeItem(nlohmann::json payload);
    void HandlePacket_DamagePlayer(nlohmann::json payload);
    void HandlePacket_DisableAnchor(nlohmann::json payload);
    void HandlePacket_EntranceDiscovered(nlohmann::json payload);
    void HandlePacket_GameComplete(nlohmann::json payload);
    void HandlePacket_GiveItem(nlohmann::json payload);
    void HandlePacket_OcarinaSfx(nlohmann::json payload);
    void HandlePacket_PlayerSfx(nlohmann::json payload);
    void HandlePacket_PlayerUpdate(nlohmann::json payload);
    void HandlePacket_RequestTeamState(nlohmann::json payload);
    void HandlePacket_RequestTeleport(nlohmann::json payload);
    void HandlePacket_ServerMessage(nlohmann::json payload);
    void HandlePacket_SetCheckStatus(nlohmann::json payload);
    void HandlePacket_SetFlag(nlohmann::json payload);
    void HandlePacket_TeleportTo(nlohmann::json payload);
    void HandlePacket_UnsetFlag(nlohmann::json payload);
    void HandlePacket_UpdateBeansCount(nlohmann::json payload);
    void HandlePacket_UpdateClientState(nlohmann::json payload);
    void HandlePacket_UpdateDungeonItems(nlohmann::json payload);
    void HandlePacket_UpdateRoomState(nlohmann::json payload);
    void HandlePacket_UpdateTeamState(nlohmann::json payload);
    void HandlePacket_BossDefeated(nlohmann::json payload);
    void HandlePacket_CutsceneStart(nlohmann::json payload);
    void HandlePacket_EnemyDefeated(nlohmann::json payload);
    void HandlePacket_PlayerDown(nlohmann::json payload);
    void HandlePacket_PlayerRevived(nlohmann::json payload);
    void HandlePacket_RupeeIncome(nlohmann::json payload);
    void HandlePacket_WarpSpawned(nlohmann::json payload);

    // Co-op state, see Coop.cpp
    void RegisterCoopHooks();
    void ResetCoopState();
    void Coop_OnGameFrame();
    void Coop_OnPlayerUpdate();
    void Coop_UpdateDownState(Player* player);
    void Coop_UpdateRupees();
    void Coop_UpdateCutsceneWatch();
    void Coop_ProcessPendingCutscene();
    bool Coop_IsTeammate(const AnchorClient& client);
    bool Coop_IsAnyTeammateAlive();
    bool Coop_CanStartRemoteCutscene();
    void Coop_StartScriptCutscene(const std::string& path);
    void Coop_KillBosses();
    void Coop_ProcessPendingBossKill();
    // Enemy movement/health sync, see EnemySync.cpp
    void Coop_UpdateEnemySync();
    void EnemySync_Reset();
    void EnemySync_OnActorUpdate(Actor* actor);
    uint32_t EnemySync_GetAuthority();
    bool EnemySync_GetNetId(Actor* actor, uint32_t* authority, uint32_t* netId);
    Actor* EnemySync_FindByNetId(uint32_t authority, uint32_t netId);
    void HandlePacket_EnemySync(nlohmann::json payload);
    void HandlePacket_EnemyHit(nlohmann::json payload);
    void SendPacket_EnemySync();
    // Converts an "updates per second" room setting to "send every N game updates" (the game runs 20 updates/sec)
    static uint32_t RateToInterval(u8 perSecond);
    // Ocarina songs, see OcarinaSong.cpp
    void Coop_UpdateSongWatch();
    void Coop_ProcessPendingWarp();
    void SendPacket_OcarinaSong(uint8_t song);
    void SendPacket_WarpSong();
    void HandlePacket_OcarinaSong(nlohmann::json payload);
    void HandlePacket_WarpSong(nlohmann::json payload);
    // World interactions, see Interactions.cpp
    void Coop_UpdateInteractions();
    bool Coop_AnyTeammateInScene();
    void Coop_SendToTeammatesInScene(nlohmann::json& payload);
    void Interactions_Reset();
    void Interactions_ShouldActorUpdate(Actor* actor, bool* should);
    void Interactions_OnActorKill(Actor* actor);
    void HandlePacket_Projectiles(nlohmann::json payload);
    void HandlePacket_WorldObjects(nlohmann::json payload);
    void HandlePacket_ObjectBroken(nlohmann::json payload);
    void HandlePacket_TorchLit(nlohmann::json payload);
    // Player effects and time of day, see PlayerEffects.cpp
    void Coop_UpdateLens();
    void Coop_UpdateTimeSync();
    void HandlePacket_TimeSync(nlohmann::json payload);
    void Coop_UpdateFaroresWind();
    void HandlePacket_FaroresWind(nlohmann::json payload);
    void Coop_UpdateMusic();
    void HandlePacket_Music(nlohmann::json payload);
    // More interactions
    bool Coop_IsLowestIdInScene();
    bool Interactions_ForceSilverCollect(Actor* actor);
    void HandlePacket_SilverCollected(nlohmann::json payload);
    // Drops, fairies, bugs and fish, see Drops.cpp
    void Drops_OnActorInit(Actor* actor);
    void Drops_OnActorKill(Actor* actor);
    void Coop_UpdateDrops();
    void HandlePacket_DropSpawn(nlohmann::json payload);
    void HandlePacket_DropMove(nlohmann::json payload);
    void HandlePacket_DropGone(nlohmann::json payload);
    // Batch 3
    void Coop_ApplyPendingRoomKills();
    void Coop_UpdateGameOver();
    void Coop_UpdateTradeTimer();
    void HandlePacket_GameOverChoice(nlohmann::json payload);
    void HandlePacket_TradeTimer(nlohmann::json payload);
    // Batch 4
    void Coop_UpdateWarpBlocks();
    void Coop_UpdateWeather();
    void HandlePacket_Weather(nlohmann::json payload);
    void Coop_UpdateSongLearning();
    void HandlePacket_SongLearned(nlohmann::json payload);
    void Coop_UpdateBossRoom();
    void HandlePacket_BossRoom(nlohmann::json payload);
    void Coop_UpdateSharedAge();
    void HandlePacket_AgeChanged(nlohmann::json payload);
    void Coop_UpdateSpectator();

  public:
    uint32_t ownClientId;
    inline static const std::string clientVersion = (char*)gGitCommitHash;

    // Connection status for the room window and the debug view
    std::atomic<int> rttMs{ -1 };   // round trip to the server, measured with PING/PONG
    std::atomic<int> udpState{ 0 }; // 0 = not used, 1 = trying, 2 = in use
    std::atomic<size_t> outgoingQueueLength{ 0 };
    std::atomic<size_t> incomingQueueLength{ 0 };

    // Sends one packet to several clients: once with a list of targets when the server can fan it out, otherwise once
    // per client
    void SendToClients(nlohmann::json& payload, const std::vector<uint32_t>& clientIds);
    // Forgets remembered server certificates (menu)
    static void ForgetTlsCertificates();
    // Ping, traffic, queues and UDP/TLS state for the room window, see AnchorRoomWindow.cpp
    void DrawNetworkDetails();
    // Roughly how old a packet from this client is when the game uses it: half of their round trip to the server,
    // half of ours, the wait for the next game update and the simulated latency
    int EstimatedPacketAgeMs(uint32_t senderClientId);

    // Packet types //
    inline static const std::string ALL_CLIENT_STATE = "ALL_CLIENT_STATE";
    inline static const std::string DAMAGE_PLAYER = "DAMAGE_PLAYER";
    inline static const std::string DISABLE_ANCHOR = "DISABLE_ANCHOR";
    inline static const std::string ENTRANCE_DISCOVERED = "ENTRANCE_DISCOVERED";
    inline static const std::string GAME_COMPLETE = "GAME_COMPLETE";
    inline static const std::string GIVE_ITEM = "GIVE_ITEM";
    inline static const std::string HANDSHAKE = "HANDSHAKE";
    inline static const std::string OCARINA_SFX = "OCARINA_SFX";
    inline static const std::string PLAYER_SFX = "PLAYER_SFX";
    inline static const std::string PLAYER_UPDATE = "PLAYER_UPDATE";
    inline static const std::string REQUEST_TEAM_STATE = "REQUEST_TEAM_STATE";
    inline static const std::string REQUEST_TELEPORT = "REQUEST_TELEPORT";
    inline static const std::string SERVER_MESSAGE = "SERVER_MESSAGE";
    inline static const std::string SET_CHECK_STATUS = "SET_CHECK_STATUS";
    inline static const std::string SET_FLAG = "SET_FLAG";
    inline static const std::string TELEPORT_TO = "TELEPORT_TO";
    inline static const std::string UNSET_FLAG = "UNSET_FLAG";
    inline static const std::string UPDATE_BEANS_COUNT = "UPDATE_BEANS_COUNT";
    inline static const std::string UPDATE_CLIENT_STATE = "UPDATE_CLIENT_STATE";
    inline static const std::string UPDATE_DUNGEON_ITEMS = "UPDATE_DUNGEON_ITEMS";
    inline static const std::string UPDATE_ROOM_STATE = "UPDATE_ROOM_STATE";
    inline static const std::string UPDATE_TEAM_STATE = "UPDATE_TEAM_STATE";
    // Co-op packet types
    inline static const std::string BOSS_DEFEATED = "BOSS_DEFEATED";
    inline static const std::string CUTSCENE_START = "CUTSCENE_START";
    inline static const std::string ENEMY_DEFEATED = "ENEMY_DEFEATED";
    inline static const std::string ENEMY_HIT = "ENEMY_HIT";
    inline static const std::string ENEMY_SYNC = "ENEMY_SYNC";
    inline static const std::string OBJECT_BROKEN = "OBJECT_BROKEN";
    inline static const std::string OCARINA_SONG = "OCARINA_SONG";
    inline static const std::string PROJECTILES = "PROJECTILES";
    inline static const std::string TORCH_LIT = "TORCH_LIT";
    inline static const std::string TIME_SYNC = "TIME_SYNC";
    inline static const std::string FARORES_WIND = "FARORES_WIND";
    inline static const std::string MUSIC = "MUSIC";
    inline static const std::string SILVER_COLLECTED = "SILVER_COLLECTED";
    inline static const std::string DROP_SPAWN = "DROP_SPAWN";
    inline static const std::string DROP_MOVE = "DROP_MOVE";
    inline static const std::string DROP_GONE = "DROP_GONE";
    inline static const std::string WARP_SONG = "WARP_SONG";
    inline static const std::string WORLD_OBJECTS = "WORLD_OBJECTS";
    inline static const std::string PLAYER_DOWN = "PLAYER_DOWN";
    inline static const std::string PLAYER_REVIVED = "PLAYER_REVIVED";
    inline static const std::string RUPEE_INCOME = "RUPEE_INCOME";
    inline static const std::string WARP_SPAWNED = "WARP_SPAWNED";
    inline static const std::string GAME_OVER_CHOICE = "GAME_OVER_CHOICE";
    inline static const std::string TRADE_TIMER = "TRADE_TIMER";
    inline static const std::string WEATHER = "WEATHER";
    inline static const std::string SONG_LEARNED = "SONG_LEARNED";
    inline static const std::string BOSS_ROOM = "BOSS_ROOM";
    inline static const std::string AGE_CHANGED = "AGE_CHANGED";
    // Transport
    inline static const std::string SERVER_INFO = "SERVER_INFO";
    inline static const std::string PING = "PING";
    inline static const std::string PONG = "PONG";
    inline static const std::string UDP_HELLO = "UDP_HELLO";
    inline static const std::string UDP_ACK = "UDP_ACK";

    // Co-op runtime state (game thread only)
    struct CoopState {
        // Team death
        bool isDown = false;
        bool allowGameOver = false;
        bool isReviving = false;
        bool reviveHealthApplied = false;
        uint16_t reviveTimer = 0;
        uint32_t reviverClientId = 0;
        // Shared rupee income
        bool rupeesInitialized = false;
        s16 lastRupees = 0;
        s32 pendingRemoteRupees = 0;
        s32 outgoingRupees = 0;
        uint16_t rupeeFlushTimer = 0;
        // Cutscene sync
        s16 lastCsState = 0;
        bool isPlayingRemoteCutscene = false;
        uint16_t remoteCutsceneGrace = 0;
        std::string remoteCutscenePath;
        uint32_t framesSinceLoad = 0;
        uint32_t framesInScene = 0;
        std::map<std::string, uint32_t> recentCutscenes; // key -> frame it was last played/sent
        uint32_t frameCounter = 0;
        // A cutscene we were asked to show but could not start yet
        bool hasPendingCutscene = false;
        nlohmann::json pendingCutscene;
        uint32_t pendingCutsceneExpires = 0;
        bool pendingTeleportDone = false;
        // A boss kill that has to wait until we're out of a cutscene (boss intro)
        s16 pendingBossKillScene = -1;
    } coop;

    static Anchor* Instance;
    std::map<uint32_t, AnchorClient> clients;
    RoomState roomState;

    void Enable();
    void Disable();
    void OnIncomingJson(nlohmann::json payload);
    void OnConnected();
    void OnDisconnected();
    void ProcessOutgoingPackets();
    void DrawMenu();
    void ProcessIncomingPacketQueue();
    void SendJsonToRemote(nlohmann::json packet);
    bool IsSaveLoaded();
    bool CanTeleportTo(uint32_t clientId);
    uint32_t GetDummyPlayerClientId(const Actor* actor);
    bool Interactions_IsHorseMirror(Actor* actor);

    void SendPacket_ClearTeamState(std::string teamId);
    void SendPacket_DamagePlayer(u32 clientId, u8 damageEffect, u8 damage);
    void SendPacket_EntranceDiscovered(u16 entranceIndex);
    void SendPacket_GameComplete();
    void SendPacket_GiveItem(u16 modId, s16 getItemId);
    void SendPacket_Handshake();
    void SendPacket_OcarinaSfx(uint8_t note, float modulator, int8_t bend);
    void SendPacket_PlayerSfx(u16 sfxId);
    void SendPacket_PlayerUpdate();
    void SendPacket_RequestTeamState();
    void SendPacket_RequestTeleport(u32 clientId);
    void SendPacket_SetCheckStatus(RandomizerCheck rc);
    void SendPacket_SetFlag(s16 sceneNum, s16 flagType, s16 flag);
    void SendPacket_TeleportTo(u32 clientId);
    void SendPacket_UnsetFlag(s16 sceneNum, s16 flagType, s16 flag);
    void SendPacket_UpdateBeansCount();
    void SendPacket_UpdateClientState();
    void SendPacket_UpdateDungeonItems();
    void SendPacket_UpdateRoomState();
    void SendPacket_UpdateTeamState();
    void SendPacket_BossDefeated(Actor* actor);
    void SendPacket_CutsceneStart();
    void SendPacket_EnemyDefeated(Actor* actor);
    void SendPacket_PlayerDown();
    void SendPacket_PlayerRevived(uint32_t reviverClientId);
    void SendPacket_RupeeIncome(s32 amount);
    void SendPacket_WarpSpawned(Actor* actor);
};

// Packets where only the newest one matters (positions, snapshots, sounds): they may go over UDP, and a lost one is
// replaced by the next
bool Anchor_IsRealtimePacket(const std::string& packetType);

typedef enum {
    // Starting at 5 to continue from the last value in the PlayerDamageResponseType enum
    DUMMY_PLAYER_HIT_RESPONSE_STUN = 5,
    DUMMY_PLAYER_HIT_RESPONSE_FIRE,
    DUMMY_PLAYER_HIT_RESPONSE_NORMAL,
} DummyPlayerDamageResponseType;

// Draws effects on a teammate's figure (burning, shock, burning stick), see PlayerEffects.cpp
void Anchor_DummyPlayerEffects(Player* player, AnchorClient& client);
// Shows a teammate's horse under their figure while they ride, see Interactions.cpp
void Anchor_DummyHorse(Player* player, AnchorClient& client);
// Spawns a visual effect that runs and draws around a teammate's figure (ocarina song waves), see Interactions.cpp
Actor* Anchor_SpawnEffectMirror(s16 actorId, Vec3f pos, s16 params, uint32_t owner);

// Saved sessions (connection presets), see Sessions.cpp
void AnchorSessionsMenu(bool isEnabled);
void AnchorSessions_RecordSaveFile(int32_t fileNum);

class AnchorRoomWindow : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

    void InitElement() override{};
    void DrawElement() override;
    void Draw() override;
    void UpdateElement() override{};
};

#endif // __cplusplus
