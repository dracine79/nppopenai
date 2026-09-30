# Spécification — Sélecteur d’instructions personnalisable

**Statut :** implémenté dans le fork personnalisé. Compilation et tests du parseur et de la fenêtre réalisés; essai final dans l’installation active de Notepad++ à effectuer après installation.

## 1. Objectif

Permettre de choisir rapidement une instruction NPPOpenAI au clavier ou à la souris, tout en laissant l’utilisateur définir lui-même les libellés, l’ordre et la hiérarchie des menus dans `NppOpenAI_instructions`. Les préfixes actuels `E-`, `T-`, `F-` et `C-` ne doivent plus déterminer l’interface.

Une seule instruction est exécutée par appel. Le texte du document, la consigne ponctuelle saisie dans la fenêtre et le contenu de l’instruction demeurent trois données distinctes.

## 2. Fenêtre de sélection

De haut en bas :

1. Barre de menus Windows construite à partir de la section `Menu` du fichier d’instructions. Un menu ou sous-menu s’ouvre au clic et avec `Alt` suivi de sa touche d’accès; les flèches et `Entrée` permettent de choisir une feuille. Le nom affiché et la touche d’accès ne dépendent pas d’un préfixe imposé.
2. Champ **Consignes**, destiné à une précision ponctuelle libre. Il est vide à l’ouverture et n’est jamais rempli en analysant le texte sélectionné.
3. Champ **Nom de l’instruction**, avec le texte indicatif « Rechercher une instruction… ». Il reçoit le focus à l’ouverture. Choisir une feuille du menu ou un élément récent remplit ce champ et associe l’identifiant réel de l’instruction.
4. Liste de complétion sous le champ Nom : elle apparaît seulement lorsqu’une saisie produit **une à quatre correspondances**. Avec plus de quatre correspondances, un indice demande de préciser la recherche; avec aucune, un message l’indique. `Entrée` ne peut exécuter qu’une instruction identifiée sans ambiguïté.
5. Cinq instructions récentes au maximum, numérotées de 1 à 5. Chaque numéro correspond à une instruction distincte; les choix disparus du fichier sont retirés de l’historique.
6. Cases **Conserver la sélection et la consigne** et **Afficher le raisonnement**, puis commandes de validation et d’annulation. Les valeurs par défaut des deux options dans le `.ini` cible sont `0`; une valeur explicitement réglée par l’utilisateur dans ce fichier initialise la case correspondante.

La recherche ignore la casse et les accents et peut porter sur le libellé visible et son chemin de menu. Des libellés identiques à deux endroits doivent être distingués par leur chemin. Une instruction absente des menus peut rester accessible par recherche si elle existe dans la section `Instruction`.

La fenêtre est redimensionnable. Sa largeur minimale affiche normalement toute la barre de menus; sa hauteur minimale conserve visibles les champs, les options et les commandes. La liste peut défiler si nécessaire. Sa taille maximale ne dépasse pas la fenêtre Notepad++ visible ni la zone de travail du moniteur. Une taille mémorisée est ramenée dans ces limites lorsque la résolution ou le moniteur change. Si la fenêtre Notepad++ devient plus étroite que tous les libellés réunis, les menus doivent rester accessibles par repli ou retour à la ligne : les deux limites de taille ne peuvent alors être satisfaites simultanément sans adaptation.

## 3. Raccourcis et récents

- Le focus initial se trouve dans **Nom de l’instruction**.
- Dans ce champ, une touche **1 à 5** remplit le nom complet de l’instruction récente correspondante et sélectionne son identifiant. Elle **ne lance pas** le traitement; `Entrée` le confirme.
- **8** bascule « Conserver la sélection et la consigne »; **9** bascule « Afficher le raisonnement ». L’état est immédiatement visible et le focus reste sur Nom.
- Pour ne pas empêcher la recherche d’un nom contenant des chiffres, ces touches sont interprétées comme raccourcis lorsque Nom est vide ou contient déjà un nom complet sélectionné. Pendant une saisie partielle, elles restent des caractères ordinaires. Dans Consignes, tous les chiffres restent du texte.
- Les récents sont enregistrés avec l’**identifiant stable** de l’instruction, jamais avec son rang dans le fichier ou dans le menu. Remplir Nom ne modifie pas l’ordre des récents; une validation de l’instruction le met à jour. Les récents sont conservés entre les ouvertures de Notepad++.
- `Échap` ferme une liste ou un menu ouvert, puis annule la fenêtre selon le contexte. Les raccourcis de menu suivent les conventions de Windows; aucune lettre de famille n’est codée en dur.

## 4. Consignes, sélection et réponse

Le sens est **unique** : le champ Consignes alimente la demande; le plugin ne recherche ni `//` ni une autre balise dans la sélection. Le texte sélectionné reste une source à traiter, même s’il contient de tels caractères.

La consigne ponctuelle est envoyée au modèle séparément de l’instruction choisie et du texte source. L’instruction commune, l’instruction choisie et les placeholders sont assemblés une seule fois; les consignes ponctuelles ne sont pas requises dans chaque instruction. Une consigne vide n’ajoute rien.

