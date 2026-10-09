# gta-sa-mcp

Serveur MCP pour **GTA: San Andreas** : il permet Ã  Claude Code, Codex ou tout client MCP d'observer,
piloter et modifier le jeu en direct, avec des donnÃ©es structurÃ©es **et** des captures d'Ã©cran.

Boucle visÃ©e : **observer â†’ capturer â†’ analyser â†’ placer/modifier â†’ vÃ©rifier visuellement â†’ corriger â†’ sauvegarder.**

```
 agent (Claude Code, Codexâ€¦)
        â”‚  MCP, stdio (JSON-RPC)
 mcp/   serveur Node, sans dÃ©pendance : schÃ©mas des tools, images, lancement du jeu
        â”‚  named pipe \\.\pipe\gta-sa-mcp (une ligne JSON par requÃªte/rÃ©ponse)
 asi/   gta-sa-mcp.asi injectÃ© dans gta_sa.exe : exÃ©cute les commandes sur le thread du jeu
```

## Ce que fait le plugin

- **Quasi-headless** : fenÃªtre sans bordure (pas de plein Ã©cran), rendu actif mÃªme sans le focus, ne
  prend ni la souris ni le clavier du bureau ; la fenÃªtre peut Ãªtre placÃ©e hors Ã©cran (`x=-3000`).
- **DÃ©marrage sans intervention** : vidÃ©os d'intro passÃ©es, nouvelle partie lancÃ©e toute seule, monde
  Â« bac Ã  sable Â» (pas de `main.scm`, pas de trafic, joueur invincible et non affichÃ©, HUD masquÃ©).
  Tout est rÃ©glable dans `gta-sa-mcp.ini`.
- **Sous SA-MP**, le plugin n'automatise rien (dÃ©marrage, scripts, joueur) : il se contente d'observer
  et d'Ã©diter.

## Tools

| Domaine | Tools |
| --- | --- |
| Session | `get_status`, `launch_game` |
| Observation | `get_player`, `get_camera`, `get_nearby_entities`, `get_entity`, `get_entity_bounds`, `search_models`, `get_model_info` |
| Vision | `take_screenshot`, `capture_scene` |
| DÃ©placement | `teleport`, `set_camera`, `look_at`, `set_world` |
| GÃ©omÃ©trie | `raycast`, `screen_to_world`, `world_to_screen`, `get_ground_z`, `is_position_free` |
| Ã‰dition | `create_object`, `move_object`, `rotate_object`, `delete_object`, `clone_object`, `select_object`, `list_objects` |
| Historique et fichiers | `undo`, `redo`, `save_changes`, `load_changes` |
| Rechargement Ã  chaud | `load_model`, `restore_model` |

`capture_scene` renvoie en un appel : la capture, la camÃ©ra (position, cap/tangage, FOV), les entitÃ©s
visibles (rÃ©fÃ©rence, modÃ¨le, position, rotation, boÃ®te englobante, rectangle Ã  l'Ã©cran, masquÃ©e ou non),
l'objet sÃ©lectionnÃ© et des sondes de collision (centre de l'Ã©cran, grille de points, sol sous la camÃ©ra).

`load_model` remplace le DFF, le TXD et/ou la collision d'un modÃ¨le par des fichiers sur disque, sans
relancer le jeu : on exporte depuis Blender, on recharge, on regarde la capture, on corrige.

`save_changes` Ã©crit `<jeu>\gta-sa-mcp\scenes\<nom>.json` (rechargeable avec `load_changes`) et peut
exporter en `pawn` (lignes `CreateObject` pour SA-MP/open.mp) ou en `ipl`.

## Installation

