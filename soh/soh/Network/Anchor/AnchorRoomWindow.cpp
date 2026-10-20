#include <ship/window/gui/IconsFontAwesome4.h>
#include <chrono>
#include <set>
#include <string>

#include "Anchor.h"
#include "soh/OTRGlobals.h"
#include "soh/util.h"
#include "soh/Enhancements/randomizer/SeedContext.h"

extern "C" {
extern PlayState* gPlayState;
}

void AnchorRoomWindow::Draw() {
    if (!IsVisible() || !Anchor::Instance->isConnected) {
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg,
                          ImVec4(0, 0, 0, CVarGetFloat(CVAR_SETTING("Notifications.BgOpacity"), 0.5f)));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

    auto vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowViewport(vp->ID);

    ImGui::Begin("Anchor Room", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar);

    DrawElement();

    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

static ImVec4 PingColor(int pingMs) {
    if (pingMs < 80) {
        return ImVec4(0.5f, 1.0f, 0.5f, 1.0f);
    }
    if (pingMs < 150) {
        return ImVec4(1.0f, 0.9f, 0.4f, 1.0f);
    }
    return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
}

// Hearts (or DOWN) and ping after a player's name. -1 = unknown, not shown.
static void DrawPlayerStatus(s16 health, s16 healthCapacity, bool isDown, int pingMs) {
    if (isDown) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "DOWN");
    } else if (health >= 0 && healthCapacity > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), ICON_FA_HEART " %g/%g", health / 16.0f,
                           healthCapacity / 16.0f);
    }
    if (pingMs >= 0) {
        ImGui::SameLine();
        ImGui::TextColored(PingColor(pingMs), "%d ms", pingMs);
    }
}

void AnchorRoomWindow::DrawElement() {
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));
    bool showNetwork = CVarGetInteger(CVAR_REMOTE_ANCHOR("Debug.ShowNetwork"), 0) != 0;

    if (isGlobalRoom) {
        u32 activeClients = 0;
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            if (client.online) {
                activeClients++;
            }
        }
        ImGui::Text("Players Online: %d", activeClients);
        if (showNetwork) {
            Anchor::Instance->DrawNetworkDetails();
        }
        return;
    }

    // First build a list of teams
    std::set<std::string> teams;
    for (auto& [clientId, client] : Anchor::Instance->clients) {
        teams.insert(client.teamId);
    }

    for (auto& team : teams) {
        if (teams.size() > 1) {
            ImGui::SeparatorText(team.c_str());
        }
        bool isOwnTeam = team == CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            if (client.teamId != team) {
                continue;
            }

            ImGui::PushID(clientId);

            if (client.clientId == Anchor::Instance->roomState.ownerClientId) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s", ICON_FA_GAVEL);
                ImGui::SameLine();
            }

            if (client.self) {
                ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.8f, 1.0f), "%s", CVarGetString(CVAR_REMOTE_ANCHOR("Name"), ""));
            } else if (!client.online) {
                ImGui::TextColored(ImVec4(1, 1, 1, 0.3f), "%s - offline", client.name.c_str());
                ImGui::PopID();
                continue;
            } else {
                ImGui::Text("%s", client.name.c_str());
            }

            if (Anchor::Instance->roomState.showLocationsMode == 2 ||
                (Anchor::Instance->roomState.showLocationsMode == 1 && isOwnTeam)) {
                if ((client.self ? Anchor::Instance->IsSaveLoaded() : client.isSaveLoaded)) {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        ImVec4(1, 1, 1, 0.5f), "- %s",
                        SohUtils::GetSceneName(client.self ? gPlayState->sceneNum : client.sceneNum).c_str());
                }
            }

            // Hearts and DOWN for the own team, ping for everyone
            if (client.self) {
                bool saveLoaded = Anchor::Instance->IsSaveLoaded();
                DrawPlayerStatus(saveLoaded ? gSaveContext.health : (s16)-1,
                                 saveLoaded ? gSaveContext.healthCapacity : (s16)-1, Anchor::Instance->coop.isDown,
                                 Anchor::Instance->rttMs);
            } else {
                bool showHealth = isOwnTeam && client.isSaveLoaded;
                DrawPlayerStatus(showHealth ? client.health : (s16)-1, client.healthCapacity,
                                 isOwnTeam && client.isDown, client.pingMs);
            }

            if (Anchor::Instance->CanTeleportTo(client.clientId)) {
                ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (ImGui::Button(ICON_FA_LOCATION_ARROW, ImVec2(20.0f, 20.0f))) {
                    Anchor::Instance->SendPacket_RequestTeleport(client.clientId);
                }
                ImGui::PopStyleVar();
            }

            if (client.clientVersion != Anchor::clientVersion) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), ICON_FA_EXCLAMATION_TRIANGLE);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Incompatible version! Will not work together!");
                    ImGui::Text("Yours: %s", Anchor::clientVersion.c_str());
                    ImGui::Text("Theirs: %s", client.clientVersion.c_str());
                    ImGui::EndTooltip();
                }
            }
            uint32_t seed = IS_RANDO ? Rando::Context::GetInstance()->GetSeed() : 0;
            if (client.isSaveLoaded && Anchor::Instance->IsSaveLoaded() && client.seed != seed && client.online &&
                !client.self) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0, 0, 1), ICON_FA_EXCLAMATION_TRIANGLE);
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    ImGui::Text("Seed mismatch! Continuing will break things!");
                    ImGui::Text("Yours: %u", seed);
                    ImGui::Text("Theirs: %u", client.seed);
                    ImGui::EndTooltip();
                }
            }
            ImGui::PopID();
        }
    }

    if (showNetwork) {
        Anchor::Instance->DrawNetworkDetails();
    }
}

