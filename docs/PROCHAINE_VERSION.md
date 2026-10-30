# Prochaine version — encodage et destination des réponses

## Défaut signalé

Avec **Conserver la sélection et la consigne** activé, le libellé ajouté par le plugin peut apparaître sous une forme corrompue, par exemple `Consigne appliqu饠:`. Dans l'exemple transmis, la sélection d'origine, la consigne saisie et la réponse sont lisibles. Le défaut visible est donc concentré sur le libellé généré par le plugin.

## Constat vérifié dans la version `daf88f9`

Le code construit l'insertion dans `src/api/OpenAIClient.cpp` avec un littéral étroit `"Consigne appliquée :"`, puis passe toute l'insertion à `EditorInterface::encodeForDocument`, qui attend de l'UTF-8. Dans le DLL compilé, la séquence du libellé contient `... 75 E9 65 ...` pour `uée`, alors que sa représentation UTF-8 serait `... 75 C3 A9 65 ...`. Ce mélange d'encodages est un défaut du plugin. L'exemple utilisateur confirme la corruption affichée; la version exacte du DLL chargé lors de cet essai reste à confirmer.

## Correction implémentée

1. Construire le libellé en UTF-8 de façon explicite, par exemple à partir d'un littéral large converti par `toUTF8`, avant de l'ajouter à `insertion`. Vérifier les autres littéraux non ASCII ajoutés aux textes envoyés au modèle ou insérés dans Notepad++.
2. Garder une seule règle de conversion : chaînes internes d'insertion en UTF-8, puis conversion vers l'encodage du document au dernier moment. Ne pas modifier l'encodage du document pour corriger ce libellé.
3. Ajouter un essai de régression avec une consigne contenant des accents, la conservation activée et un document temporaire en UTF-8, puis en encodage hérité (par exemple Windows-1252). Vérifier l'affichage de `Consigne appliquée :`, la réponse, et l'encodage du fichier après sauvegarde et réouverture.
4. Vérifier qu'un caractère impossible à représenter dans l'encodage du document déclenche une erreur sans modifier la sélection.

**Statut :** correctif implémenté, DLL Release x64 recompilée; essais dans Notepad++ actif restant à effectuer.

## Destination de la réponse et changement d’onglet

### Défaut signalé

Pendant une requête, l’utilisateur peut changer d’onglet. La réponse est malgré tout écrite sans garantir sa destination d’origine, avec le risque de modifier le mauvais document ou le mauvais emplacement.

### Comportement implémenté après validation des cas d’usage

- Au lancement, mémoriser l’identifiant du buffer, l’identité du document et la sélection (ou le curseur sans sélection).
- Même onglet et document inchangé : insérer après la sélection si la conservation est activée, sinon remplacer la plage mémorisée. Déplacer le curseur ne change pas cette destination.
- Tout changement d’onglet, même suivi d’un retour à l’onglet source, ou toute modification du document, même annulée, impose un popup : **Emplacement d’origine**, **Définir l’endroit…**, **Presse-papiers**.
- L’origine est désactivée après modification, fermeture ou changement d’encodage : les anciennes coordonnées ne sont pas réutilisées. Un buffer fermé puis rouvert ne redevient pas une origine valide.
- **Définir l’endroit…** laisse les onglets accessibles. L’utilisateur place le curseur puis confirme avec **Insérer ici**. Ce choix insère la réponse seule au curseur, sans remplacer une sélection courante ni ajouter la consigne.
- **Presse-papiers** copie la réponse seule en Unicode, sans modifier aucun document. L’historique Windows peut la conserver selon les réglages du système.
- Fermer le popup reporte le choix. **Placer la réponse en attente** dans le menu, ou **Ask OpenAI**, le rouvre. Une seule requête ou réponse en attente est autorisée à la fois. Une réponse en attente est conservée en mémoire jusqu’à son placement ou la fermeture de Notepad++.
- L’insertion utilise l’encodage et les fins de ligne de la destination choisie. Une erreur d’encodage ou un document en lecture seule laisse la réponse disponible pour une autre destination. Une insertion constitue une seule opération d’annulation et sélectionne le texte ajouté.

### Validation

`PlacementProbe` utilise le contrôleur de production, la vraie boîte de dialogue et un hôte Win32 simulant les documents. Il vérifie la conservation et le remplacement à la plage mémorisée, le changement d’onglet et le retour à l’origine, le document modifié ou fermé, le report du choix, la nouvelle destination, le curseur sans sélection, les accents UTF-8 / Windows-1252 et le rejet d’un caractère non représentable sans modification du document.

Compilation Release x64 : `MSBuild vs.proj/NppPluginTemplate.sln /p:Configuration=Release /p:Platform=x64 /p:SkipPackaging=true`.
Test de placement : `MSBuild tests/PlacementProbe.vcxproj /p:Configuration=Release /p:Platform=x64`, puis `tests/bin/PlacementProbe.exe`.

Les essais de bout en bout dans Notepad++ actif (dont documents clonés dans deux vues, copie dans le presse-papiers et sauvegarde/réouverture des fichiers) restent à effectuer.
