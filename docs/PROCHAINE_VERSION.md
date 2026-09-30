# Prochaine version — libellé de consigne et encodage

## Défaut signalé

Avec **Conserver la sélection et la consigne** activé, le libellé ajouté par le plugin peut apparaître sous une forme corrompue, par exemple `Consigne appliqu饠:`. Dans l'exemple transmis, la sélection d'origine, la consigne saisie et la réponse sont lisibles. Le défaut visible est donc concentré sur le libellé généré par le plugin.

## Constat vérifié dans la version `daf88f9`

Le code construit l'insertion dans `src/api/OpenAIClient.cpp` avec un littéral étroit `"Consigne appliquée :"`, puis passe toute l'insertion à `EditorInterface::encodeForDocument`, qui attend de l'UTF-8. Dans le DLL compilé, la séquence du libellé contient `... 75 E9 65 ...` pour `uée`, alors que sa représentation UTF-8 serait `... 75 C3 A9 65 ...`. Ce mélange d'encodages est un défaut du plugin. L'exemple utilisateur confirme la corruption affichée; la version exacte du DLL chargé lors de cet essai reste à confirmer.

## Correction prévue

1. Construire le libellé en UTF-8 de façon explicite, par exemple à partir d'un littéral large converti par `toUTF8`, avant de l'ajouter à `insertion`. Vérifier les autres littéraux non ASCII ajoutés aux textes envoyés au modèle ou insérés dans Notepad++.
2. Garder une seule règle de conversion : chaînes internes d'insertion en UTF-8, puis conversion vers l'encodage du document au dernier moment. Ne pas modifier l'encodage du document pour corriger ce libellé.
3. Ajouter un essai de régression avec une consigne contenant des accents, la conservation activée et un document temporaire en UTF-8, puis en encodage hérité (par exemple Windows-1252). Vérifier l'affichage de `Consigne appliquée :`, la réponse, et l'encodage du fichier après sauvegarde et réouverture.
4. Vérifier qu'un caractère impossible à représenter dans l'encodage du document déclenche une erreur sans modifier la sélection.

**Périmètre de cette note :** documentation du défaut seulement. Aucun DLL n'a été recompilé pour ce point.
