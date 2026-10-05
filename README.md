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

- ASI : C++17, MSVC Win32, compilé uniquement par GitHub Actions (`.github/workflows/build.yml`).
- MCP : `cd mcp && npm test` (tests contre un faux plugin, aucun jeu requis).
- Essai d'un tool sur le jeu lancé, sans client MCP : `node mcp/scripts/call.mjs capture_scene '{}' shot`.
