# Mindustry 3DS 0.2 — artefacts, icônes, convoyeurs, énergie

Lots réalisés à partir du moteur 0.1 (C++/citro2d) et du sous-module Mindustry v146.
Chaque cause retenue ci-dessous est appuyée par une preuve (source citée, mesure ou test).
Les captures hôte reproduisent les appels citro2d ; **elles ne remplacent pas un essai sur console**.

## Lot 1 — artefacts visuels

### Causes établies

| # | Constat | Preuve | Correction |
|---|---|---|---|
| 1 | **Les sprites en miroir étaient décalés d'une fois leur taille.** C'était le cas des virages de convoyeur `blendscly = -1`, ainsi que des jambes et des armes en miroir. | `citro2d 1.7.0` (version liée, `dkp-pacman -Q`), `C2Di_CalcQuad` : la taille vient de `fabs(pos.w)` mais l'origine de `-center` ; le miroir se fait en inversant les UV quand `w < 0`. Le backend passait `center = w/2`, donc négatif. Avant/après : `docs/controle/miroir_avant_apres.png`. | `source/gfx_quad.hpp` : le centre vaut toujours `\|w\|/2`, `\|h\|/2`. |
| 2 | **Halos autour des contours après la réduction 32 → 16 px.** | LANCZOS crée des couleurs absentes des 4 pixels sources dans **15,9 %** des pixels réduits (mesure sur 400 sprites de blocs). | Moyenne 2×2 exacte en alpha prémultiplié (`downscale()` dans `tools/build_assets.py`). Elle ne peut pas produire de couleur hors plage. |
| 3 | **Atlas sans marge entre sprites.** | Les t3s étaient générés sans option `--border`. tex3ds peut aussi tourner des sous-textures (`top < bottom`, une dans l'atlas actuel). | `--border edge` : bordure de 1 px recopiant la couleur du bord, sans partage. |
| 4 | **Sprites posés entre deux pixels pendant les déplacements de caméra.** | La caméra suit le joueur par interpolation. 7 442 des 19 731 sprites à 0°/90° des scènes de contrôle tombaient à une position sub-pixel. Avec `GPU_NEAREST`, qui est le filtre par défaut (`C3D_TexInitWithParams` ne pose aucun bit de filtre), l'échantillonnage tombe alors sur les bords des texels. | Caméra alignée sur le pixel écran ; coins des quads à 0°/90°/180°/270° arrondis au pixel (`spriteQuad`). Résultat : **0** position sub-pixel. |
| 5 | **Arbres apparaissant tardivement en bord d'écran.** | Les sprites de `white-tree` font 80 px (5 cases) mais n'étaient parcourus qu'à 1 case de marge. Leur ombre pouvait aussi recouvrir un arbre déjà dessiné. | Marge calculée depuis la plus grande taille de sprite ; toutes les ombres sont dessinées avant les corps. |

Le filtrage linéaire n'est **pas** en cause : les textures sont en NEAREST (source citro3d), et aucun flou n'a été ajouté.

### Scènes de contrôle reproductibles

`make scenes` (ou `tests/run_scenes.sh`) enregistre les appels citro2d de scènes fixes (`tests/control_scenes.cpp`). Il les compose ensuite avec **le vrai atlas produit par tex3ds**, en reproduisant `C2Di_CalcQuad`, l'inversion des UV, les sous-textures pivotées et l'échantillonnage NEAREST (`tools/render_shots.py`), puis lance les contrôles (`tools/check_scenes.py`) :

| Contrôle | Jeu (alignement actif) | Référence sans alignement |
|---|---:|---:|
| Sprites à 0°/90° posés entre deux pixels | **0** / 21 053 | 7 442 / 19 731 |
| Texels lus hors de leur sprite | 0 | 0 |
| Panoramique lent (0,25 unité/image), écart avec l'image 0 décalée | 0 px | 0 px |
| Panoramique diagonal (0,37 ; 0,21), même mesure | 0 px | 0 px |

Les scènes couvrent : caméra fixe, déplacement lent, diagonale, les 7 cas de raccord × 4 directions, les raccords avec foreuse, routeur, jonction, trieur, nœud, noyau 3×3, usine et ponts, les bâtiments et arbres en bord d'écran, l'aperçu du tracé et la palette de chaque catégorie.
Le rastériseur hôte est exact sur des sprites isolés (0 écart aux 4 rotations). Le GPU réel peut arrondir différemment aux bords des texels : l'alignement au pixel et les bordures `edge` visent justement à ne pas dépendre de ces arrondis. **À confirmer sur console.**

## Lot 2 — icônes des menus

- Les icônes sont précomposées comme le fait `tools/Generators.java` (`block-<nom>-full`), à partir des couches de `Block.icons()` :
  - foreuse : socle, rotor et dessus (`Drill.icons()`) ;
  - tourelle : `block-N`, puis `-preview` détouré (`DrawTurret`) ;
  - noyau : région, plus la région d'équipe multipliée par la couleur de Sharded ;
  - batterie : `DrawRegion("-top")` ;
  - pulvérisateur : rotor et dessus (`DrawMulti`) ;
  - mur : `name` ou `name1`.
- Deux tailles réduites par filtre boîte entier : **32×32** (palette, fiche) et 16×16 (onglets). Elles s'affichent toujours à l'échelle 1, sans agrandissement non entier.
- La même icône (`BlockDef.sprIcon`) sert à la palette, aux onglets, à la fiche du bloc sélectionné et à la fiche d'inspection. Les futurs menus de recherche pourront la reprendre.
- Les tourelles en jeu utilisent désormais `duo-preview` (corps et canons) au lieu du seul corps.
- `squareSprite` est calculé comme dans `Generators.java` (aucun pixel sous 90 % d'opacité).
- Planche : `docs/controle/palettes.png`.

## Lot 3 — convoyeurs

Comparaison avec `Conveyor` et `Autotiler` v146 :

| Point | Avant | Après |
|---|---|---|
| Animation | exigeait `b.len > 0` | `enabled && clogHeat <= 0,5` (un tapis vide défile) |
| Raccords étendus (`Conveyor.draw`, `blending`) | absents | demi-régions `sliced()` sous les blocs, vers les voisins non carrés (trieur, nœud, murs de ferraille…) |
| `Conveyor.blends` | `hasItems` approximé | `outputsItems() \|\| (lookingAt && hasItems)` exact, multi-cases par les bords |
| Miroirs | décalés d'une case (lot 1) | corrects pour les 7 cas × 4 directions |
| Remplacement basique → titane | objets perdus, plan abandonné | `ConveyorBuild.overwrote` (objets conservés), coût du titane payé une fois |
| Rotation d'un tapis (même en construction) | rotation perdue | `quickRotate` : gratuit, objets et progression conservés |
| Tracé maintenu | un pas diagonal laissait un trou ; le tapis précédent était tourné vers une case refusée | le diagonal devient deux pas d'un axe ; le tapis précédent n'est tourné que si la pose suivante réussit |
| Aperçu | fantôme droit, pâle | `Autotiler` appliqué aux plans : les virages et les entrées latérales sont visibles avant la construction |

Tests fonctionnels (`tests/host_test.cpp`) :
- les 4 directions (aucun objet perdu ni dupliqué) ;
- les 28 cas de raccord ;
- une foreuse 2×2 en contact latéral ;
- les joints vers les blocs non carrés ;
- l'animation d'un tapis vide ;
- 3 tapis fusionnés ;
- la traversée d'une jonction et la transition basique → titane → basique ;
- l'embouteillage puis la reprise ;
- la rotation et l'amélioration sans perte ni double facturation.

Planche visuelle : `docs/controle/convoyeurs_4_directions.png` et `connections.png`.

## Lot 4 — énergie et diagnostic (repris de Forge3DS, règles v146)

Voir `docs/FORGE3DS_INVENTAIRE.md` pour le tableau complet. Ajouts de ce lot :

- **Panneau solaire** (6/s) et **grand panneau solaire** (78/s) : `SolarGenerator`. Les secteurs portés n'ont pas de règle d'éclairage, d'où une efficacité de 1.
- **Configuration des nœuds** : A sur un nœud, puis A sur une cible à portée pour lier ou délier (`PowerNode.configure`, 10 liens au plus). A sur le nœud lui-même lie automatiquement, ou délie tout s'il a déjà des liens (`onConfigureBuildTapped`).
- **Ponts** : liaison configurable, alignée et à 4 cases au plus (`ItemBridge`). **Trieur** : L/R choisit l'objet trié.
- **États** (`Building.status()` / `BlockStatus`) avec raison : actif, sans combustible, sans courant, non relié au réseau, courant insuffisant en %, manque d'ingrédient (lequel et combien), sortie pleine, aucun minerai, sans munitions, batterie en charge, en décharge, pleine ou vide.
- **Bilan par réseau** (`PowerGraph`) : production, consommation et stock des batteries.
- **Portée et liens visibles** au survol et pendant la configuration ; aperçu des liens à la pose d'un nœud (`getPotentialLinks`).
- **Aide** : une ligne rappelle comment alimenter une usine non reliée.
- **Usines** sans liquide : pulvérisateur, mélangeur de pyratite et mélangeur d'explosif, avec les recettes de `Blocks.java`.
- **Activer ou désactiver une machine** : Mindustry v146 ne le permet au joueur que par processeur logique. Ce n'est donc **pas** ajouté.
- **Mesure de performance** dans le menu pause : FPS moyen sur 2 s, temps CPU de la simulation et du dessin, mesurés avec `svcGetSystemTick`.

## Préservation de la fluidité

- Tout le travail nouveau coûteux est fait à la génération des ressources : icônes, demi-régions, régions teintées et réduction des sprites.
- Les raccords sont calculés seulement quand le voisinage change (`updateProximity`), pas à chaque image.
- Les joints ne sont dessinés qu'à côté des blocs non carrés.
- Les fantômes de plan hors écran sont ignorés.
- L'aperçu des liens d'un nœud n'est calculé que pendant la pose d'un nœud.
- L'atlas reste unique : 512×1024 en RGBA8, 331 sprites.
- **Aucune mesure FPS sur console n'est encore disponible** : le menu pause donne désormais les chiffres à relever.
