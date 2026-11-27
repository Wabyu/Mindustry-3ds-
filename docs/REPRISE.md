# Reprise — enrichissement Serpulo (branche claude/serpulo-enrichment)

Point de départ : `3a7cf8a` (0.3.2 avec musique). Lots demandés, dans l'ordre :

| Lot | État | Reste à faire |
|---|---|---|
| 1. Transport et liquides | **fait** (sauf tapis plastanium) | `StackConveyor` (tapis plastanium : piles de 10, chargement et déchargement aux extrémités) |
| 2. Industrie et énergie | **partiel** : foreuses laser et à souffle, bonus d'eau, diode, grande batterie (lot 2a) | cultivateur et presse à spores (pétrole), compresseur de plastanium, mélangeur de cryofluide, fondeur et séparateur (laitier), tisseur de phase, fonderie d'alliage ; générateur différentiel, réacteur au thorium (chaleur, refroidissement au cryofluide, alerte et explosion). Prérequis : liquides multiples par bâtiment (le module actuel n'en garde qu'un) |
| 3. Défense | à faire | Scorch, Salvo, Lancer (charge), Arc, Parallax, défense ponctuelle, murs avancés, projecteur de réparation, bouclier, accélérateur ; refroidissement des tourelles, statuts (brûlure, mouillé, gel), incendie |
| 4. Unités | à faire | usines sol/air, reconstructeur T2, poly/mono, commandement tactile, ralliement |
| 5. Construction | à faire | tracé en L, remplacement en ligne, schémas, démolition de zone, minimap, catalogue, diagnostic |
| 6. Progression | à faire | tutoriel Ground Zero à objectifs, recherche, noyaux, cargaisons, aperçu des vagues, modes survie et bac à sable, seeds |

Contraintes à garder :
- Title ID `000400000F4D5300`, dossier `SD:/3ds/mindustry-3ds/`, musique et DSP (droits du RSF vérifiés par `tools/verify_cia.py`).
- Sauvegardes : format 2 avec table des noms (les nouveaux blocs n'exigent pas de nouveau format) ; formats 1 (0.2) et 2 (0.3) testés par fixtures.
- Catégories : la palette affiche 14 blocs. Les lots 2 et 3 dépasseront ce nombre en « Production », « Énergie » et « Tourelles » : prévoir une pagination de la grille.
- Build : `ffmpeg` sur l'hôte pour `romfs/music/*.pcm` si le conteneur devkitARM n'en a pas.
