# gta-sa-mcp

Serveur MCP pour **GTA: San Andreas** : il permet à Claude Code, Codex ou tout client MCP d'observer,
piloter et modifier le jeu en direct, avec des données structurées **et** des captures d'écran.

Boucle visée : **observer → capturer → analyser → placer/modifier → vérifier visuellement → corriger → sauvegarder.**

```
 agent (Claude Code, Codex…)
        │  MCP, stdio (JSON-RPC)
 mcp/   serveur Node, sans dépendance : schémas des tools, images, lancement du jeu
        │  named pipe \\.\pipe\gta-sa-mcp (une ligne JSON par requête/réponse)
 asi/   gta-sa-mcp.asi injecté dans gta_sa.exe : exécute les commandes sur le thread du jeu
```

## Ce que fait le plugin

- **Quasi-headless** : fenêtre sans bordure (pas de plein écran), rendu actif même sans le focus, ne
  prend ni la souris ni le clavier du bureau ; la fenêtre peut être placée hors écran (`x=-3000`).
- **Démarrage sans intervention** : vidéos d'intro passées, nouvelle partie lancée toute seule, monde
  « bac à sable » (pas de `main.scm`, pas de trafic, joueur invincible et non affiché, HUD masqué).
  Tout est réglable dans `gta-sa-mcp.ini`.
- **Sous SA-MP**, le plugin n'automatise rien (démarrage, scripts, joueur) : il se contente d'observer
  et d'éditer.

## Tools

| Domaine | Tools |
| --- | --- |
| Session | `get_status`, `launch_game` |
| Observation | `get_player`, `get_camera`, `get_nearby_entities`, `get_entity`, `get_entity_bounds`, `search_models`, `get_model_info` |
| Vision | `take_screenshot`, `capture_scene` |
| Déplacement | `teleport`, `set_camera`, `look_at`, `set_world` |
| Géométrie | `raycast`, `screen_to_world`, `world_to_screen`, `get_ground_z`, `is_position_free` |
| Édition | `create_object`, `move_object`, `rotate_object`, `delete_object`, `clone_object`, `select_object`, `list_objects` |
| Historique et fichiers | `undo`, `redo`, `save_changes`, `load_changes` |
| Rechargement à chaud | `load_model`, `restore_model` |

`capture_scene` renvoie en un appel : la capture, la caméra (position, cap/tangage, FOV), les entités
visibles (référence, modèle, position, rotation, boîte englobante, rectangle à l'écran, masquée ou non),
l'objet sélectionné et des sondes de collision (centre de l'écran, grille de points, sol sous la caméra).

`load_model` remplace le DFF, le TXD et/ou la collision d'un modèle par des fichiers sur disque, sans
relancer le jeu : on exporte depuis Blender, on recharge, on regarde la capture, on corrige.

`save_changes` écrit `<jeu>\gta-sa-mcp\scenes\<nom>.json` (rechargeable avec `load_changes`) et peut
exporter en `pawn` (lignes `CreateObject` pour SA-MP/open.mp) ou en `ipl`.

## Installation

