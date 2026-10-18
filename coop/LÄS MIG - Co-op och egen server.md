# Zelda MP – co-op och egen server

Det här har lagts till i Ship of Harkinian och Anchor-servern:

| Funktion | Hur den fungerar |
|---|---|
| **Egen server med lösenord** | Du kör `anchor.exe` själv. Den som anger fel lösenord nekas. Efter 10 felaktiga försök på 5 minuter spärras den IP-adressen tillfälligt. |
| **Lagdöd** | Hälsan är individuell. Den som dör ligger kvar på marken. Står en lagkamrat bredvid i 2 sekunder återupplivas man med en fe och får 3 hjärtan. Ligger **hela laget** nere samtidigt blir det vanlig game over, och man fortsätter från checkpoint eller senaste sparning. |
| **Delad inkomst** | Rupier som någon hittar läggs till i allas plånböcker. Det man köper dras bara från den egna plånboken. |
| **Kistor** | När någon öppnar en kista får alla i laget föremålet, och kistan öppnas också hos den som står i samma rum. Kräver *Sync Items & Flags* (på från början). |
| **Fiender och bossar** | En fiende som dödas hos en spelare dör också för lagkamrater i samma område. En boss dör för alla i bossrummet, och den blå warpen dyker upp för alla. |
| **Fienders rörelser och hälsa** *(experimentell)* | Spelare i samma rum ser fienderna på samma ställen, och fienderna har gemensam hälsa, så allas träffar räknas. Fiender som skapas under spelets gång dyker upp för alla. Bossar delar bara hälsan. |
| **Cutscenes** | När någon triggar en cutscene ser lagkamraterna den också. Med inställningen *Everyone* teleporteras lagkamrater i andra områden dit först. |

