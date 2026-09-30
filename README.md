# NppOpenAI personnalisé — Windows 64 bits

Ce dépôt est un fork de [NppOpenAI](https://github.com/Krazal/nppopenai), sous la licence indiquée dans `LICENSE`. Le DLL `NppOpenAI.dll` est compilé pour Notepad++ 64 bits. `NppOpenAI-original.dll` conserve la version antérieure pour un retour en arrière.

## Installation

1. Fermer Notepad++ complètement.
2. Sauvegarder le DLL et les deux fichiers de configuration actifs.
3. Copier `NppOpenAI.dll` dans `C:\Program Files\Notepad++\plugins\NppOpenAI\`.
4. Pour utiliser les menus personnalisables avec les 22 instructions existantes, copier le fichier local `NppOpenAI_instructions.migrated` sous le nom `NppOpenAI_instructions` dans `%APPDATA%\Notepad++\plugins\Config\`. Ce fichier est généré localement et exclu du dépôt Git. Le fichier d'origine reste compatible, mais il apparaît sous un menu unique « Instructions ».
5. Pour démarrer les deux options à `0` avec les autres paramètres actuels conservés, copier `NppOpenAI.ini.migrated` sous le nom `NppOpenAI.ini` au même endroit. Ce fichier est aussi local et exclu de Git. Une valeur `1` déjà présente dans le `.ini` actif reste sinon prioritaire sur le défaut `0`.
6. Rouvrir Notepad++ et utiliser `Ctrl+Maj+O`.

## Sélecteur

Le menu Windows est construit selon `[Menu]` dans `NppOpenAI_instructions`; les libellés, l'ordre et les sous-menus se changent dans ce fichier, sans recompilation. Les menus s'ouvrent à la souris ou par `Alt` et la lettre soulignée. Un sous-menu peut être ajouté avec deux espaces d'indentation supplémentaires, pour un maximum de trois niveaux feuille comprise.

Le focus initial est sur **Nom de l'instruction**. La recherche ignore la casse et les accents et montre une liste seulement avec une à quatre correspondances. Un choix dans le menu ou la liste remplit le nom; `Entrée` exécute l'instruction. Les cinq dernières instructions validées sont enregistrées par identifiant dans le `.ini`.

Dans le champ Nom au repos, `1` à `5` insèrent une instruction récente, `8` change **Conserver la sélection et la consigne**, et `9` change **Afficher le raisonnement**. Pendant la saisie partielle d'un nom, ces chiffres sont du texte ordinaire. La fenêtre peut être redimensionnée et mémorise sa taille.

Le champ **Consignes** reçoit une précision ponctuelle distincte de la sélection. Le plugin ne recherche aucune balise `//` dans le document. Si la conservation est cochée, le passage reste en place et la consigne utilisée, si présente, est recopiée avant la réponse. Sinon, la réponse remplace la sélection. Une consigne seule peut être envoyée sans sélection. La réponse en flux est assemblée avant insertion : l'annulation et les erreurs conservent le passage; l'affichage progressif est suspendu dans cette version. L'option de raisonnement agit sur les blocs `<think>…</think>` présents dans le texte reçu.

## Fichier d'instructions

Le format cible accepte `[Info]` (notes non envoyées), `[Global]` (texte commun), `[PlaceHolder:ID]` (bloc réutilisable sans autre placeholder), `[Instruction:ID]` (instruction avec références `{{ID}}`) et `[Menu]` (arbre de libellés et identifiants). Le fichier `NppOpenAI_instructions.example` illustre la syntaxe. Les anciens blocs `[Prompt:Nom]` restent utilisables; mélanger les deux formats est refusé avec un diagnostic.

Le script `tools/Migrate-Instructions.ps1` convertit une copie de l'ancien fichier en conservant ses instructions et leurs identifiants. Il retire les anciennes lignes demandant au modèle d'interpréter une ligne initiale `//`, puisque le champ Consignes remplit désormais ce rôle. Il ne remplace jamais un fichier de sortie existant :

```powershell
.\tools\Migrate-Instructions.ps1 -InputPath 'chemin\NppOpenAI_instructions' -OutputPath 'chemin\NppOpenAI_instructions.migrated'
```

Le fichier est lu et validé avant l'appel au modèle : identifiants, références, indentation, profondeur, libellés et touches d'accès explicites sont vérifiés. Une erreur empêche l'appel et précise la ligne lorsque la source de l'erreur est localisable.

## Configuration du modèle

Dans `[API]` du `.ini`, une seule ligne `model=` doit être active. Les autres peuvent commencer par `;`; une description finale après `;` est ignorée lors de l'appel à l'API. L'URL Ollama vient toujours du `.ini` et n'est pas inscrite dans le DLL. Dans `[PLUGIN]`, `keep_question=0` et `show_reasoning=0` sont les valeurs par défaut. Les cases de la fenêtre reprennent ces valeurs à chaque ouverture, et leur modification ponctuelle ne les enregistre pas.

## Vérifications

Compilation Release x64 avec Visual Studio 2022. Le test `CatalogProbe` vérifie le nouveau format, l'ancien format et plusieurs erreurs de structure; le fichier migré de 22 instructions passe la validation. Le test `ChooserSmoke` ouvre la vraie boîte de dialogue et vérifie le menu, la recherche accentuée, les instructions récentes et les deux options. Le fonctionnement dans l'installation active de Notepad++ et la réponse du serveur après installation restent à vérifier sur place.
