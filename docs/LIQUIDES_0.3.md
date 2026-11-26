# Mindustry 3DS 0.3 — fondations des liquides (eau)

Lot unique : pompe mécanique, conduite, réservoir, eau et générateur à vapeur, avec les règles de Mindustry v146.
Validé par 110 vérifications hôte (88 existantes et 22 nouvelles) et par les scènes de contrôle. **Pas encore validé sur console.**

## Règles reprises des sources v146

| Élément | Source | Valeurs et règles |
|---|---|---|
| Eau | `Liquids.java` | couleur `596ab8`. Table `kLiquids` générée (`LIQUIDS` dans `tools/build_assets.py`), extensible |
| Sols | `Blocks.java`, `ShallowLiquid.set()` | `liquidDrop` = eau ; `liquidMultiplier` 1 (eau peu profonde), 1,5 (eau profonde) |
| Pompe mécanique | `Pump` | 7/60 par tick et par case × `liquidMultiplier`, capacité 10. Placement : au moins une case de liquide et un seul liquide (`canPlaceOn`). `floating`, donc possible sur eau profonde. S'arrête une fois pleine (`shouldConsume`), vide par `dumpLiquid` |
| Conduite | `Conduit` | capacité 10 ; chaque tick, `moveLiquidForward` (débit par écart de remplissage, `liquidPressure` 1). Refuse le liquide venant de sa sortie. Fuit à une extrémité ouverte (÷1,5 par tick) ; les flaques ne sont pas portées, la fuite est perdue. Raccords par `Autotiler` (`Conduit.blends`) |
| Réservoir | `LiquidRouter` « liquid-tank » | 3×3, capacité 1 800, `dumpLiquid` (échelle 2) vers les voisins |
| Générateur à vapeur | `ConsumeGenerator` | 5,5 par tick × inflammabilité, 0,1 d'eau par tick, un combustible toutes les 90 ticks. Efficacité = min(combustible, eau stockée / besoin) (`ConsumeLiquid.efficiency`) |

Sprites officiels :
- conduite : `conduit-bottom-N` multiplié par `565656` (`botColor`), puis le liquide, puis `conduit-top-N` ;
- réservoir : fond, liquide, région ;
- pompe : région, puis `-liquid` teinté selon le remplissage ;
- vapeur : `-top` de chauffe, deux `-turbine` tournant en sens opposés (`rotation` 45 pour la seconde), `-cap`, `-liquid`.

Les icônes suivent `icons()`. Le liquide est dessiné avec l'opacité du remplissage, comme `Drawf.liquid`.

## Fiches

Elles affichent :
- le liquide et la quantité / capacité ;
- le débit de pompage ;
- la production électrique de la vapeur ;
- les causes d'arrêt : *sans combustible*, *manque d'eau (x %)*, *réservoir plein*, *aucun liquide sous la pompe*.

## Sauvegardes

- **Format 2** : il embarque la table des noms de blocs (les identifiants sont remappés par nom au chargement) et la quantité de liquide de chaque bâtiment.
- **Format 1 (0.2)** : toujours lu, grâce à `tools/legacy_blocks_v1.txt`, la table des 80 blocs de la 0.2. Le test s'appuie sur `tests/fixtures/save_v1_0.2.sav`, une vraie sauvegarde produite par le code de la 0.2 (`tests/fixtures/make_save_v1.cpp`).
- **Sauvegarde illisible** : la partie ne démarre pas et le fichier reste intact. Avant d'écrire une nouvelle partie (X), l'ancien fichier est renommé `.prev` s'il est lisible, `.unreadable-<ms>` sinon : il n'est jamais écrasé.
- Title ID et dossier `SD:/3ds/mindustry-3ds/` inchangés.

## Coût Old 3DS

- Mise à jour limitée aux bâtiments de liquide, sans allocation ni parcours de carte. La pompe ne recalcule son sol qu'à `updateProximity`.
- Chaque conduite ajoute une région, un rectangle et une région au dessin.
- Atlas unique, 512×1024, 358 sprites.

## Limites connues

- Un seul liquide par bâtiment. Mindustry garde un montant par liquide, mais une conduite n'en accepte qu'un à la fois : suffisant pour l'eau. Le mélange de liquides et les réactions sont à faire avec les liquides suivants.
- Les montants sont en flottants 32 bits : la conservation est exacte à 10⁻⁴ près en relatif.
- Ne sont pas portés : jonction et pont liquides, pompes rotatives, bonus d'eau des foreuses, flaques.