| **Ocarinasånger** | Alla hör tonerna. **Sun's Song** byter tid för hela laget, **Song of Storms** ger regn och **Song of Time** flyttar tidsblocken för de som är i samma område. **Warp-sånger** tar med hela laget. Alla får en notis om vilken sång som spelades. |
| **Projektiler och föremål** | Pilar (även eld-, is- och ljuspilar), frön, Deku-nötter, bomber, bombchus, bumerang, hookshot och spin attack-virveln syns hos lagkamrater i samma område. Bomber och Bomb Flowers exploderar hos alla. Magin syns också: Din's Fire, Farore's Wind och Nayru's Love. Bikupor, rullande stenblock, isblock, roterande väggar, Goron-statyn och kobrastatyerna i Spirit Temple följer också med. Silverrupier räknas för hela laget. Krukor, lådor, stenar och buskar som lyfts, kastas eller slås sönder, gräs som klipps, block som knuffas och facklor som tänds delas. Då kan ni lösa pussel tillsammans. |
| **Spelareffekter** | Masker, eld och elchock på kroppen, brinnande Deku-pinne och hammarstötar syns på lagkamraternas figurer. Använder en lagkamrat i samma rum Lens of Truth ser du också det som linsen visar. |
| **Tid på dygnet** | Hela laget har samma tid. Den som anslöt först håller tiden. |
| **Musik** | Alla i samma område hör samma musik, inklusive bossmusik och tystnad. Den som anslöt först styr. |
| **Lösa föremål, feer, insekter och fiskar** | Hjärtan, rupier och annat som ramlar ut ur fiender, krukor och gräs syns hos alla i samma område. Feer, insekter och fiskar som dyker upp under spelet finns också hos alla. Den som tar något först får det, och då försvinner det för de andra. |
| **Farore's Wind** | Warp-punkten delas inom laget (en per ålder). Den som använder den förbrukar den för alla. |
| **Epona** | När en lagkamrat rider syns hästen under figuren. Den är bara en kopia som inte går att rida på. |
| **Riktiga pilar och bumeranger** | Lagkamraters pilar och bumeranger flyger på riktigt i ditt spel och kan träffa fiender och brytare hos dig. |
| **Plattformar och hissar** | Plattformar och hissar som rör sig är på samma ställe för alla i rummet. Den som anslöt först styr. Vattennivån i Water Temple, hissen i Forest Temple och Song of Time-blocken följer inte med, eftersom de styrs av flaggor. |
| **Fiender i andra rum** | Dödar en lagkamrat fiender i ett rum där du inte är, dör de också när du kommer dit, så länge lagkamraten är kvar i rummet. Fiender som hör till en annan fiende skapas nu under rätt "förälder" hos dig. |
| **Gemensam game over** | Ligger hela laget nere svarar den första som väljer på "Save?" och "Continue?" för hela laget. Kräver lagdöd. |
| **Bytestimers** | Timern för Odd Mushroom, Frog och Eye Drops startar och stoppar för alla som bär samma föremål. |
| **Nät, rödis, sprängbara väggar** | Spindelnät som bränns, rödis som smälts med blå eld, väggar och stenblock som sprängs och pelare som slås sönder med hammaren försvinner direkt hos lagkamrater i samma rum. |
| **Warpblocken i Spirit Temple** | När någon byter plats på ett Song of Time-warpblock byts det också hos lagkamraterna. |
| **Väder** | Regn, oväder, blixtar och mörk himmel är samma för alla i området. Den som anslöt först styr. |
| **Inlärda sånger** | När en lagkamrat lär sig en sång får alla den. Man ser också lärarens genomgång med noterna, så fort man står still och är ledig. |
| **Bossintron** | När någon går in i bossrummet förs alla i samma dungeon dit, så att hela laget ser introt och slåss tillsammans. |
| **Gemensam ålder** | Blir någon barn eller vuxen i Temple of Time följer resten av laget med till Temple of Time i samma ålder. |
| **Åskådarkamera** | Ligger du nere följer kameran en lagkamrat i samma område. Byt lagkamrat med L. Den kan stängas av under *Spectate Teammates While Down*. |
| **Fasta figurer** | Lagkamraternas figurer är solida och går inte att gå igenom. Det gäller inte när någon ligger nere. |
| **Djur** | Cuccos och hästar i området är på samma ställe för alla. Bär du en Cucco, eller har nyss kastat den, är det du som styr den. |
| **Sparade sessioner** | Servern sparar varje rums framsteg i mappen `sessions\` och laddar dem igen när någon ansluter till samma Room ID, även efter en omstart. Olika Room ID blir olika sessioner. |
| **Uppdateringsfrekvens** | Rummets ägare väljer hur många gånger per sekund spelarnas och fiendernas positioner skickas (1–20). |

Varje funktion kan slås av och på av rummets ägare under **Network → Anchor → Co-op**.

---

## 1. Bygg om spelet (soh.exe)

Ändringarna ligger i `Game SDK\soh\soh\Network\Anchor\`.

1. Bygg Game SDK på samma sätt som förra gången. De nya `.cpp`-filerna hittas automatiskt av CMake.
2. Kopiera den nya `soh.exe` till `Spelbart\`.
3. **Alla spelare måste ha exakt samma soh.exe.** Ge dina vänner din byggda fil. Klienter med olika version ignorerar varandra.

Originalfilerna innan ändringarna ligger i `Backup original\` ifall du vill gå tillbaka.

## 2. Starta servern

1. Öppna `Multiplayermod SDK\Starta server.bat` i Anteckningar.
2. Ändra `ANCHOR_PASSWORD=byt-till-ditt-losenord` till ett eget lösenord. Spara filen.
3. Dubbelklicka på `Starta server.bat`. Om Windows brandvägg frågar, klicka **Tillåt**.
4. Fönstret visar `Server running on :43383` och `Password protection: ON`.

Konsolkommandon i serverfönstret:

| Kommando | Vad det gör |
|---|---|
| `list` | Visar aktiva rum och spelare |
| `sessions` | Visar alla sparade sessioner |
| `save` | Sparar alla sessioner direkt (görs annars automatiskt var 10:e sekund och när servern stängs) |
| `deleteSession <rum>` | Raderar en sparad session. Den får inte vara aktiv, använd `deleteRoom <rum>` först. |
| `messageAll <text>` | Skickar ett meddelande till alla |
| `stop` | Sparar och stänger servern |

### Så att vänner kan ansluta över internet

- **Port forwarding:** i routern, skicka **TCP-port 43383** till din dators lokala IP. Den lokala IP:n hittar du med `ipconfig`, till exempel `192.168.1.23`.
- **Vänner anger din publika IP** som Host. Den ser du på t.ex. whatismyip.com. Har du dynamisk IP kan en gratis DDNS-tjänst ge dig ett fast namn.
- **På samma nätverk** räcker din lokala IP.
- **Du själv** kan ange `127.0.0.1` som Host.

Lösenordet skickas okrypterat. Det räcker för att hålla obehöriga borta, men använd inte samma lösenord som till något viktigt.

## 3. Anslut i spelet

Öppna **Network → Anchor** i menyn i spelet. Alla spelare fyller i:

- **Host & Port:** serverns adress och `43383`
- **Server Password:** lösenordet från bat-filen
- **Room ID:** samma för alla, till exempel `zelda-kvall`
- **Team ID:** samma för alla som ska dela progression
- **Name & Color:** valfritt

Klicka sedan på **Enable**. Den som skapar rummet först blir ägare (🔨) och styr **Co-op**-inställningarna.

---

## 4. Spara och fortsätta senare (flera sessioner)

En session är en kombination av server, **Room ID** och **Team ID**. Lagets framsteg sparas på servern, och var och en har dessutom sin egen sparfil.

1. Fyll i anslutningen som vanligt. Skriv ett namn under **Saved Sessions**, till exempel "Kväll med Erik", och klicka på **Save Current**.
2. Ladda en sparfil och spela. Medan du är ansluten kommer menyn ihåg vilken sparfil du använde för sessionen.
3. Nästa gång väljer du sessionen i listan och klickar på **Use Session**, sedan **Enable**. Ladda den sparfil som menyn visar ("Load save file 2 to continue this session").

Vill du ha flera parallella spel, använd olika Room ID och olika sparfiler (spelet har tre platser per profil). När ni laddar in hämtar spelet automatiskt det laget har klarat sedan sist, antingen från en lagkamrat som är online eller från serverns sparade session.

## Uppdateringsfrekvens

Under **Sync Rate** i ägarens meny:

- **Player Updates Per Second:** standard är 20, vilket betyder varje speluppdatering och ger mjukast rörelser.
- **Enemy Updates Per Second:** standard är 10.
- **Projectile & Object Updates Per Second:** standard är 20. Gäller projektiler, magi, flyttade föremål och lösa föremål.
- **Time Of Day Sync Every … Seconds:** standard är 5. Hur ofta tiden på dygnet skickas.

Högre än 20 går inte att välja, och det skulle inte hjälpa. Spelets logik körs 20 gånger per sekund, även om bilden ritas oftare. Fler paket än så skulle bara skicka samma läge flera gånger och ge mer trafik.

Sänk värdena om någon har dålig uppkoppling eller om det laggar. Höj fiendefrekvensen om fienderna ser för ryckiga ut.

## Bra att veta och kända begränsningar

- **Återupplivning** kräver att lagkamraten är i samma område och står nära. Ligger du nere i ett område där ingen annan är, måste någon komma dit, annars blir det game over när resten av laget också dör.
- **Musiken** startar om efter en återupplivning. I bossrum kan det vara tyst tills bossmusiken börjar igen.
- **Fiendernas rörelser:** en spelare per rum leder fienderna. Det är den med lägst ID, alltså oftast den som anslöt först. Hos övriga dras fienderna mot ledarens positioner tio gånger i sekunden, men har kvar sin egen AI så att de kan slå och bli slagna. Det kan därför se lite ryckigt ut, särskilt för snabba fiender, och animationen stämmer inte alltid med rörelsen. Fiender långt utanför skärmen uppdateras inte förrän du kommer närmare.
- **Fiender som hör till en annan** (Dead Hands händer, Gohmas ägg med flera) skapas bara om "föräldern" finns hos dig. En fiende som håller fast dig (Like Like, ReDead, Wallmaster) varken flyttas eller dödas av synken, så att du inte fastnar.
- **Står ni i olika rum** synkas inte fiendernas rörelser. Varje rum leds av den som är där. Fiender som dödas i ett rum där du inte är dör när du kommer dit, men bara medan den som dödade dem är kvar i rummet.
- **Bossar delar hälsa**, men skadan som en annan spelare gör syns först när bossen träffas nästa gång hos dig. Vissa bossfaser triggas bara av egna träffar.
- **Bossar:** Ganondorf och Ganon synkas inte, de går direkt in i slutsekvensen. Är du mitt i bossens intro dödas bossen först när introt är klart.
- **Cutscenes** visas inte för spelare som har en annan ålder (barn eller vuxen) än den som triggade, eftersom banorna skiljer sig åt. Den som ligger nere, pratar med någon eller har menyn öppen får cutscenen när den är ledig igen, i upp till en minut. Därefter hoppas den över. Eftertexterna delas inte.
- **Rupier** som kommer in medan du är offline får du när du laddar in, så länge ingen har sparat under tiden.
- **Ocarinan:** Sun's Song byter tid för alla, även där sången inte går att spela. En warp, sång eller cutscene som kommer medan du pratar med någon, har en annan cutscene eller ligger nere väntar tills du är ledig, i upp till en minut. Saria's Song, Epona's Song och Zelda's Lullaby har inga effekter i omgivningen att synka, men det de låser upp (dörrar, Epona, Triforce-brytare) sparas som flaggor och synkas. Tiden på dygnet i övrigt synkas inte.
- **Projektiler och magi är bara visuella kopior** hos lagkamraterna, utom pilar och bumeranger när *Real Teammate Arrows & Boomerangs* är på. Skadan de gör når ändå alla genom fiendernas gemensamma hälsa. En lagkamrats Nayru's Love skyddar bara lagkamraten, och din egen magi påverkas aldrig av deras kopior.
- **Föremål i världen** matchas på startposition. Föremål som skapats under spelets gång kopplas ihop med det närmaste föremålet av samma sort, inom ungefär en kroppslängd. Saknas det helt hos dig skapas det, men bara om du är i samma rum. Det du själv håller i händerna flyttas aldrig av en lagkamrat.
- **Spelarnas handlingar** i övrigt: rörelser, animationer, utrustning och ljud syns för andra. PvP-skada fungerar om den är på. Samtal med NPC:er sker var för sig.
- **Fasta figurer:** man kan inte stå ovanpå varandra eller knuffa varandra framåt, bara blockera.
- **Djur:** bara Cuccos och hästar. Andra NPC:er, till exempel hundar, Malon och butiksbiträden, rör sig var för sig.
- **Sångernas genomgång** visar noterna och spelar melodin, men du behöver inte spela tillbaka den. Sången får du direkt.
- **Gemensam ålder** gäller bara byten i Temple of Time. Första gången någon blir vuxen följer de andra med när den personen kommer tillbaka till Temple of Time, efter Chamber of Sages.
- **Inte synkat än:** fiendernas och bossarnas fullständiga AI och attacker, Ganondorf och Ganon, minispel och fiskespöet. De kräver särskild hantering för varje fiende och minispel.
- **Epona** syns bara under en lagkamrat som rider. Det går inte att rida två på samma häst, och hästen kan saknas på platser där spelet inte tillåter hästar.
- **Gemensam game over** kräver att alla har game over-menyn uppe samtidigt. Den som fortfarande är på väg dit får svaret när menyn visas, i upp till 30 sekunder.
- Funktionerna är testade genom kompilering men **inte genom spelande**. Testa gärna en dungeon tillsammans, och hör av dig om något beter sig konstigt.
