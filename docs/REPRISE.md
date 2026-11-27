# Reprise — enrichissement Serpulo (branche claude/serpulo-enrichment)

Point de départ : `3a7cf8a` (0.3.2 avec musique). Lots demandés, dans l'ordre :

| Lot | État | Reste à faire |
|---|---|---|
| 1. Transport et liquides | **fait** (sauf tapis plastanium) | `StackConveyor` (tapis plastanium : piles de 10, chargement et déchargement aux extrémités) |
| 2. Industrie et énergie | **fait** (lots 2a, 2b) | bonus d'attribut du cultivateur (spores du sol), puddles/incendies liés au pétrole, explosion des générateurs à vapeur (`explosionDamage` 0 en v146, rien à faire) |
| 3. Défense | à faire | Scorch, Salvo, Lancer (charge), Arc, Parallax, défense ponctuelle, murs avancés, projecteur de réparation, bouclier, accélérateur ; refroidissement des tourelles, statuts (brûlure, mouillé, gel), incendie |
| 4. Unités | à faire | usines sol/air, reconstructeur T2, poly/mono, commandement tactile, ralliement |
| 5. Construction | à faire | tracé en L, remplacement en ligne, schémas, démolition de zone, minimap, catalogue, diagnostic |
| 6. Progression | à faire | tutoriel Ground Zero à objectifs, recherche, noyaux, cargaisons, aperçu des vagues, modes survie et bac à sable, seeds |

Contraintes à garder :
- Title ID `000400000F4D5300`, dossier `SD:/3ds/mindustry-3ds/`, musique et DSP (droits du RSF vérifiés par `tools/verify_cia.py`).
- Sauvegardes : format 3 (liquide produit, chaleur) ; format 2 avec table des noms (les nouveaux blocs n'exigent pas de nouveau format) ; formats 1 (0.2) et 2 (0.3) testés par fixtures.
- Palette paginée (commit 4f13c47).
- Build : `ffmpeg` sur l'hôte pour `romfs/music/*.pcm` si le conteneur devkitARM n'en a pas.