Prérequis : GTA San Andreas **1.0 US** (`gta_sa.exe` de 14 383 616 octets), un chargeur d'ASI
(Silent's ASI Loader, CLEO…) et Node.js 18+.

1. Télécharger l'artefact `gta-sa-mcp-asi` du dernier build
   ([Actions](../../actions/workflows/build.yml)) — le plugin n'est compilé que par la CI.
2. Copier `gta-sa-mcp.asi` et `gta-sa-mcp.ini` dans le dossier du jeu :
   ```powershell
   .\scripts\install.ps1 -GameDir "C:\Jeux\GTA San Andreas" -From .\gta-sa-mcp-asi
   ```
3. Déclarer le serveur MCP, par exemple dans `.mcp.json` (Claude Code) :
   ```json
   {
     "mcpServers": {
       "gta-sa": {
         "command": "node",
         "args": ["C:/chemin/vers/gta-sa-mcp/mcp/src/index.js"],
         "env": { "GTA_SA_DIR": "C:/Jeux/GTA San Andreas" }
       }
     }
   }
   ```
   Avec `GTA_SA_DIR`, le tool `launch_game` démarre le jeu lui-même.

La taille de la fenêtre est la résolution choisie dans les options d'affichage du jeu.

## Conventions

- Positions `[x, y, z]` en mètres, `z` vers le haut, `+y` au nord, `+x` à l'est.
- Caps en degrés, `0` = nord, sens antihoraire. Tangage positif vers le haut.
- Rotations d'objet `[rx, ry, rz]` en degrés, convention de `CreateObject` (SA-MP) et de MTA.
- Coordonnées écran en pixels du jeu depuis le coin haut-gauche ; une capture réduite indique
  `image_to_screen_scale`.
- Entités du monde : référence du type `"building:118272"`. Objets créés : `object_id`.
- Les collisions (`raycast`, `get_ground_z`, `snap_to_ground`, `is_position_free`) n'existent qu'autour
  du joueur : `teleport` près de la zone de travail. La caméra, elle, peut être n'importe où.

## Développement

Voir [docs/architecture.md](docs/architecture.md) : organisation du code, protocole du pipe, et comment
ajouter un tool (une fonction côté ASI, une description côté MCP).

- ASI: C++17, MSVC Win32; GitHub Actions or a local development build (below).
- MCP : `cd mcp && npm test` (tests contre un faux plugin, aucun jeu requis).
- Essai d'un tool sur le jeu lancé, sans client MCP : `node mcp/scripts/call.mjs capture_scene '{}' shot`.

## SA-MP background testing

Use the normal SA-MP launcher, not `launch_game` (which launches single-player).
The plugin leaves SA-MP's scripts/startup and outer game-process hook alone.
Queued gameplay commands run on the game thread at the frame boundary instead.
Capture reads the Direct3D back buffer; 10-bit `A2R10G10B10` displays are supported.

For a dedicated off-screen test installation:

```ini
[window]
windowed=1
x=-3000
y=0
no_activate=1
run_in_background=1
[input]
isolate_physical=1
[startup]
auto_start=0
scripts=1
[world]
traffic=1
invincible=0
hud=1
```

Use one window-mode plugin at a time. Each client needs a distinct `[ipc] pipe`
value and the matching `GTA_SA_MCP_PIPE` environment variable in its MCP process.

`set_game_input` applies raw GTA controller axes/buttons to **local pad 0** after
native `UpdatePads`, without `SendInput`, foreground activation or teleporting.
For example, `{"left_y":-128,"frames":90,"timeout_ms":5000}` requests forward
movement. Axes are -128..128; leases are 1..300 simulation updates and at most
10 seconds. Poll `get_game_input` for actual applied frames and cancellation;
`release_game_input` requests a neutral sample on the next simulation update.
The client need not remain connected for a lease to expire.

Button names are raw controller fields (`square`, `cross`, `circle`, `triangle`,
shoulder buttons and D-pad), not universal action names: GTA control mode affects
what they do. The menu and remote-player control context are excluded. Optional
physical-input isolation neutralizes the local pad between commands. It does
not suppress SA-MP shortcuts that poll OS keys directly, nor isolate every mouse
camera path. Horn/steering history and last-input timers computed inside the
native update are not synthesized. Do not claim complete hardware-input fidelity.
These commands are test instrumentation, not changes to the replacement samp.dll.

### Local development build

With Visual Studio 2022 C++ tools, Windows SDK and CMake installed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32
cmake --build build --config Release
cd mcp
npm test
```

The ASI is `build/Release/gta-sa-mcp.asi`. Restart GTA after replacing it.

`no_activate=1` also intercepts GTA/SA-MP's own cursor confinement, recentering,
capture and focus imports. It does not hook user32 globally or repeatedly release
another application's cursor confinement. `get_status.blocked_mouse_calls` records
intercepted mouse requests. Windows are created with `WS_EX_NOACTIVATE`.
Captures are taken before the real device's Present, after the SA-MP proxy draws
its UI; `get_status.capture_at_present` exposes that capture route. A status
response alone never proves the game rendered or that an input moved the player.
