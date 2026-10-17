#include <ship/Context.h>
#include <nlohmann/json.hpp>

#include "Anchor.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SohGui/UIWidgets.hpp"

/**
 * Saved sessions
 *
 * A session is one multiplayer game: the server (host, port, password), the Room ID and the Team ID. The server keeps
 * the team's progress for every Room ID (see persist.go on the server), and each player keeps their own save file. To
 * continue a session later you connect to the same room again and load the same save file, this menu remembers both.
 *
 * Stored as a JSON array in a CVar, so it lives in shipofharkinian.json with the other settings.
 */

static const char* SESSIONS_CVAR = CVAR_REMOTE_ANCHOR("SavedSessions");

static nlohmann::json LoadSessions() {
    try {
        nlohmann::json sessions = nlohmann::json::parse(CVarGetString(SESSIONS_CVAR, "[]"));
        if (sessions.is_array()) {
            return sessions;
        }
    } catch (...) {}
    return nlohmann::json::array();
}

static void StoreSessions(const nlohmann::json& sessions) {
    CVarSetString(SESSIONS_CVAR, sessions.dump().c_str());
    Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
}

static bool IsCurrentSession(const nlohmann::json& session) {
    return session.value("host", "") == CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org") &&
           session.value("port", 0) == CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383) &&
           session.value("roomId", "") == CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), "") &&
           session.value("teamId", "") == CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
}

void AnchorSessions_RecordSaveFile(int32_t fileNum) {
    nlohmann::json sessions = LoadSessions();
    bool changed = false;
    for (auto& session : sessions) {
        if (IsCurrentSession(session) && session.value("saveFile", 0) != fileNum + 1) {
            session["saveFile"] = fileNum + 1;
            changed = true;
        }
    }
    if (changed) {
        StoreSessions(sessions);
    }
}

void AnchorSessionsMenu(bool isEnabled) {
    static int selected = 0;
    static std::string newName;

    nlohmann::json sessions = LoadSessions();

    ImGui::SeparatorText("Saved Sessions");

    if (!sessions.empty()) {
        selected = std::clamp(selected, 0, (int)sessions.size() - 1);
        auto& current = sessions[selected];

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::BeginCombo("##SavedSessions", current.value("name", "?").c_str())) {
            for (int i = 0; i < (int)sessions.size(); i++) {
                std::string label = sessions[i].value("name", "?") + "##" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), i == selected)) {
                    selected = i;
                }
            }
            ImGui::EndCombo();
        }

        auto& shown = sessions[selected];
        ImGui::TextDisabled("%s:%d  room \"%s\"  team \"%s\"", shown.value("host", "").c_str(), shown.value("port", 0),
                            shown.value("roomId", "").c_str(), shown.value("teamId", "").c_str());
        if (shown.value("saveFile", 0) > 0) {
            ImGui::TextDisabled("Load save file %d to continue this session", shown.value("saveFile", 0));
        }

        ImGui::BeginDisabled(isEnabled);
        if (UIWidgets::Button("Use Session", UIWidgets::ButtonOptions()
                                                 .Size(ImVec2(ImGui::GetContentRegionAvail().x / 2, 0))
                                                 .Color(THEME_COLOR)
                                                 .Tooltip("Fill in the connection settings below from this session"))) {
            CVarSetString(CVAR_REMOTE_ANCHOR("Host"), shown.value("host", "").c_str());
            CVarSetInteger(CVAR_REMOTE_ANCHOR("Port"), shown.value("port", 43383));
            CVarSetString(CVAR_REMOTE_ANCHOR("Password"), shown.value("password", "").c_str());
            CVarSetString(CVAR_REMOTE_ANCHOR("RoomId"), shown.value("roomId", "").c_str());
            CVarSetString(CVAR_REMOTE_ANCHOR("TeamId"), shown.value("teamId", "default").c_str());
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->SaveConsoleVariablesNextFrame();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (UIWidgets::Button("Forget Session", UIWidgets::ButtonOptions()
                                                    .Color(UIWidgets::Colors::Red)
                                                    .Tooltip("Removes it from this list only. The progress saved on "
                                                             "the server and in your save file is kept."))) {
            sessions.erase(selected);
            selected = 0;
            StoreSessions(sessions);
            return;
        }
    } else {
        ImGui::TextDisabled("No saved sessions yet");
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFontSize() * 9);
    UIWidgets::InputOptions nameOptions = UIWidgets::InputOptions().Color(THEME_COLOR);
    nameOptions.placeholder = "Session name";
    UIWidgets::InputString("##NewSessionName", &newName, nameOptions);
    ImGui::SameLine();
    std::string roomId = CVarGetString(CVAR_REMOTE_ANCHOR("RoomId"), "");
    ImGui::BeginDisabled(newName.empty() || roomId.empty());
    if (UIWidgets::Button(
            "Save Current",
            UIWidgets::ButtonOptions()
                .Color(THEME_COLOR)
                .Tooltip("Remember the current host, port, password, Room ID and Team ID under this name. While "
                         "connected, the save file you load is remembered too."))) {
        nlohmann::json session;
        session["name"] = newName;
        session["host"] = CVarGetString(CVAR_REMOTE_ANCHOR("Host"), "anchor.hm64.org");
        session["port"] = CVarGetInteger(CVAR_REMOTE_ANCHOR("Port"), 43383);
        session["password"] = CVarGetString(CVAR_REMOTE_ANCHOR("Password"), "");
        session["roomId"] = roomId;
        session["teamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
        session["saveFile"] = 0;

        // Replace a session with the same name
        for (int i = 0; i < (int)sessions.size(); i++) {
            if (sessions[i].value("name", "") == newName) {
                session["saveFile"] = sessions[i].value("saveFile", 0);
                sessions.erase(i);
                break;
            }
        }
        sessions.push_back(session);
        selected = (int)sessions.size() - 1;
        newName.clear();
        StoreSessions(sessions);
    }
    ImGui::EndDisabled();
}
