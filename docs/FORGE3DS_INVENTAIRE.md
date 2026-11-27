# Fonctionnalités de Forge3DS 1.4.6 : état dans Mindustry 3DS 0.2

Source : les sources et documents de Forge3DS 1.4.6 (`ELECTRICITE.md`, `INDUSTRIE.md`, `ATELIERS.md`, `PROGRESSION.md`, `TUTORIEL.md`, `EXTENSIONS.md`, `NOEUDS_CONFIGURABLES.md`, `CARTES*.md`).
Les valeurs de Forge3DS ne sont **pas** reprises : seules les données et règles de Mindustry v146 font foi. Par exemple, le panneau solaire produit 0,1/tick dans v146, contre 0,12 dans Forge3DS.

Légende : **présent** · **partiel** · **absent** · **adaptation nécessaire** (n'existe pas ainsi dans Mindustry, ou exige un choix).

## Énergie et diagnostic

| Fonctionnalité Forge3DS | État | Détail dans Mindustry 3DS |
|---|---|---|
| Générateur à combustion | présent | `ConsumeGenerator` + `ConsumeItemFlammable` |
| Panneau solaire | présent | `SolarGenerator`, 0,1/tick ; grand panneau ajouté |
| Générateur à vapeur (charbon + eau) | présent | 0.3, `ConsumeGenerator` + `ConsumeLiquid` |
| Batterie | présent | charge selon la place libre, décharge selon le stock (`PowerGraph`) |
| Nœuds : liens automatiques à la pose | présent | `PowerNode.getNodeLinks` / `getPotentialLinks` |
| Nœuds : 10 liens configurables | présent | `PowerNode.configure` ; le double appui lie automatiquement ou délie tout |
| Ponts configurables | présent | `ItemBridge`, aligné, 4 cases |
| Suspendre / reprendre une machine | adaptation nécessaire | dans v146, seulement par processeur logique ; non ajouté |
| États explicites (voyants) | présent | `BlockStatus` + raison détaillée |
| Production, consommation et stockage par réseau | présent | fiche d'inspection |
| Portée et liens visibles | présent | survol, configuration, aperçu à la pose |
| Aide pour alimenter une usine | partiel | une ligne dans la fiche ; pas de page d'aide dédiée |

## Liquides et industrie

| Fonctionnalité Forge3DS | État | Détail |
|---|---|---|
| Pompe mécanique, conduites, réservoir, eau | présent | 0.3, voir `LIQUIDES_0.3.md` |
| Jonction, routeur et ponts liquides, conduites pulsée et blindée, pompes rotative et à impulsion | présent | 0.4 lot 1 |
| Pétrole, cryofluide, laitier | absent | lot 2 |
| Presse à graphite, fonderie de silicium, four à verre | présent | |
| Pulvérisateur, mélangeur de pyratite, mélangeur d'explosif | présent | ajoutés dans la 0.2 (objets et énergie uniquement) |
| Cultivateur, presse à spores, plastanium, centrifugeuse, cryofluide, tissu phasé, alliage, fondeur, séparateur | absent | dépendent des liquides ou de blocs non portés |
| Déchargeur, conteneurs liés au noyau | partiel | déchargeur à filtre tactile (0.4) ; conteneurs non liés au noyau |
| Commandes optionnelles (ATELIERS) | adaptation nécessaire | propres à Forge3DS, absentes de Mindustry |

## Logistique et construction

| Fonctionnalité Forge3DS | État | Détail |
|---|---|---|
| Convoyeurs basique et titane, raccords | présent | `Conveyor` / `Autotiler`, voir `AMELIORATIONS_0.2.md` |
| Tracé maintenu, rotation sans repayer | présent | rotation rapide, découpe des diagonales |
| Routeur, jonction, trieur, portes, pont | présent | |
| Tapis blindé, pont de phase | présent | 0.4 lot 1 |
| Convoyeur plastanium | absent | `StackConveyor`, voir REPRISE.md |
| Démolir une zone (rectangle) | absent | adaptation possible du « break » rectangulaire de Mindustry |
| Schémas (copier/coller) | absent | |

## Progression, unités et modes

| Fonctionnalité Forge3DS | État | Détail |
|---|---|---|
| Campagne par secteurs | partiel | 5 secteurs officiels débloqués dans l'ordre ; pas de lancement ni de ressources entre secteurs |
| Recherche (arbre technologique) | absent | `TechTree` à porter |
| Tutoriel en missions | adaptation nécessaire | Mindustry utilise objectifs et processeurs (Ground Zero) ; non porté |
| Usines d'unités, unités alliées, ralliement | absent | |
| Survie libre, bac à sable | absent | |
| Tourelles Duo, Scatter, Hail | présent | |
| Sauvegarde automatique | partiel | réglable (2/5/10 min/désactivée), copie `.prev` récupérable ; synchrone |
| Grandes cartes / agrandissement | adaptation nécessaire | les cartes officielles ont leur taille propre |
| Banc d'essai et mesures | partiel | FPS et temps CPU dans le menu pause ; pas de banc séparé |
