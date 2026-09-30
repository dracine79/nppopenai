param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$source = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $InputPath).Path)
if ($source -match '(?m)^\[Instruction:' -or $source -match '(?m)^\[Menu\]') {
    throw 'Le fichier semble déjà utiliser le nouveau format.'
}
$headers = [regex]::Matches($source, '(?m)^\[Prompt:([^\]\r\n]+)\][ \t]*\r?$')
if ($headers.Count -eq 0) {
    throw 'Aucun bloc [Prompt:...] à convertir.'
}
if (Test-Path -LiteralPath $OutputPath) {
    throw 'Le fichier de sortie existe déjà. Choisir un autre nom pour préserver son contenu.'
}

$labels = @{
    'Traduire'='Traduire'; 'Definir'='Définir'; 'Ecrire'='Écrire';
    'Reecrire'='Réécrire'; 'Resumer'='Résumer'; 'Raccourcir'='Raccourcir';
    'Allonger'='Allonger'; 'Formel'='Formel'; 'Informel'='Informel';
    'Inspiration'='Inspiration'; 'Humour'='Humour'; 'Paragraphe'='Paragraphe';
    'Liste'='Liste'; 'Business'='Business'; 'Academique'='Académique';
    'SAACS'='SAACS'; 'Actions'='Actions'; 'PMP'='PMP'; 'Dryrun'='Dryrun';
    'Exploser'='Exploser'; 'Cerner'='Cerner'; 'Carto'='Carto'
}
$groups = [ordered]@{
    'E'='&Édition'; 'T'='&Tonalité'; 'F'='&Format'; 'C'='&Commandes'
}
$ids = @($headers | ForEach-Object { $_.Groups[1].Value })
$body = [regex]::Replace($source, '(?m)^\[Prompt:([^\]\r\n]+)\][ \t]*\r?$', '[Instruction:$1]')
$body = [regex]::Replace($body, '(?m)^Une ligne initiale commençant par //[^\r\n]*\r?\n', '')
$body = "[Info]`r`n" + $body.TrimStart([char[]]"`r`n")
$menu = New-Object System.Text.StringBuilder
[void]$menu.AppendLine('[Menu]')
foreach ($group in $groups.GetEnumerator()) {
    $members = @($ids | Where-Object { $_ -like "$($group.Key)-*" })
    if ($members.Count -eq 0) { continue }
    [void]$menu.AppendLine($group.Value)
    foreach ($id in $members) {
        $short = $id.Substring(2)
        $label = if ($labels.ContainsKey($short)) { $labels[$short] } else { $short }
        [void]$menu.AppendLine("  $label = $id")
    }
}
$unknown = @($ids | Where-Object { $_ -notmatch '^[ETFC]-' })
if ($unknown.Count -gt 0) {
    [void]$menu.AppendLine('&Autres')
    foreach ($id in $unknown) { [void]$menu.AppendLine("  $id = $id") }
}
$output = $body.TrimEnd() + "`r`n`r`n" + $menu.ToString()
[System.IO.File]::WriteAllText($OutputPath, $output, (New-Object System.Text.UTF8Encoding($false)))
Write-Output "Conversion terminée : $($ids.Count) instructions. Fichier source préservé."
