# Zelda MP Co-op – changes

## 1.2

Everyone needs the new game **and** the new server (Anchor-full-sync-test). An older server still works with the new
game, but without encryption, UDP, ping and the server-side copies.

### Connection and latency

- **Encrypted connection (TLS).** New option *Encrypted Connection (TLS)* in the Anchor menu. The server accepts
  encrypted and unencrypted connections on the same port and creates a self-signed certificate in `tls\` on its first
  start, printing its fingerprint. The game remembers the certificate on the first connection (like SSH) and refuses
  the server if it changes, or checks it against *Server Fingerprint (optional)* when one is filled in.
  The fingerprint can be pasted with or without colons, or as the whole line from the server window; one that can't be
  read stops the connection instead of trusting any certificate. *Forget Saved Certificates* clears what the game
  remembered. Server options: `ANCHOR_REQUIRE_TLS=1` (only
  encrypted connections) and `ANCHOR_TLS=0` (no encryption).
- **UDP for movement.** Player positions, enemy snapshots, projectiles, loose items' positions and sounds go over UDP
  when it works, so a lost packet no longer holds everything else up. The UDP port is the same number as the TCP port.
  Over TLS the datagrams are encrypted too (AES-128-GCM with a key that is only sent over TLS), and old or repeated
  datagrams are dropped. When nothing arrives over UDP for 15 seconds, the game goes back to TCP and keeps trying.
  Anything that must not get lost (items, flags, saves, deaths...) always stays on TCP. Option: *Use UDP For Movement*.
- **The server makes the copies.** A packet for several players (positions, sounds, ocarina notes, projectiles,
  objects, drops) is sent once with a list of receivers, and the server hands out the copies. With four players
  that's a third of the upload it used to be.
- **Packets go out together.** The game and the server write everything that is waiting in one go instead of one
  packet at a time.
- **Received packets are used sooner.** They are now handled at the start of each game update, before the players
  and enemies move, instead of at the end: up to 50 ms less delay for everything.
- **Ping and reconnect.** The game pings the server every 5 seconds. A connection that has been silent for
  15 seconds is made again by itself (switching Wi-Fi, a router that dropped the connection). Failed connection
  attempts wait 1, 2, 4 and then 5 seconds before the next one, also when the server closes the connection right
  away.
- **Enemy prediction.** Followers measure each enemy's speed from the last two snapshots and move it on to where it
  should be by now (at most 6 game updates ahead). Option: *Predict Enemy Movement*.

### Game

- **Room window:** everyone's ping, teammates' hearts and **DOWN** for a teammate waiting to be revived. A teammate who
  is down blinks red on the minimap.
- **Network debug** (Anchor menu → *Network Debug*): ping, KB and packets per second, queues, UDP/TCP, TLS and who
  leads the enemies in the room window, plus simulated latency, jitter and lost movement updates.
- **Smoother picture by default:** *Match Refresh Rate* is on, so the game is drawn at the monitor's refresh rate.
  It can be turned off under Settings → Graphics.

### Fixes

- Spider webs, red ice, bombable walls, boulders and hammer pillars open with their real animation and sound again.
  1.1 removed them straight away, on top of Anchor's own handling.
- Hits from several players on the same enemy at the same time all count (the damage is sent, not just the health
  that was left).
- Two cutscene teleports right after each other could leave falling into the void without damage for the rest of the
  session.
- Teammates' figures that were removed are no longer pointed at afterwards.
- Connecting and disconnecting no longer touch the game from the network thread.
- Projectile copies whose "gone" update got lost disappear by themselves after a moment.
- Server: a player joining at the same moment as another could briefly be told the wrong player id.

### Server

- `list` shows each player's ping, traffic in and out (KB/s), send queue, TLS and UDP.
- `compose.yml` maps the UDP port and keeps `sessions/` and `tls/` between rebuilds.

### Builds

- Pushes to the co-op branches only build Windows. Put `[full build]` in the commit message to also build Linux and
  macOS. The release branch no longer starts a build of its own.