- Si **Conserver la sélection et la consigne** est coché, la sélection reste intacte. Le plugin ajoute ensuite, si le champ est non vide, une ligne « Consigne appliquée : » suivie de la valeur effectivement envoyée, puis la réponse.
- Si la case n’est pas cochée, la réponse remplace la sélection et la consigne n’est pas recopiée dans le document.
- Une annulation ou une erreur de requête ne doit pas faire perdre la sélection, y compris en mode flux.
- Les états des deux cases s’appliquent à l’appel courant. À l’appel suivant, ils sont réinitialisés selon les valeurs du `.ini`; une case ponctuelle ne modifie pas ce fichier silencieusement.

Le libellé « Afficher le raisonnement » doit correspondre au comportement effectif : avec l’API actuelle, il concerne notamment les blocs `<think>`. Le filtrage en flux doit tenir compte d’une balise coupée entre plusieurs fragments.

## 5. Structure cible de `NppOpenAI_instructions`

La syntaxe détaillée ci-dessous est celle du format implémenté. La section existante `[Global]` reste également disponible.

| Section | Contenu et effet |
|---|---|
| `Info` | Notes et repères humains; jamais envoyés au modèle. |
| `PlaceHolder` | Blocs nommés de texte réutilisable. Un placeholder ne peut contenir ni appeler un autre placeholder. |
| `Instruction` | Blocs nommés par un identifiant stable; leur contenu peut insérer des placeholders. |
| `Menu` | Arbre indenté dont les parents sont des libellés de menu et les feuilles des libellés pointant vers un identifiant d’instruction. L’ordre du fichier fixe l’ordre affiché. |

La profondeur maximale est de **trois niveaux, feuille comprise** : menu → sous-menu → instruction. Une instruction peut être placée à plusieurs endroits ou sous plusieurs libellés, sans dupliquer son texte; elle n’apparaît qu’une fois dans les récents. Les identifiants restent distincts des libellés visibles et ne changent pas lorsqu’un menu est renommé.

Notation : un en-tête de bloc nomme chaque placeholder ou instruction; une référence dans le contenu d’une instruction utilise `{{Nom}}`. Dans `Menu`, deux espaces représentent un niveau d’indentation; une feuille associe son libellé à l’identifiant de l’instruction. Le caractère `&`, suivant la convention Windows, désigne la lettre soulignée d’un menu, avec `&&` pour un `&` littéral. Si aucune lettre n’est donnée, une lettre disponible est déterminée automatiquement.

Exemple illustratif du nouveau format :

```text
[Info]
Notes personnelles sur ce fichier.

[PlaceHolder:TON_NEUTRE]
Adopte un ton neutre et clair.

[Instruction:REECRIRE]
{{TON_NEUTRE}}
Réécris le passage en préservant ses faits.

[Instruction:CERNER]
Pose des questions ciblées pour préciser le périmètre.

[Menu]
Édition
  Réécrire = REECRIRE
Commandes
  Exploration
    Cerner = CERNER
```

Les identifiants sont uniques et stables; les libellés du menu sont libres. Une indentation de deux espaces est retenue pour chaque niveau et les tabulations sont rejetées afin qu’un alignement visuel trompeur ne change pas la structure.

La section existante `[Global]` conserve son rôle de texte commun appliqué une fois à chaque instruction pendant la migration. Les anciens blocs `[Prompt:…]` restent fonctionnels dans un fichier ancien; un fichier au nouveau format est traité comme tel, sans interprétation partielle par l’ancien parseur. Installer le nouveau format en même temps que la nouvelle DLL. Aucun contenu de `Info` ou `Menu` n’est transmis au modèle.

## 6. Validation et cas limites

Le fichier est entièrement lu et validé avant de remplir la fenêtre. Une erreur indique la ligne concernée et empêche un appel au modèle avec une instruction incomplète.

À vérifier : identifiants absents ou dupliqués; feuille pointant vers une instruction inconnue; placeholder inconnu ou imbriqué; indentation mélangée ou profondeur excessive; libellé vide; touches d’accès en conflit au même niveau; instruction définie mais absente des menus; encodage UTF-8 ou UTF-16 et caractères accentués; plusieurs menus pointant vers une même instruction; recherche donnant 0, 1, 4 ou 5 résultats; instruction récente supprimée; texte sélectionné contenant `//`; consignes sur plusieurs lignes; erreur ou annulation d’une réponse en flux; changement de taille ou de moniteur.

L’absence de sélection est un point à trancher lors de l’implémentation : le plugin actuel la refuse avant l’ouverture du sélecteur, tandis qu’une consigne seule pourrait suffire à une instruction de génération comme « Écrire ».

## 7. Critères d’acceptation

1. L’utilisateur peut changer les libellés, l’ordre et les sous-menus en éditant uniquement `NppOpenAI_instructions`, puis recharger le fichier sans recompiler la DLL.
2. Le choix d’une instruction par menu, complétion ou numéro récent conduit au même identifiant et au même contenu transmis au modèle.
3. Les touches 1 à 5 remplissent Nom sans exécution; 8 et 9 changent uniquement l’état des cases dans le contexte défini.
4. Les lignes `//` de la sélection ne sont jamais interprétées comme consignes par le plugin.
5. La conservation garde le passage et écrit la consigne ponctuelle réellement utilisée; une erreur ne détruit pas le passage.
6. Un fichier mal formé produit un diagnostic exploitable et ne déclenche aucun appel au modèle.
7. Le menu, les champs et les options restent utilisables au clavier, à la souris, après redimensionnement et avec la mise à l’échelle Windows.
