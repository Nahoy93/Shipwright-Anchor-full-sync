# Zelda MP – co-op and private server

## New in 1.2

- **Encrypted connection (TLS)** for the password and everything else, with a certificate the server makes itself.
- **UDP for movement:** positions, enemies, projectiles and sounds no longer freeze for a moment when a packet is lost. Falls back to TCP by itself.
- **Less delay:** received packets are used at the start of each game update (up to 50 ms sooner), the server copies packets to everyone instead of the game sending one per player, and enemies that follow another player are predicted to where they should be by now.
- **Ping, hearts and who is down** in the room window, and a teammate who is down blinks red on the minimap.
- **Reconnects by itself** when the connection silently dies (Wi-Fi switch), with a short pause between attempts.
- **Network debug:** traffic and queues in the room window, plus simulated latency, jitter and packet loss to test a bad connection at home.
- **Smoother picture by default** (*Match Refresh Rate* is on).
- Fixes: webs, red ice and walls open with their real animation again, hits from several players on the same enemy all count, falling into the void always hurts after a cutscene teleport.

The full list is in `CHANGELOG.md`. Everyone needs the new game **and** the new server.

These features have been added to Ship of Harkinian and the Anchor server:

| Feature | How it works |
|---|---|
| **Private server with password** | You run `anchor.exe` yourself. Anyone entering the wrong password is refused. After 10 wrong attempts in 5 minutes, that IP address is blocked for a while. |
| **Team death** | Health is individual. A player who dies stays down on the ground. A teammate standing next to them for 2 seconds revives them with a fairy and 3 hearts. If **the whole team** is down at the same time, it's a normal game over and you continue from the checkpoint or last save. |
| **Shared income** | Rupees anyone finds are added to everyone's wallet. What you buy is only taken from your own wallet. |
| **Chests** | When someone opens a chest, everyone on the team gets the item, and the chest also opens for anyone in the same room. Requires *Sync Items & Flags* (on by default). |
| **Enemies and bosses** | An enemy killed by one player also dies for teammates in the same area. A boss dies for everyone in the boss room, and the blue warp appears for everyone. |
| **Enemy movement and health** *(experimental)* | Players in the same room see enemies in the same places, and enemies share health so everyone's hits count. Enemies spawned during play appear for everyone. Bosses only share health. |
| **Cutscenes** | When someone triggers a cutscene, teammates see it too. With the *Everyone* setting, teammates in other areas are teleported there first. |
| **Ocarina songs** | Everyone hears the notes. **Sun's Song** changes the time for the whole team, **Song of Storms** brings rain and **Song of Time** moves the time blocks for those in the same area. **Warp songs** take the whole team along. Everyone gets a notification about which song was played. |
| **Projectiles and objects** | Arrows (including fire, ice and light arrows), seeds, Deku Nuts, bombs, bombchus, the boomerang, the hookshot and the spin attack swirl are visible to teammates in the same area. Bombs and Bomb Flowers explode for everyone. Magic is visible too: Din's Fire, Farore's Wind and Nayru's Love. Beehives, rolling boulders, ice blocks, rotating walls, the Goron statue and the cobra statues in the Spirit Temple follow along. Silver rupees count for the whole team. Pots, crates, rocks and bushes that are lifted, thrown or broken, grass that is cut, blocks that are pushed and torches that are lit are shared, so you can solve puzzles together. |
| **Player effects** | Masks, fire and electric shocks on the body, a burning Deku Stick and hammer shockwaves show on teammates' figures. If a teammate in the same room uses the Lens of Truth, you see what the lens reveals too. |
| **Time of day** | The whole team has the same time. Whoever joined first keeps the time. |
| **Music** | Everyone in the same area hears the same music, including boss music and silence. Whoever joined first decides. |
| **Loose items, fairies, bugs and fish** | Hearts, rupees and anything else that drops from enemies, pots and grass shows for everyone in the same area. Fairies, bugs and fish that appear during play exist for everyone too. Whoever grabs something first gets it, and it disappears for the others. |
| **Farore's Wind** | The warp point is shared by the team (one per age). Using it uses it up for everyone. |
| **Epona** | When a teammate rides, the horse shows under their figure. It is only a copy and can't be ridden. |
| **Real arrows and boomerangs** | Teammates' arrows and boomerangs fly for real in your game and can hit enemies and switches for you. |
| **Platforms and elevators** | Moving platforms and elevators are in the same place for everyone in the room. Whoever joined first decides. The water level in the Water Temple, the Forest Temple elevator and the Song of Time blocks don't follow, since they are controlled by flags. |
| **Enemies in other rooms** | If a teammate kills enemies in a room you're not in, they also die when you get there, as long as the teammate is still in the room. Enemies that belong to another enemy are now spawned under the right "parent" for you. |
| **Shared game over** | If the whole team is down, the first one to answer "Save?" and "Continue?" answers for the whole team. Requires team death. |
| **Trade timers** | The timer for the Odd Mushroom, Frog and Eye Drops starts and stops for everyone carrying the same item. |
| **Webs, red ice, bombable walls** | Spider webs that are burned, red ice melted with blue fire, walls and boulders that are blown up and pillars smashed with the hammer disappear right away for teammates in the same room. |
| **Spirit Temple warp blocks** | When someone swaps a Song of Time warp block, it swaps for teammates too. |
| **Weather** | Rain, storms, lightning and dark skies are the same for everyone in the area. Whoever joined first decides. |
| **Learned songs** | When a teammate learns a song, everyone gets it. You also see the teacher's demonstration with the notes as soon as you're standing still and free. |
| **Boss intros** | When someone enters the boss room, everyone in the same dungeon is brought there, so the whole team sees the intro and fights together. |
| **Shared age** | If someone becomes a child or an adult in the Temple of Time, the rest of the team follows to the Temple of Time at the same age. |
| **Spectator camera** | While you're down, the camera follows a teammate in the same area. Switch teammate with L. It can be turned off with *Spectate Teammates While Down*. |
| **Solid figures** | Teammates' figures are solid and can't be walked through. This doesn't apply while someone is down. |
| **Animals** | Cuccos and horses in the area are in the same place for everyone. If you're carrying a Cucco, or just threw it, you control it. |
| **Saved sessions** | The server saves each room's progress in the `sessions\` folder and loads it again when someone joins the same Room ID, even after a restart. Different Room IDs are different sessions. |
| **Update rate** | The room owner chooses how many times per second player and enemy positions are sent (1–20). |
| **Encrypted connection** | With *Encrypted Connection (TLS)* on, everything between you and the server is encrypted, the password too. The first connection saves the server's certificate, and the game refuses the server if it ever changes. |
| **UDP for movement** | Positions, enemy updates, projectiles and sounds go over UDP when it works: a lost packet is replaced by the next one instead of holding everything up. If UDP is blocked, they go over TCP like before. |
| **Ping and status** | The room window shows everyone's ping, your teammates' hearts and who is down. A teammate who is down blinks red on the minimap. |

