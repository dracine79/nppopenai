# Prochaine version — encodage et destination des réponses

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

## Destination de la réponse et changement d’onglet

### Défaut signalé

Pendant une requête, l’utilisateur peut changer d’onglet. La réponse est malgré tout écrite sans garantir sa destination d’origine, avec le risque de modifier le mauvais document ou le mauvais emplacement.

### Comportement prévu

1. Au lancement de la requête, mémoriser l’identité stable du document source et la plage de texte sélectionnée. Le nom du fichier, le numéro d’onglet et le contrôle Scintilla actif ne suffisent pas à identifier durablement cette destination.
2. À la réception, retrouver le document source même si un autre onglet est actif. Avec **Conserver la sélection et la consigne**, insérer la consigne et la réponse immédiatement après la sélection d’origine. Sans conservation, remplacer uniquement la plage d’origine si elle est encore valide.
3. Un simple changement d’onglet ou déplacement du curseur ne change pas la destination. Revenir à l’onglet source et donner le focus à la réponse lors de son insertion.
4. Si le document a été fermé ou si des modifications rendent la plage d’origine incertaine, ne rien insérer automatiquement. Conserver la réponse en mémoire et demander où la placer, avec des choix explicites : emplacement sélectionné dans un document ouvert, nouvel onglet ou report de l’insertion. Ne jamais choisir silencieusement l’onglet actif.
5. Sans sélection initiale, demander la destination avant l’insertion si aucun point d’insertion fiable n’a été mémorisé.
6. Appliquer les conversions d’encodage et les fins de ligne du document effectivement choisi. Une annulation ou une erreur doit préserver les documents et permettre de récupérer la réponse.
7. Associer chaque réponse à sa propre requête; si plusieurs requêtes sont autorisées, leurs destinations ne doivent pas se mélanger.

### Vérifications prévues

- Lancer depuis un onglet A puis passer à B : seule la destination prévue dans A reçoit la réponse.
- Déplacer le curseur ou changer de sélection pendant la requête : la destination d’origine reste utilisée.
- Modifier le document source avant le retour : suivre la plage si cela reste fiable, sinon demander une destination.
- Fermer le document source puis ouvrir un autre document au même nom : ne pas confondre les deux documents.
- Vérifier les deux modes, conservation et remplacement, ainsi que les documents sans nom et les deux vues de Notepad++.
- Annuler le choix de destination puis récupérer la réponse sans perte ni modification du mauvais document.

**Statut :** besoin ajouté aux travaux de la prochaine version; comportement à implémenter et à tester.
