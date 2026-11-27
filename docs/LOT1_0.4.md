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

# 0.4 — lot 2a : foreuses avancées, bonus d'eau, diode, grande batterie

| Bloc | Classe v146 | Règles portées |
|---|---|---|
| Foreuse laser | `Drill` | 3×3, tier 4, `drillTime` 280, 1,1 énergie/tick, eau 0,08 en boost |
| Foreuse à souffle | `Drill` | 4×4, tier 5, `drillTime` 280, `warmupSpeed` 0,01, 20 objets, 3 énergie/tick, eau 0,1, boost 1,8 |
| Bonus d'eau (toutes les foreuses) | `ConsumeLiquid.boost()` | `speed = lerp(1, liquidBoostIntensity, optionalEfficiency) × efficiency`, appliqué au `warmup` et à la progression (×1,6² en régime établi) |
| Grande batterie | `Battery` | 3×3, 50 000 |
| Diode | `PowerDiode` | de l'arrière vers l'avant : la moitié de l'écart de charge relative des batteries, bornée à la place libre ; ne relie pas les réseaux |

Tests PC (138 au total) : foreuse laser sans énergie (0), 150 objets en 120 s (1,26/s attendu), rapport du bonus d'eau 2,55 (1,6² = 2,56) ; diode : sens unique, énergie conservée, rechargement.
Les tampons de `updatePower` sont réutilisés d'une image à l'autre (plus d'allocation par image).
Coût hôte de la base de test (2 311 bâtiments) : 29 à 47 µs par tick selon les exécutions. Mesure PC bruitée, pas une mesure console.

# 0.4 — lot 2b : industrie liquide, différentiel, réacteur

Liquides ajoutés : pétrole, cryofluide, laitier. Le `LiquidModule` de v146 garde un montant par liquide. Ici, un emplacement supplémentaire stocke le liquide **produit** par une usine (`outputLiquid`), et le transport reste mono-liquide comme les conduites v146. Le mélangeur de cryofluide garde ainsi l'eau et le cryofluide en même temps.

| Bloc | Classe v146 | Règles portées |
|---|---|---|
| Cultivateur | `AttributeCrafter` | eau 18/60, 80/60 énergie, 1 spore / 100 ticks (bonus d'attribut non porté) |
| Presse à spores | `GenericCrafter` | 1 spore / 20 ticks, pétrole 18/60 par tick efficace, 0,7 énergie |
| Compresseur de plastanium | `GenericCrafter` | pétrole 0,25 + 2 titane, 3 énergie, 60 ticks |
| Mélangeur de cryofluide | `GenericCrafter` | eau 12/60 + 1 titane / 120 ticks, cryofluide 12/60, 1 énergie |
| Fondeur | `GenericCrafter` | ferraille → laitier 12/60, 1 énergie |
| Séparateur | `Separator` | laitier 4/60, 1,1 énergie, 35 ticks, tirage pondéré cuivre 5 / plomb 3 / graphite 2 / titane 2 |
| Centrifugeuse à charbon, tisseur de phase, fonderie d'alliage | `GenericCrafter` | valeurs de `Blocks.java` |
| Générateur différentiel | `ConsumeGenerator` | pyratite (`consumeItem`, multiplicateur 1) toutes les 220 ticks + cryofluide 0,1 ; 18/tick |
| Réacteur au thorium | `NuclearReactor` | production = thorium / 30 × 15 ; chaleur += remplissage × 0,02 ; refroidissement min(cryofluide, chaleur / 0,5) ; alerte au-dessus de 0,46 (`flashThreshold`) ; explosion à 0,999 si thorium ≥ 5 ou chaleur ≥ 0,5 : 19 cases, 5 000 dégâts avec atténuation |

- Rendu : les couches `DrawMulti` sont lues dans `Blocks.java`, dans leur ordre (régions fixes ou tournantes, `DrawWarmupRegion` / `DrawFlame`, `DrawLiquidRegion`, `DrawLiquidTile`). La vapeur et la fonderie de silicium passent par ce même chemin.
- Réacteur : capot teinté par le refroidissant, voyants rouges selon la chaleur. Fiche : chaleur, production, liquide produit. Bandeau d'alerte clignotant en haut de l'écran.
- Sauvegarde **format 3** : liquide produit et chaleur du réacteur. Les formats 1 (0.2) et 2 (0.3) restent lus (fixtures).
- La pagination de la palette (commit `4f13c47`, intégré par cherry-pick) accueille les nouveaux blocs.

Tests PC (153 au total, dont 15 pour ce lot), avec les valeurs mesurées :

| Test | Résultat |
|---|---|
| Cultivateur | 9 spores en 1 000 ticks, 300 d'eau consommée |
| Pétrole | conservé exactement (60,00 produit = 60,00 stocké) |
| Compresseur de plastanium | 5 plastanium pour 10 titane |
| Mélangeur de cryofluide | entrée et sortie simultanées, état « sortie pleine » |
| Séparateur | 1 028 tirages : cuivre 0,45 (attendu 5/12) et titane 0,16 (attendu 2/12) |
| Fondeur | produit du laitier |
| Différentiel | efficacité 1, s'arrête sans cryofluide |
| Réacteur refroidi | chaleur 0 |
| Réacteur non refroidi | alerte, puis explosion qui détruit un mur à 4 cases ; le réacteur à 20 cases survit |
| Format 3 | rechargement correct |

Coût hôte de la base de test : 30 à 59 µs par tick selon l'exécution (machine partagée, mesure bruitée, pas une mesure console).
