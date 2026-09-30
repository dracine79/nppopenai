# NppOpenAI personnalisé — Windows 64 bits

DLL compilée à partir de [NppOpenAI](https://github.com/Krazal/nppopenai), version 0.5.0.0, révision `34da3cb`. Le fichier `source.patch` décrit les modifications. La licence du projet figure dans `LICENSE`.

Ce dossier est aussi un dépôt Git local contenant le code source modifié et les DLL livrées. Aucun dépôt distant n'est configuré.

## Installation

1. Fermer complètement Notepad++.
2. Copier `NppOpenAI.dll` dans `C:\Program Files\Notepad++\plugins\NppOpenAI\`, en remplaçant la DLL actuelle. Windows peut demander une autorisation d'administrateur.
3. Rouvrir Notepad++. Les fichiers `NppOpenAI.ini` et `NppOpenAI_instructions` restent en place. `NppOpenAI-original.dll` est une copie de la DLL installée avant cette modification, pour un retour en arrière.

## Choix d'une instruction

La fenêtre a une largeur fixe et une liste verticale regroupée par famille. Le champ du haut filtre les noms. `Tab` passe à la liste, les flèches déplacent la sélection et `Entrée` la valide. `Échap` annule. On peut également double-cliquer une instruction.

Raccourcis de famille : `Alt+T` Tous, `Alt+E` Édition, `Alt+O` Tonalité, `Alt+F` Format, `Alt+C` Commandes. `Alt+V` valide la sélection. La dernière instruction utilisée reste présélectionnée si elle figure dans les résultats.

## Préprompt commun

Ajouter une section `[Global]` dans `NppOpenAI_instructions`, par exemple entre le tableau de repères et le premier `[Prompt:...]` :

```text
[Global]
En l'absence de consigne contraire, réponds en français dans un style neutre, clair et concis. Préserve les faits, chiffres, noms, termes techniques et nuances. N'invente pas d'information manquante.
```

Le contenu de `[Global]` est ajouté à chaque instruction nommée. Le tableau de repères situé avant les sections reste exclu des appels au modèle. Une section `[Global]` vide ne change rien. Les 22 instructions actuelles restent utilisables telles quelles.

## Modèle dans `NppOpenAI.ini`

Conserver **une seule** ligne `model=` active dans la section `[API]`. Les autres peuvent commencer par `;`. Une description peut suivre le nom du modèle après un autre `;` :

```ini
;model=qwen3.5:27b       ; Modèle polyvalent
model=granite4.2:8b     ; Modèle courant
```

La nouvelle DLL retire le commentaire final et les espaces avant l'appel à l'API. Pour changer de modèle, commenter l'ancienne ligne et décommenter la nouvelle, puis enregistrer le fichier. Redémarrer Notepad++ si le changement ne prend pas effet immédiatement.

Pour `response_type=ollama`, `api_url` peut être l'adresse de base du serveur ou l'adresse complète de `/api/generate`. La DLL ajoute ce chemin seulement si aucun chemin d'API n'est déjà indiqué. L'adresse du serveur provient toujours du `.ini`; elle n'est pas inscrite dans la DLL.

## Vérification effectuée

Compilation x64 réussie; version et exports requis de Notepad++ présents. Le parseur a été testé avec un préprompt commun, une instruction unique et les 22 instructions du fichier actuel. La fenêtre a été testée avec recherche, filtre de famille et absence de résultat. La lecture Windows des commentaires INI a été vérifiée. Le serveur Ollama a répondu aux appels de génération standard et en flux avec le modèle actif (`HTTP 200`), y compris avec un transfert HTTP en blocs. Aucun essai visuel dans l'installation courante de Notepad++ n'a été réalisé.