PrÃ©requis : GTA San Andreas **1.0 US** (`gta_sa.exe` de 14 383 616 octets), un chargeur d'ASI
(Silent's ASI Loader, CLEOâ€¦) et Node.js 18+.

1. TÃ©lÃ©charger l'artefact `gta-sa-mcp-asi` du dernier build
   ([Actions](../../actions/workflows/build.yml)) â€” le plugin n'est compilÃ© que par la CI.
2. Copier `gta-sa-mcp.asi` et `gta-sa-mcp.ini` dans le dossier du jeu :
   ```powershell
   .\scripts\install.ps1 -GameDir "C:\Jeux\GTA San Andreas" -From .\gta-sa-mcp-asi
   ```
3. DÃ©clarer le serveur MCP, par exemple dans `.mcp.json` (Claude Code) :
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
   Avec `GTA_SA_DIR`, le tool `launch_game` dÃ©marre le jeu lui-mÃªme.

La taille de la fenÃªtre est la rÃ©solution choisie dans les options d'affichage du jeu.

## Conventions

- Positions `[x, y, z]` en mÃ¨tres, `z` vers le haut, `+y` au nord, `+x` Ã  l'est.
- Caps en degrÃ©s, `0` = nord, sens antihoraire. Tangage positif vers le haut.
- Rotations d'objet `[rx, ry, rz]` en degrÃ©s, convention de `CreateObject` (SA-MP) et de MTA.
- CoordonnÃ©es Ã©cran en pixels du jeu depuis le coin haut-gauche ; une capture rÃ©duite indique
  `image_to_screen_scale`.
- EntitÃ©s du monde : rÃ©fÃ©rence du type `"building:118272"`. Objets crÃ©Ã©s : `object_id`.
- Les collisions (`raycast`, `get_ground_z`, `snap_to_ground`, `is_position_free`) n'existent qu'autour
  du joueur : `teleport` prÃ¨s de la zone de travail. La camÃ©ra, elle, peut Ãªtre n'importe oÃ¹.

## DÃ©veloppement

Voir [docs/architecture.md](docs/architecture.md) : organisation du code, protocole du pipe, et comment
ajouter un tool (une fonction cÃ´tÃ© ASI, une description cÃ´tÃ© MCP).

- ASI: C++17, MSVC Win32; GitHub Actions or a local development build (below).
- MCP : `cd mcp && npm test` (tests contre un faux plugin, aucun jeu requis).
- Essai d'un tool sur le jeu lancÃ©, sans client MCP : `node mcp/scripts/call.mjs capture_scene '{}' shot`.

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


### Windowed device-reset trials

`reset_windowed_device` runs GTA's native video-mode recreation on the game
thread, including RenderWare and SA-MP loss/restore callbacks. It requires an
in-game session, verified GTA code, `windowed=1` and `no_activate=1`. It forces
recreation of the current mode without requesting exclusive fullscreen.

The result reports the actual `reset_calls` and last `hresult`. For a single
successful reset, require exactly one call and HRESULT 0, then inspect captures
and game progress. A rollback can issue another Reset, so a final successful
HRESULT alone is insufficient. Status exposes cumulative `reset_count` and
`last_reset_hresult`; these are instrumentation, not visual acceptance.

The no-activation guard also intercepts game/SA-MP `SetWindowPos` calls, retaining
the configured off-screen coordinates for the game window. This test does not
reproduce Alt+Enter, exclusive fullscreen or a third-party window-mode plugin.
A paired DL-R1/replacement trial recreated three model previews, preserved a
style-4 sprite and retained local movement after one successful Reset per client.
The frontend-menu-active branch has not been exercised by this command's trials.


`get_status.reset_history` retains the latest 64 Reset attempts, oldest first.
Each entry records a monotonic `ordinal`, the start `tick_ms` from Windows
GetTickCount (which wraps after about 49.7 days), whether parameters were present,
and the requested and forwarded width, height, and windowed flag. The requested
values precede the plugin's overrides; forwarded values are copied immediately
before the native Reset call, before the driver can modify them.

An entry is published before the call, with `completed: false` and `hresult: null`.
After return it records that individual HRESULT, including failed attempts before
a successful rollback. The log also writes a begin/end pair for each attempt.
History snapshots are copied under a mutex and can be read while a Reset is in
progress. The existing `reset_count` still counts returned calls; its atomic
counters and the history are not one combined transaction, so a status request
at a completion boundary may briefly show different counts. This diagnostic
history does not establish that rendering or game resources recovered correctly.

Reset forwards the native requested back-buffer width and height unchanged.
The plugin only forces windowed mode and clears the fullscreen refresh rate;
it does not replace dimensions with GTA's potentially stale screen globals.
After a successful Reset, FitWindow uses the returned presentation parameters.
The requested/forwarded history remains available to distinguish these overrides
from native resize requests. CreateDevice startup is unchanged, and a same-mode
reset keeps the dimensions requested by GTA. A successful HRESULT at the old
size is not evidence that a requested resize took effect.

`get_status.resolution` reports GTA logical screen globals, not a fresh D3D
back-buffer query. After an external HWND resize those globals can lag the real
window/back buffer. Use screenshot image metadata and the Reset history to
inspect the actual capture dimensions; do not infer complete UI resize parity
from the status field or a successful Reset alone.
