#include <ship/Context.h>

#include "Anchor.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/SohMenu.h"
#include "soh/util.h"
#include "soh/SohGui/UIWidgets.hpp"

namespace SohGui {
extern std::shared_ptr<SohMenu> mSohMenu;
extern std::shared_ptr<AnchorRoomWindow> mAnchorRoomWindow;
} // namespace SohGui

static const char* pvpModes[3] = { "Off", "On", "On + Friendly Fire" };
static std::vector<const char*> teleportModes = { "None", "Team Only", "All" };
static std::vector<const char*> showLocationsModes = { "None", "Team Only", "All" };
static std::vector<const char*> cutsceneSyncModes = { "Off", "Same Area Only", "Everyone (Pulled To Cutscene)" };

void AnchorMainMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    std::string host = CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
    uint16_t port = CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
    std::string anchorTeamId = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    std::string anchorRoomId = CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
    std::string anchorName = CVarGetString(CVAR_REMOTE_ANCHOR("Name"), "");
    std::string anchorPassword = CVarGetString(CVAR_REMOTE_ANCHOR("Password"), "");
    bool isFormValid = !SohUtils::IsStringEmpty(host) && port > 1024 && port < 65535 &&
                       !SohUtils::IsStringEmpty(anchorRoomId) && !SohUtils::IsStringEmpty(anchorName);

    AnchorSessionsMenu(anchor->isEnabled);

    ImGui::SeparatorText("Connection Settings");

    ImGui::BeginDisabled(anchor->isEnabled);
    ImGui::Text("Host & Port");
    if (UIWidgets::InputString("##Host", &host,
                               UIWidgets::InputOptions()
                                   .Size(ImGui::GetContentRegionAvail() -
                                         ImVec2((ImGui::GetFontSize() * 5 + ImGui::GetStyle().ItemSpacing.x), 0))
                                   .Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), host.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::SameLine();
    UIWidgets::PushStyleInput(THEME_COLOR);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5);
    if (ImGui::InputScalar("##Port", ImGuiDataType_U16, &port)) {
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), port);
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    UIWidgets::PopStyleInput();

    ImGui::Text("Name & Color");
    static Color_RGBA8 defaultColor = { 100, 255, 100, 255 };
    UIWidgets::CVarColorPicker("##Color", CVAR_REMOTE_ANCHOR("Color"), defaultColor);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##Name", &anchorName, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Name"), anchorName.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Text("Room ID");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##RoomId", &anchorRoomId,
                               UIWidgets::InputOptions().IsSecret(anchor->isEnabled).Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), anchorRoomId.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Text("Server Password (leave empty if the server has none)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##Password", &anchorPassword,
                               UIWidgets::InputOptions().IsSecret(true).Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Password"), anchorPassword.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Text("Team ID (Items & Flags Shared)");
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (UIWidgets::InputString("##TeamId", &anchorTeamId, UIWidgets::InputOptions().Color(THEME_COLOR))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), anchorTeamId.c_str());
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }
    ImGui::Spacing();

    if (UIWidgets::Button("Restore Defaults", UIWidgets::ButtonOptions()
                                                  .Size(ImVec2(ImGui::GetContentRegionAvail().x / 2, 0))
                                                  .Color(UIWidgets::Colors::Red))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
        CVarSetString(CVAR_REMOTE_ANCHOR("Name"), "");
        CVarSetString(CVAR_REMOTE_ANCHOR("Password"), "");
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::SameLine();

    if (UIWidgets::Button("Global Room", UIWidgets::ButtonOptions()
                                             .Color(UIWidgets::Colors::Blue)
                                             .Tooltip("Always-online public room so you don't have to experience "
                                                      "Hyrule alone. PVP and syncing are disabled."))) {
        CVarSetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
        CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
        CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), "soh-global");
        Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
    }

    ImGui::EndDisabled();

    ImGui::Spacing();

    ImGui::BeginDisabled(!isFormValid);
    const char* buttonLabel = anchor->isEnabled ? "Disable" : "Enable";
    UIWidgets::PushStyleButton(anchor->isEnabled ? UIWidgets::ColorValues.at(UIWidgets::Colors::Red)
                                                 : UIWidgets::ColorValues.at(UIWidgets::Colors::Green));
    if (ImGui::Button(buttonLabel, ImVec2(-1.0f, 0.0f))) {
        if (anchor->isEnabled) {
            CVarClear(CVAR_REMOTE_ANCHOR("Enabled"));
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Disable();
        } else {
            CVarSetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 1);
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
            anchor->Enable();
        }
    }
    UIWidgets::PopStyleButton();
    ImGui::EndDisabled();
    ImGui::Spacing();

    if (!anchor->isEnabled) {
        return;
    }

    if (!anchor->isConnected) {
        ImGui::Text("Connecting...");
        return;
    }

    ImGui::SeparatorText("Current Room");
    ImGui::Text("%s Connected", ICON_FA_CHECK);

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (ImGui::Button("Request Team State")) {
        anchor->SendPacket_RequestTeamState();
    }
    UIWidgets::Tooltip("Try this if you are missing items or flags that your team members have collected");
    UIWidgets::PopStyleButton();

    ImGui::SameLine();

    UIWidgets::WindowButton("Toggle Anchor Room Window", CVAR_WINDOW("AnchorRoom"), SohGui::mAnchorRoomWindow);

    ImGui::Spacing();

    bool hideLocations = Anchor::Instance->roomState.showLocationsMode == 0;
    ImGui::BeginDisabled(hideLocations);
    UIWidgets::CVarCheckbox(
        "Show Other Players on Minimap", CVAR_REMOTE_ANCHOR("ShowOtherPlayersOnMinimap"),
        UIWidgets::CheckboxOptions()
            .Color(THEME_COLOR)
            .DefaultValue(true)
            .Tooltip(!hideLocations
                         ? "Other players will appear on the minimap in areas where you have the compass. "
                           "Visibility is restricted according to the Show Locations mode for the room."
                         : "Cannot show other players because the room's Show Locations mode is set to None."));
    ImGui::EndDisabled();

    ImGui::Spacing();

    if (!SohGui::mAnchorRoomWindow->IsVisible()) {
        SohGui::mAnchorRoomWindow->DrawElement();
    }
}

void AnchorAdminMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;
    bool isGlobalRoom = (std::string("soh-global") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), ""));

    if (!anchor->isEnabled || !anchor->isConnected || anchor->roomState.ownerClientId != anchor->ownClientId ||
        isGlobalRoom) {
        return;
    }

    ImGui::SeparatorText("Room Settings (Admin Only)");

    UIWidgets::PushStyleButton(THEME_COLOR);
    if (ImGui::Button("Clear All Team State")) {
        std::set<std::string> teams;
        for (auto& [clientId, client] : Anchor::Instance->clients) {
            teams.insert(client.teamId);
        }
        for (auto& team : teams) {
            anchor->SendPacket_ClearTeamState(team);
        }
    }
    UIWidgets::PopStyleButton();

    if (UIWidgets::CVarCombobox("PvP Mode:", CVAR_REMOTE_ANCHOR("RoomSettings.PvpMode"), pvpModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Show Locations For:", CVAR_REMOTE_ANCHOR("RoomSettings.ShowLocationsMode"),
                                showLocationsModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCombobox("Allow Teleporting To:", CVAR_REMOTE_ANCHOR("RoomSettings.TeleportMode"), teleportModes,
                                UIWidgets::ComboboxOptions()
                                    .DefaultIndex(1)
                                    .LabelPosition(UIWidgets::LabelPositions::Above)
                                    .Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Sync Items & Flags", CVAR_REMOTE_ANCHOR("RoomSettings.SyncItemsAndFlags"),
                                UIWidgets::CheckboxOptions().DefaultValue(true).Color(THEME_COLOR))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::SeparatorText("Co-op");

    if (UIWidgets::CVarCheckbox("Shared Rupee Income", CVAR_REMOTE_ANCHOR("RoomSettings.ShareRupees"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(true)
                                    .Color(THEME_COLOR)
                                    .Tooltip("Rupees a teammate finds are added to everyone's wallet. Spending only "
                                             "affects your own wallet."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox(
            "Team Death (Revive Teammates)", CVAR_REMOTE_ANCHOR("RoomSettings.TeamDeath"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(true)
                .Color(THEME_COLOR)
                .Tooltip(
                    "Health is individual. A player who dies stays down until a teammate stands next to them for 2 "
                    "seconds. When the whole team is down at once, everyone gets a game over and continues from their "
                    "checkpoint or last save."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Sync Enemy & Boss Kills", CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemies"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(true)
                                    .Color(THEME_COLOR)
                                    .Tooltip("Enemies and bosses defeated by a teammate also die for players in the "
                                             "same area. Teammates in a boss "
                                             "room also get the blue warp."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Sync Enemy Movement & Health (Experimental)",
                                CVAR_REMOTE_ANCHOR("RoomSettings.SyncEnemyMovement"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(true)
                                    .Color(THEME_COLOR)
                                    .Tooltip("Players in the same room see enemies in the same places and share their "
                                             "health, so everyone's hits "
                                             "count. Enemies spawned during gameplay appear for everyone. One player "
                                             "per room leads the enemies. "
                                             "Bosses only share their health."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox(
            "Share Projectiles & Objects", CVAR_REMOTE_ANCHOR("RoomSettings.SyncInteractions"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(true)
                .Color(THEME_COLOR)
                .Tooltip(
                    "Teammates in the same area see your arrows, seeds, nuts, bombs, bombchus and boomerang. Pots, "
                    "crates, "
                    "rocks, bushes, grass, push blocks and torches are shared, so you can solve puzzles together."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    if (UIWidgets::CVarCheckbox(
            "Share Player Effects", CVAR_REMOTE_ANCHOR("RoomSettings.SyncPlayerEffects"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(true)
                .Color(THEME_COLOR)
                .Tooltip(
                    "Masks, being on fire or shocked, burning Deku Sticks and Megaton Hammer hits show on teammates' "
                    "figures. When a teammate in the same room uses the Lens of Truth, you see what it reveals "
                    "too."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Shared Time Of Day", CVAR_REMOTE_ANCHOR("RoomSettings.SyncTime"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(true)
                                    .Color(THEME_COLOR)
                                    .Tooltip("The whole team has the same time of day, following the player who joined "
                                             "first."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    if (UIWidgets::CVarCheckbox(
            "Shared Music", CVAR_REMOTE_ANCHOR("RoomSettings.SyncMusic"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(true)
                .Color(THEME_COLOR)
                .Tooltip("Everyone in the same area hears the same music, following the teammate who "
                         "joined first."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::SeparatorText("Ocarina");
    if (UIWidgets::CVarCheckbox(
            "Share Song Effects", CVAR_REMOTE_ANCHOR("RoomSettings.SyncSongs"),
            UIWidgets::CheckboxOptions()
                .DefaultValue(true)
                .Color(THEME_COLOR)
                .Tooltip("Sun's Song changes the time for the whole team, Song of Storms makes it rain and Song of "
                         "Time moves "
                         "the blocks for teammates in the same area. Everyone is told which song was played."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarCheckbox("Warp Songs Bring The Team", CVAR_REMOTE_ANCHOR("RoomSettings.SyncWarpSongs"),
                                UIWidgets::CheckboxOptions()
                                    .DefaultValue(true)
                                    .Color(THEME_COLOR)
                                    .Tooltip("When someone warps with a warp song, all teammates warp along."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::SeparatorText("Sync Rate");
    if (UIWidgets::CVarSliderInt("Player Updates Per Second: %d", CVAR_REMOTE_ANCHOR("RoomSettings.PlayerUpdateRate"),
                                 UIWidgets::IntSliderOptions()
                                     .Min(1)
                                     .Max(20)
                                     .DefaultValue(20)
                                     .Color(THEME_COLOR)
                                     .Tooltip("How often each player sends their position and animation. 20 is the "
                                              "smoothest (every game update). Lower it if someone has a slow "
                                              "connection."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarSliderInt("Enemy Updates Per Second: %d", CVAR_REMOTE_ANCHOR("RoomSettings.EnemyUpdateRate"),
                                 UIWidgets::IntSliderOptions()
                                     .Min(1)
                                     .Max(20)
                                     .DefaultValue(10)
                                     .Color(THEME_COLOR)
                                     .Tooltip("How often the player leading a room sends the enemies' positions and "
                                              "health. Higher is smoother but uses more bandwidth."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarSliderInt("Projectile & Object Updates Per Second: %d",
                                 CVAR_REMOTE_ANCHOR("RoomSettings.ObjectUpdateRate"),
                                 UIWidgets::IntSliderOptions()
                                     .Min(1)
                                     .Max(20)
                                     .DefaultValue(20)
                                     .Color(THEME_COLOR)
                                     .Tooltip("How often projectiles, magic, moved objects (pots, blocks, boulders) "
                                              "and loose items (drops, fairies, bugs, fish) are sent."))) {
        anchor->SendPacket_UpdateRoomState();
    }
    if (UIWidgets::CVarSliderInt("Time Of Day Sync Every %d Seconds",
                                 CVAR_REMOTE_ANCHOR("RoomSettings.TimeSyncSeconds"),
                                 UIWidgets::IntSliderOptions()
                                     .Min(1)
                                     .Max(30)
                                     .DefaultValue(5)
                                     .Color(THEME_COLOR)
                                     .Tooltip("How often the time keeper sends the time of day."))) {
        anchor->SendPacket_UpdateRoomState();
    }

    ImGui::SeparatorText("Cutscenes");
    if (UIWidgets::CVarCombobox(
            "Share Cutscenes With:", CVAR_REMOTE_ANCHOR("RoomSettings.SyncCutscenes"), cutsceneSyncModes,
            UIWidgets::ComboboxOptions()
                .DefaultIndex(2)
                .LabelPosition(UIWidgets::LabelPositions::Above)
                .Color(THEME_COLOR)
                .Tooltip("When someone triggers a cutscene, teammates watch it too. With Everyone, teammates in other "
                         "areas are brought to the cutscene first. Players of a different age don't get it."))) {
        anchor->SendPacket_UpdateRoomState();
    }
}

void AnchorInstructionsMenu(WidgetInfo& info) {
    auto anchor = Anchor::Instance;

    ImGui::SeparatorText("Usage Instructions");

    ImGui::TextWrapped("0. Self hosted server: enter its address (your public IP or domain) and port under Host & "
                       "Port, and the server password if it has one.");

    ImGui::TextWrapped("1. All players involved should start at the file select screen");

    ImGui::TextWrapped("2. Come up with a unique Room ID (this is basically your password) and enter it, along with "
                       "your desired player name and team ID and click Enable");

    ImGui::TextWrapped("3. The host should configure the randomizer settings and generate a seed, then share the newly "
                       "generated JSON spoiler file with other players.");

    ImGui::TextWrapped("4. All players should load the same JSON spoiler file (drag it into SoH window), make sure "
                       "seed icons match, then create a new file.");

    ImGui::TextWrapped("5. All players should now load into their game. IMPORTANT! If using an existing save/seed "
                       "ensure the player with the most progress loads the file first.");

    ImGui::TextWrapped("6. After everyone has loaded in, verify on the network tab that it doesn't warn about anyone "
                       "being on a wrong version or seed.");

    ImGui::Spacing();

    ImGui::TextWrapped(
        "Note: Team ID is used to group players together in the same team, sharing items and flags. Make sure all "
        "players who want to share progress use the same Team ID. All players with the same Team ID should be using "
        "the same randomizer seed, while players on different teams can use different seeds.");
}

void RegisterAnchorMenu() {
    WidgetPath path = { "Network", "Anchor", SECTION_COLUMN_1 };
    SohGui::mSohMenu->AddWidget(path, "AnchorMainMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorMainMenu)
        .HideInSearch(true);
    path.column = SECTION_COLUMN_2;
    SohGui::mSohMenu->AddWidget(path, "AnchorAdminMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorAdminMenu)
        .HideInSearch(true);
    SohGui::mSohMenu->AddWidget(path, "AnchorInstructionsMenu", WIDGET_CUSTOM)
        .CustomFunction(AnchorInstructionsMenu)
        .HideInSearch(true);
}

static RegisterMenuInitFunc menuInitFunc(RegisterAnchorMenu);
