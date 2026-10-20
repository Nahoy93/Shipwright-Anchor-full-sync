#include "soh/Network/Anchor/Anchor.h"
#include "soh/Network/Anchor/JsonConversions.hpp"
#include <nlohmann/json.hpp>
#include <vector>

extern "C" {
#include "macros.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Horse/z_en_horse.h"
extern PlayState* gPlayState;
}

/**
 * PLAYER_UPDATE
 *
 * Contains real-time data necessary to update other clients in the same scene as the player
 *
 * Sent every frame to other clients within the same scene
 *
 * Note: This packet is sent _a lot_, so please do not include any unnecessary data in it
 */

void Anchor::SendPacket_PlayerUpdate() {
    if (!IsSaveLoaded()) {
        return;
    }

    uint32_t currentPlayerCount = 0;
    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            currentPlayerCount++;
        }
    }
    if (currentPlayerCount == 0) {
        return;
    }

    // Room setting: how many updates per second to send
    static uint32_t framesSinceLastUpdate = 0;
    if (++framesSinceLastUpdate < RateToInterval(roomState.playerUpdateRate)) {
        return;
    }
    framesSinceLastUpdate = 0;

    Player* player = GET_PLAYER(gPlayState);
    nlohmann::json payload;

    payload["type"] = PLAYER_UPDATE;
    payload["sceneNum"] = gPlayState->sceneNum;
    payload["roomNum"] = gPlayState->roomCtx.curRoom.num;
    payload["entranceIndex"] = gSaveContext.entranceIndex;
    payload["linkAge"] = gSaveContext.linkAge;
    payload["posRot"]["pos"] = player->actor.world.pos;
    payload["posRot"]["rot"] = player->actor.shape.rot;
    std::vector<int> jointArray;
    for (size_t i = 0; i < 24; i++) {
        Vec3s joint = player->skelAnime.jointTable[i];
        jointArray.push_back(joint.x);
        jointArray.push_back(joint.y);
        jointArray.push_back(joint.z);
    }
    payload["prevTransl"] = player->skelAnime.prevTransl;
    payload["movementFlags"] = player->skelAnime.movementFlags;
    payload["jointTable"] = jointArray;
    payload["upperLimbRot"] = player->upperLimbRot;
    payload["currentBoots"] = player->currentBoots;
    payload["currentShield"] = player->currentShield;
    payload["currentTunic"] = player->currentTunic;
    payload["stateFlags1"] = player->stateFlags1;
    payload["stateFlags2"] = player->stateFlags2 & ~PLAYER_STATE2_DISABLE_DRAW;
    payload["buttonItem0"] = gSaveContext.equips.buttonItems[0];
    payload["itemAction"] = player->itemAction;
    payload["heldItemAction"] = player->heldItemAction;
    payload["modelGroup"] = player->modelGroup;
    payload["invincibilityTimer"] = player->invincibilityTimer;
    payload["unk_862"] = player->unk_862;
    payload["unk_85C"] = player->unk_85C;
    payload["actionVar1"] = player->av1.actionVar1;
    payload["currentMask"] = player->currentMask;
    payload["stickFlame"] = player->heldItemAction == PLAYER_IA_DEKU_STICK ? player->unk_860 : 0;
    payload["isBurning"] = (bool)player->bodyIsBurning;
    payload["isShocked"] = player->bodyShockTimer != 0;
    payload["lensActive"] = gPlayState->actorCtx.lensActive && gSaveContext.magicState == MAGIC_STATE_CONSUME_LENS;
    // Epona, so teammates see the horse under the figure
    if ((player->stateFlags1 & PLAYER_STATE1_ON_HORSE) && player->rideActor != nullptr &&
        player->rideActor->id == ACTOR_EN_HORSE) {
        EnHorse* horse = (EnHorse*)player->rideActor;
        SkelAnime* skelAnime = &horse->skin.skelAnime;
        std::vector<s16> horseJoints;
        for (s32 i = 0; i < skelAnime->limbCount && skelAnime->jointTable != nullptr; i++) {
            horseJoints.push_back(skelAnime->jointTable[i].x);
            horseJoints.push_back(skelAnime->jointTable[i].y);
            horseJoints.push_back(skelAnime->jointTable[i].z);
        }
        payload["horse"]["params"] = horse->actor.params;
        payload["horse"]["pos"] = horse->actor.world.pos;
        payload["horse"]["rotY"] = horse->actor.shape.rot.y;
        payload["horse"]["joints"] = horseJoints;
    }
    payload["quiet"] = true;

    // One packet for everyone in the scene, the server makes the copies (or one packet each on older servers)
    std::vector<uint32_t> targets;
    for (auto& [clientId, client] : clients) {
        if (client.sceneNum == gPlayState->sceneNum && client.online && client.isSaveLoaded && !client.self) {
            targets.push_back(clientId);
        }
    }
    SendToClients(payload, targets);
}

