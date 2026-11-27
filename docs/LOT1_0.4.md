# 0.4 — lot 1 : transport et liquides

Valeurs et règles de Mindustry v146. Les blocs sont ajoutés par `BUILDABLE` / `VARIANT` dans `tools/build_assets.py`.

| Bloc | Classe v146 | Règles portées |
|---|---|---|
| Déchargeur | `Unloader` | 1 objet toutes les 60/11 ticks. La source est le voisin déchargeable (noyau, conteneur, usine, foreuse, générateur) le plus chargé ; la cible, le voisin qui accepte, hors stockage, le moins chargé. Filtre configurable |
| Tapis blindé | `ArmoredConveyor` | vitesse 0,08 (11/s affichés) ; n'accepte que d'un convoyeur ou par l'arrière |
| Pont de phase | `ItemBridge` | portée 12, 0,3 énergie/tick. Le `warmup` suit l'efficacité sur 30 ticks ; 1 objet toutes les 2 ticks efficaces |
| Routeur liquide | `LiquidRouter` | capacité 20, `dumpLiquid` |
| Jonction liquide | `LiquidJunction` | `getLiquidDestination` : traverse tout droit sans stocker, enchaînable |
| Pont de conduites / pont de phase liquide | `LiquidBridge` | portée 4 / 12 (12 alimenté, 0,3/tick) ; `moveLiquid` si `warmup` ≥ 0,25 ; vidange en bout de chaîne à l'échelle 1 |
| Conduite pulsée | `Conduit` | capacité 16, pression 1,025 |
| Conduite blindée | `ArmoredConduit` | 16 / 1,025 ; pas de fuite ; acceptation limitée comme en v146 |
| Pompe rotative / à impulsion | `Pump` | 2×2, 0,2/tick/case, 0,3 énergie / 3×3, 0,22, 1,3 énergie ; pompage × efficacité électrique ; pas de consommation quand pleine |

Corrections révélées par les tests :
- Les conduites appliquent `Tile.absoluteRelativeTo`, qui donne une direction **même à distance**. C'est nécessaire au passage par une jonction.
- Le trieur et le déchargeur se règlent au tactile : grille de 16 objets dans la palette pendant la configuration ; toucher à nouveau l'objet le retire du filtre. L/R reste disponible.

Transverse :
- **Autosauvegarde réglable** dans le menu pause (X : 2, 5 ou 10 minutes, ou désactivée ; 5 par défaut).
- **Copie récupérable** : chaque sauvegarde garde la précédente en `.prev`. Si une sauvegarde est illisible, la `.prev` est chargée et le fichier illisible est mis de côté, jamais écrasé.

Tests PC : 130 vérifications.
- Les 110 existantes.
- 18 pour le lot 1 : filtre, débit et conservation du déchargeur ; refus latéral du tapis blindé ; pont de phase sans puis avec énergie ; croisement par jonction (aucun mélange, jonction vide) ; conservation et absence de fuite ; saturation de la conduite blindée ; portée et conservation du pont de conduites ; pompe rotative sans puis avec énergie (4 × 0,2) ; sauvegarde puis rechargement, avec le flux qui reprend.
- 2 pour une sauvegarde 0.3 réelle (`tests/fixtures/save_v2_0.3.sav`, produite par le code `3a7cf8a`).

Coût hôte (`tests/bench.cpp`) : 2 311 bâtiments (1 710 tapis, 540 conduites) = 29 µs par tick sur PC. Ce n'est **pas** une mesure console : relever les chiffres du menu pause sur une base chargée.

Non porté dans ce lot : **tapis plastanium** (`StackConveyor`, transport par piles de 10), reporté dans `docs/REPRISE.md`.