The room owner can turn every feature on and off under **Network → Anchor → Co-op**.

---

## 1. Install

1. Unzip **Zelda-MP-Coop-Windows.zip**.
2. Copy everything in `Game\` into your Ship of Harkinian folder (where your `oot.o2r` is). Keep your `oot.o2r` and your save files. If you don't have Ship of Harkinian yet, you can play straight from the `Game\` folder: start `soh.exe` and it will ask for your own Ocarina of Time ROM to create `oot.o2r`.
3. **All players must have exactly the same soh.exe and soh.o2r.** Clients with different versions ignore each other. Give your friends the same zip.

If the game finds an older `oot.o2r`, it may ask you to create a new one from your ROM at startup. That's normal.

## 2. Start the server

Only the person hosting needs to do this.

1. Open `Server\Start server.bat` in Notepad.
2. Change `ANCHOR_PASSWORD=change-to-your-password` to a password of your own. Save the file.
3. Double click `Start server.bat`. If Windows Firewall asks, click **Allow** (it asks for both TCP and UDP).
4. The window shows `Server running on :43383`, `Password protection: ON`, `Encryption (TLS): ON` and the **TLS certificate fingerprint**.

The first start creates the server's certificate in the `tls\` folder next to `anchor.exe`. Keep that folder: with a new certificate, every player has to press *Forget Saved Certificates* once before they can connect again.

Console commands in the server window:

| Command | What it does |
|---|---|
| `list` | Shows active rooms and players, with each player's ping, traffic (KB/s), send queue, TLS and UDP |
| `sessions` | Shows all saved sessions |
| `save` | Saves all sessions right away (otherwise done automatically every 10 seconds and when the server closes) |
| `deleteSession <room>` | Deletes a saved session. It must not be active, use `deleteRoom <room>` first. |
| `messageAll <text>` | Sends a message to everyone |
| `stop` | Saves and closes the server |

### Letting friends connect over the internet

- **Port forwarding:** in your router, forward **port 43383 for both TCP and UDP** to your computer's local IP. You can find the local IP with `ipconfig`, for example `192.168.1.23`. Without the UDP forward everything still works, just over TCP.
- **Friends enter your public IP** as Host. You can see it on e.g. whatismyip.com. If you have a dynamic IP, a free DDNS service can give you a fixed name.
- **On the same network**, your local IP is enough.
- **You yourself** can enter `127.0.0.1` as Host.

### Encryption

The server accepts both encrypted and unencrypted connections on the same port. Players turn on **Encrypted Connection (TLS)** in the Anchor menu to encrypt the password and all traffic. Without it, the password is sent unencrypted: enough to keep strangers out, but don't reuse a password you use for anything important.

- The first time you connect, the game saves the server's certificate. If the certificate changes later, the game refuses to connect and says so. That protects against someone pretending to be your server.
- To be sure from the very first connection, paste the fingerprint from the server window into **Server Fingerprint (optional)**.
- If the server really was set up again (new `tls\` folder), press **Forget Saved Certificates** and connect again.
- To only allow encrypted connections, remove `rem` in front of `set ANCHOR_REQUIRE_TLS=1` in `Start server.bat`. `set ANCHOR_TLS=0` turns encryption off completely.

## 3. Connect in the game

Open **Network → Anchor** in the in-game menu. All players fill in:

- **Host & Port:** the server's address and `43383`
- **Server Password:** the password from the bat file
- **Room ID:** the same for everyone, for example `zelda-night`
- **Team ID:** the same for everyone who should share progress
- **Name & Color:** whatever you like
- **Encrypted Connection (TLS):** on, unless the server has TLS turned off
- **Use UDP For Movement:** on (the default). Turn it off only if movement behaves strangely on your network.

Then click **Enable**. Whoever creates the room first becomes the owner (🔨) and controls the **Co-op** settings.

---

## 4. Save and continue later (multiple sessions)

A session is a combination of server, **Room ID** and **Team ID**. The team's progress is saved on the server, and everyone also has their own save file.

1. Fill in the connection as usual. Type a name under **Saved Sessions**, for example "Evening with Erik", and click **Save Current**.
2. Load a save file and play. While you're connected, the menu remembers which save file you used for the session.
3. Next time, pick the session in the list and click **Use Session**, then **Enable**. Load the save file the menu shows ("Load save file 2 to continue this session").

For several games in parallel, use different Room IDs and different save files (the game has three slots per profile). When you load in, the game automatically fetches what the team has done since last time, either from a teammate who is online or from the server's saved session.

## Update rate

Under **Sync Rate** in the owner's menu:

- **Player Updates Per Second:** default 20, which means every game update and gives the smoothest movement.
- **Enemy Updates Per Second:** default 10.
- **Projectile & Object Updates Per Second:** default 20. Covers projectiles, magic, moved objects and loose items.
- **Time Of Day Sync Every … Seconds:** default 5. How often the time of day is sent.

Higher than 20 can't be chosen, and it wouldn't help. The game's logic runs 20 times per second, even if the picture is drawn more often. More packets than that would only send the same state several times and add traffic.

Lower the values if someone has a poor connection or it lags. Raise the enemy rate if enemies look too jerky.

## Room window and network debug

The room window (**Toggle Anchor Room Window**) shows, after each name: hearts, or **DOWN** for a teammate who is waiting to be revived, and the ping in milliseconds (green under 80, yellow under 150, red above). Hearts and ping need the new server.

Under **Network Debug** in the Anchor menu:

- **Show Network Details In The Room Window:** your ping, KB and packets per second in both directions, packets waiting in the queues, whether movement goes over UDP or TCP, whether the connection is encrypted, and which player leads the enemies in your room.
- **Extra Latency, Jitter, Lost Movement Updates:** makes your connection worse on purpose (only for what you receive), so you can see how the game behaves on a bad connection without having one. Only packets that may be lost on UDP (movement, enemies, sounds) are dropped. **Reset Simulation** turns it all off.

**Predict Enemy Movement** (on by default) moves enemies that follow another player on to where they should be by now, based on their speed and the ping.

## Good to know and known limitations

- **Reviving** requires the teammate to be in the same area and standing close. If you're down in an area where nobody else is, someone has to come there, otherwise it's game over when the rest of the team also dies.
- **The music** restarts after a revive. In boss rooms it may be silent until the boss music starts again.
- **Enemy movement:** one player per room leads the enemies. That's the one with the lowest ID, usually whoever joined first. For the others, enemies are pulled towards the leader's positions ten times per second, predicted forward by their speed and the ping, but keep their own AI so they can attack and be hit. When an enemy suddenly turns or stops, the prediction is a little off and gets corrected with a small jerk. The animation doesn't always match the movement. Enemies far off screen aren't updated until you get closer.
- **Enemies that belong to another enemy** (Dead Hand's hands, Gohma's eggs and others) are only spawned if the "parent" exists for you. An enemy that grabs you (Like Like, ReDead, Wallmaster) is neither moved nor killed by the sync, so you don't get stuck.
- **If you're in different rooms**, enemy movement isn't synced. Each room is led by whoever is there. Enemies killed in a room you're not in die when you get there, but only while the player who killed them is still in the room.
- **Bosses share health**, but damage another player does only shows the next time the boss is hit in your game. Some boss phases are only triggered by your own hits.
- **Bosses:** Ganondorf and Ganon aren't synced, they go straight to the ending. If you're in the middle of a boss intro, the boss is only killed once the intro is over.
- **Cutscenes** aren't shown to players who are a different age (child or adult) than the one who triggered them, since the areas differ. Someone who is down, talking to someone or has the menu open gets the cutscene once they're free again, for up to a minute. After that it's skipped. The credits are shared.
- **Rupees** that come in while you're offline are given to you when you load in, as long as nobody has saved in the meantime.
- **The ocarina:** Sun's Song changes the time for everyone, even where the song can't be played. A warp, song or cutscene that arrives while you're talking to someone, are in another cutscene or are down waits until you're free, for up to a minute. Saria's Song, Epona's Song and Zelda's Lullaby have no effects on the surroundings to sync, but what they unlock (doors, Epona, Triforce switches) is saved as flags and synced.
- **Projectiles and magic are only visual copies** for teammates, except arrows and boomerangs when *Real Teammate Arrows & Boomerangs* is on. The damage they do still reaches everyone through the enemies' shared health. A teammate's Nayru's Love only protects that teammate, and your own magic is never affected by their copies.
- **Objects in the world** are matched by their starting position. Objects spawned during play are paired with the nearest object of the same kind, within about one body length. If it's missing for you entirely it's spawned, but only if you're in the same room. Whatever you're holding is never moved by a teammate.
- **Other player actions:** movement, animations, equipment and sounds are visible to others. PvP damage works if it's on. Conversations with NPCs happen separately.
- **Solid figures:** you can't stand on top of each other or push each other forward, only block.
- **Animals:** only Cuccos and horses. Other NPCs, for example dogs, Malon and shopkeepers, move separately.
- **Song demonstrations** show the notes and play the melody, but you don't have to play it back. You get the song right away.
- **Shared age** only applies to changes in the Temple of Time. The first time someone becomes an adult, the others follow when that person returns to the Temple of Time after the Chamber of Sages.
- **Not synced yet:** the full AI and attacks of enemies and bosses, Ganondorf and Ganon, minigames and the fishing rod. They need special handling for each enemy and minigame.
- **Epona** only shows under a teammate who is riding. Two can't ride the same horse, and the horse may be missing in places where the game doesn't allow horses.
- **Shared game over** requires everyone to have the game over menu up at the same time. Anyone still on their way there gets the answer when the menu shows, for up to 30 seconds.
- **UDP** needs the UDP port forwarded on the host's router. When it doesn't get through, the game notices and uses TCP; the room window's network details show which one is in use.
- The features have been tested by compiling, **not by playing**. The connection itself (encryption, UDP, ping, the server's copies) is tested automatically against the server. Try a dungeon together and report anything that behaves strangely.

---

## Building from source

The game is built automatically by GitHub Actions on every push to the `zelda-mp-coop` branch of [Shipwright-Anchor-full-sync](https://github.com/Nahoy93/Shipwright-Anchor-full-sync). The co-op code is in `soh/soh/Network/Anchor/`. The server code is in [Anchor-full-sync-test](https://github.com/Nahoy93/Anchor-full-sync-test).
