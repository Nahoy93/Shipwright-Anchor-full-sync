#include <nlohmann/json.hpp>

#include "soh/Network/Anchor/Anchor.h"

extern "C" {
#include "macros.h"
#include "functions.h"
}

/**
 * RUPEE_INCOME
 *
 * Shared income: whenever this client's wallet goes up, the amount is sent to the team and added to their wallets
 * too. Spending is not synced, so everybody spends from their own wallet. See Coop_UpdateRupees for how incoming
 * rupees are kept from being sent back out again.
 *
 * Queued on the server, so teammates that are offline get the income when they load in (until someone saves).
 */

void Anchor::SendPacket_RupeeIncome(s32 amount) {
    if (!IsSaveLoaded() || !roomState.shareRupees || amount <= 0) {
        return;
    }

    nlohmann::json payload;
    payload["type"] = RUPEE_INCOME;
    payload["targetTeamId"] = CVarGetString(CVAR_REMOTE_ANCHOR("TeamId"), "default");
    payload["addToQueue"] = true;
    payload["amount"] = amount;

    SendJsonToRemote(payload);
}

void Anchor::HandlePacket_RupeeIncome(nlohmann::json payload) {
    if (!IsSaveLoaded() || !roomState.shareRupees) {
        return;
    }

    s32 amount = payload.at("amount").get<s32>();
    if (amount <= 0 || amount > 9999) {
        return;
    }

    // Goes through the accumulator so it counts up with the usual sound, and is capped by the wallet size.
    // (Not Rupees_ChangeBy, so the "rupees collected" stat only counts your own finds)
    gSaveContext.rupeeAccumulator = (s16)CLAMP(gSaveContext.rupeeAccumulator + amount, -0x7FFF, 0x7FFF);
    coop.pendingRemoteRupees += amount;
}