void Anchor::HandlePacket_PlayerUpdate(nlohmann::json payload) {
    uint32_t clientId = payload["clientId"].get<uint32_t>();

    if (clients.contains(clientId)) {
        auto& client = clients[clientId];

        if (client.linkAge != payload.value("linkAge", (s32)LINK_AGE_ADULT)) {
            shouldRefreshActors = true;
        }

        client.sceneNum = payload.value("sceneNum", (s16)SCENE_ID_MAX);
        client.curRoomNum = payload.value("roomNum", client.curRoomNum);
        client.entranceIndex = payload.value("entranceIndex", (s32)0);
        client.linkAge = payload.value("linkAge", (s32)LINK_AGE_ADULT);
        client.posRot = payload.value("posRot", PosRot{ 0 });
        std::vector<int> jointArray = payload.value("jointTable", std::vector<int>{});
        jointArray.resize(24 * 3); // Ensure it has enough elements, in case of missing data
        for (int i = 0; i < 24; i++) {
            client.jointTable[i].x = jointArray[i * 3];
            client.jointTable[i].y = jointArray[i * 3 + 1];
            client.jointTable[i].z = jointArray[i * 3 + 2];
        }
        client.movementFlags = payload.value("movementFlags", (u8)0);
        client.prevTransl = payload.value("prevTransl", Vec3s{ 0 });
        client.upperLimbRot = payload.value("upperLimbRot", Vec3s{ 0 });
        client.currentBoots = payload.value("currentBoots", (s8)0);
        client.currentShield = payload.value("currentShield", (s8)0);
        client.currentTunic = payload.value("currentTunic", (s8)0);
        client.stateFlags1 = payload.value("stateFlags1", (u32)0);
        client.stateFlags2 = payload.value("stateFlags2", (u32)0);
        client.buttonItem0 = payload.value("buttonItem0", (u8)0);
        client.itemAction = payload.value("itemAction", (s8)0);
        client.heldItemAction = payload.value("heldItemAction", (s8)0);
        client.modelGroup = payload.value("modelGroup", (u8)0);
        client.invincibilityTimer = payload.value("invincibilityTimer", (s8)0);
        client.unk_862 = payload.value("unk_862", (s16)0);
        client.unk_85C = payload.value("unk_85C", (f32)0);
        client.actionVar1 = payload.value("actionVar1", (s8)0);
        client.currentMask = payload.value("currentMask", (u8)0);
        client.stickFlame = payload.value("stickFlame", (s16)0);
        client.isBurning = payload.value("isBurning", false);
        client.isShocked = payload.value("isShocked", false);
        client.lensActive = payload.value("lensActive", false);
        client.horseActive = payload.contains("horse");
        if (client.horseActive) {
            auto& horse = payload["horse"];
            client.horseParams = horse.value("params", (s16)0);
            client.horsePos = horse.value("pos", Vec3f{ 0 });
            client.horseRotY = horse.value("rotY", (s16)0);
            client.horseJoints = horse.value("joints", std::vector<s16>{});
        }
    }
}