// MARK: - Network details (debug)

void Anchor::DrawNetworkDetails() {
    // Traffic per second, measured over the last second
    static std::chrono::steady_clock::time_point lastSample;
    static uint64_t lastBytesSent = 0, lastBytesReceived = 0, lastPacketsSent = 0, lastPacketsReceived = 0;
    static float kbSent = 0.0f, kbReceived = 0.0f, packetsSentRate = 0.0f, packetsReceivedRate = 0.0f;

    auto now = std::chrono::steady_clock::now();
    float elapsed = std::chrono::duration<float>(now - lastSample).count();
    if (elapsed >= 1.0f) {
        uint64_t sent = bytesSent, received = bytesReceived, packetsOut = packetsSent, packetsIn = packetsReceived;
        if (elapsed < 5.0f && sent >= lastBytesSent && received >= lastBytesReceived) {
            kbSent = (float)(sent - lastBytesSent) / 1024.0f / elapsed;
            kbReceived = (float)(received - lastBytesReceived) / 1024.0f / elapsed;
            packetsSentRate = (float)(packetsOut - lastPacketsSent) / elapsed;
            packetsReceivedRate = (float)(packetsIn - lastPacketsReceived) / elapsed;
        }
        lastBytesSent = sent;
        lastBytesReceived = received;
        lastPacketsSent = packetsOut;
        lastPacketsReceived = packetsIn;
        lastSample = now;
    }

    ImGui::SeparatorText("Network");

    int roundTrip = rttMs;
    if (roundTrip >= 0) {
        ImGui::Text("Ping:");
        ImGui::SameLine();
        ImGui::TextColored(PingColor(roundTrip), "%d ms", roundTrip);
    } else {
        ImGui::Text("Ping: unknown (the server doesn't answer pings)");
    }
    ImGui::Text("Sent: %.1f KB/s, %.0f packets/s", kbSent, packetsSentRate);
    ImGui::Text("Received: %.1f KB/s, %.0f packets/s", kbReceived, packetsReceivedRate);
    ImGui::Text("Waiting: %d to send, %d to handle", (int)outgoingQueueLength.load(), (int)incomingQueueLength.load());

    int udp = udpState;
    ImGui::Text("Movement updates: %s", udp == 2 ? "UDP" : udp == 1 ? "TCP (trying UDP)" : "TCP");
    ImGui::Text("Connection: %s", isTlsActive ? ICON_FA_LOCK " encrypted (TLS)" : ICON_FA_UNLOCK " not encrypted");

    int latency = simLatencyMs, jitter = simJitterMs, loss = simLossPercent;
    if (latency > 0 || jitter > 0 || loss > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.4f, 1.0f), "Simulated: +%d ms, jitter %d ms, %d%% lost", latency,
                           jitter, loss);
    }

    if (IsSaveLoaded() && roomState.syncEnemyMovement) {
        uint32_t authority = EnemySync_GetAuthority();
        if (authority == 0) {
            ImGui::Text("Enemies: no teammate in this room");
        } else if (authority == ownClientId) {
            ImGui::Text("Enemies: led by you");
        } else {
            auto it = clients.find(authority);
            ImGui::Text("Enemies: led by %s", it != clients.end() ? it->second.name.c_str() : "?");
        }
    }
}
