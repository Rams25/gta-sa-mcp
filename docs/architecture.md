# Architecture

## Vue d'ensemble

Deux programmes, un protocole entre les deux :

| Partie | Rôle | Ne fait pas |
| --- | --- | --- |
| `asi/` (`gta-sa-mcp.asi`, C++) | Lit et modifie la mémoire du jeu, sur le thread du jeu | Rien de spécifique à MCP |
| `mcp/` (Node) | Parle MCP à l'agent, décrit les tools, transforme les captures en images, lance le jeu | Aucune logique de jeu |

Un tool MCP sans traitement particulier est transmis tel quel à la commande du plugin de même nom.

## Plugin (`asi/src`)

```
main.cpp        enregistre les commandes, installe les hooks, démarre le pipe
core/
  memory        écriture dans le code du jeu, hooks d'appel / d'import / de vtable
  config        gta-sa-mcp.ini
  ipc           named pipe : un thread par client, une ligne JSON par message
  dispatcher    registre des commandes + file d'attente vers le thread du jeu
  bootstrap     hooks de la boucle principale, démarrage automatique, monde bac à sable
game/
  sdk           adresses et structures de gta_sa.exe 1.0 US (entités, pools, monde, streaming, objets)
  camera        caméra rendue (lecture, projection, rayon écran) et caméra fixe
  catalog       noms des modèles, lus dans les .ide du jeu
  assets        rechargement à chaud d'un modèle (DFF, TXD, COL) depuis des fichiers
render/
  window        mode fenêtré (hook Direct3DCreate9 → CreateDevice/Reset)
  capture       copie du back buffer, réduction, encodage JPEG/PNG
editor/
  scene         objets créés, undo/redo, sauvegarde/chargement/exports
commands/       un fichier par domaine ; chacun enregistre ses commandes
```

### Threads et phases

La mémoire du jeu n'est touchée que depuis le thread du jeu. Un thread de pipe met la requête en file et
attend ; le thread du jeu l'exécute à l'un de ces deux moments :

| Phase | Où | Pour quoi |
| --- | --- | --- |
| `Tick` | après `CGame::Process` (hook de l'appel en `0x53E981`) | lire et modifier le monde |
| `Frame` | avant `RsCameraShowRaster` (`0x53EC01`, `0x53E888` dans les menus) | captures : l'image est finie mais pas encore affichée |
| `Direct` | sur le thread du pipe | `get_status`, qui doit répondre même pendant un chargement |

Après une commande qui modifie la scène, les suivantes attendent `settle_frames` images (3 par défaut) :
une capture demandée juste après un `create_object` ou un `set_camera` montre donc bien le résultat.

Une exception levée par le jeu pendant une commande (pointeur invalide…) est interceptée et renvoyée
comme erreur `game_exception` : la requête échoue, le jeu continue.

### Démarrage sans intervention (`bootstrap`)

| Adresse | Effet |
| --- | --- |
| `0x748A8D`, `0x53BC78` | pas de pause ni de menu quand la fenêtre perd le focus |
| `0x6194A0` | `RsMouseSetPos` neutralisé : le jeu ne recentre plus la souris du bureau |
| `0x748B00`, `0x748BF9`, `0x748B17` | les vidéos (logo, intro) ne sont pas jouées et leurs états sont passés ; les fondus des écrans sponsors sont supprimés |
| `0x748CC2` | au menu principal, déclenche « nouvelle partie » comme le ferait le menu |
| `0x53BCC9`, `0x53BE8D`, `0x53BFC7` | remplacent `CTheScripts::Process` (au chargement puis à chaque image) : le joueur est créé par le plugin à la place de `main.scm` |

Rien de tout cela n'est installé si `samp.dll` est chargé.

Le mode fenêtré passe par `CreateWindowExA` (import de l'exe) : à la création de la fenêtre, le plugin
crée un `IDirect3D9` à lui pour patcher la vtable partagée (`CreateDevice`, puis `Reset` du device). Le
jeu résout `Direct3DCreate9` lui-même, sans import remplaçable.

### Caméra

`set_camera` et `look_at` utilisent la caméra fixe du jeu (`SetCamPositionForFixedMode` +
`TakeControlNoEntity`, comme les opcodes de script) : le jeu continue de gérer le rendu et le streaming
autour d'elle. Le FOV de ce mode est une constante dans `CCam::Process_Fixed` (`0x51D5B1`), réécrite à la
demande. `get_camera`, `world_to_screen` et `screen_to_world` lisent la caméra RenderWare réellement
rendue (matrice + `viewWindow`), donc restent justes quel que soit le mode.

### Rechargement à chaud (`game/assets`)

`load_model` court-circuite le streaming pour un modèle : il supprime les instances 3D des entités qui
l'utilisent, décharge le modèle, charge le TXD dans un emplacement à lui (`mcp_<id>`), lit le DFF avec
`CFileLoader::LoadAtomicFile`, puis marque le modèle « chargé, requis par le jeu » pour que le streaming
ne le recharge ni ne le décharge. La collision est lue avec le chargeur COL2/COL3 du jeu dans le
`CColModel` existant ; comme le jeu recharge les collisions de zone en se déplaçant, elle est réappliquée
automatiquement si elle a été écrasée. `transparent` pose l'indicateur « dessiner en dernier » du modèle
et de ses entités, nécessaire dès qu'il contient du vitrage.

## Protocole du pipe

`\\.\pipe\gta-sa-mcp` (nom réglable), octets, UTF-8, un message JSON par ligne.

```json
→ {"id": 7, "method": "create_object", "params": {"model": 1337, "position": [2490, -1670, 13.3]}}
← {"id": 7, "result": {"object_id": 1, "entity": "object:2817", "model": 1337, ...}}
← {"id": 7, "error": {"code": "unknown_model", "message": "model 99999 does not exist"}}
```

Codes d'erreur courants : `bad_params`, `unknown_method`, `game_not_ready`, `timeout`, `unknown_entity`,
`unknown_object`, `unknown_model`, `spawn_failed`, `capture_failed`, `game_exception`.

Les captures reviennent dans `result.image` (`base64`, `mime_type`, dimensions) ; le serveur MCP en fait
un bloc `image`.

## Ajouter un tool

1. **Plugin** — dans le fichier `commands/` du domaine, écrire `json MaCommande(const json& params)` et
   l'enregistrer : `dispatcher::Register("ma_commande", Phase::Tick, MaCommande, /*mutates*/ true);`
   Lever `CommandError("code", "message")` pour une erreur. Les nouvelles adresses du jeu vont dans
   `game/sdk`, pas dans la commande.
2. **MCP** — décrire le tool dans `mcp/src/tools/` : `tool('ma_commande', 'description', { ...schéma })`.
   Sans `handler`, les arguments sont transmis tels quels.

## Limites connues

- Dans le bac à sable, le joueur existe (position, collisions, streaming) mais son modèle n'est pas
  affiché : les vêtements de CJ sont normalement construits par `main.scm`.
- Une seule version du jeu : 1.0 US. Sur un autre exécutable, les hooks ne s'installent pas (voir
  `gta-sa-mcp.log`).
- La taille de la fenêtre suit la résolution choisie dans le jeu ; elle n'est pas encore réglable par l'ini.
- Les objets créés sont statiques (pas de physique) et n'existent que le temps de la session : ils se
  rechargent avec `load_changes`.
- Les entités de la carte ne sont pas déplaçables ni masquables : seuls les objets créés le sont. Leur
  modèle, lui, peut être remplacé (`load_model`).
- `load_model` ne remplace que des modèles existants (objets statiques) ; il ne crée pas de nouvel
  identifiant de modèle.
