param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [string]$OutputDir = "SpiceBin\research\bin_gvr_blocks",

    [string]$SpiceFileParsingExe = ".\x64\Debug\SpiceFileParsing.exe",

    [switch]$SkipParse
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$aklzMagic = [byte[]](0x41, 0x4b, 0x4c, 0x5a, 0x7e, 0x3f, 0x51, 0x64, 0x3d, 0xcc, 0xcc, 0xcd)

function Read-U32Be {
    param(
        [byte[]]$Bytes,
        [int]$Offset
    )

    return ([uint32]$Bytes[$Offset] -shl 24) -bor
        ([uint32]$Bytes[$Offset + 1] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 8) -bor
        [uint32]$Bytes[$Offset + 3]
}

function Read-U32Le {
    param(
        [byte[]]$Bytes,
        [int]$Offset
    )

    return [uint32]$Bytes[$Offset] -bor
        ([uint32]$Bytes[$Offset + 1] -shl 8) -bor
        ([uint32]$Bytes[$Offset + 2] -shl 16) -bor
        ([uint32]$Bytes[$Offset + 3] -shl 24)
}

function Test-Tag {
    param(
        [byte[]]$Bytes,
        [int]$Offset,
        [string]$Tag
    )

    if ($Offset -lt 0 -or $Offset + $Tag.Length -gt $Bytes.Length) {
        return $false
    }

    for ($i = 0; $i -lt $Tag.Length; ++$i) {
        if ($Bytes[$Offset + $i] -ne [byte][char]$Tag[$i]) {
            return $false
        }
    }
    return $true
}

function Test-Aklz {
    param([byte[]]$Bytes)

    if ($Bytes.Length -lt $aklzMagic.Length) {
        return $false
    }

    for ($i = 0; $i -lt $aklzMagic.Length; ++$i) {
        if ($Bytes[$i] -ne $aklzMagic[$i]) {
            return $false
        }
    }
    return $true
}

function Expand-Aklz {
    param([byte[]]$Bytes)

    if (-not (Test-Aklz $Bytes)) {
        return $Bytes
    }
    if ($Bytes.Length -lt 0x10) {
        throw "AKLZ input is too small for its header."
    }

    $decompressedSize = [int](Read-U32Be $Bytes 0x0c)
    $output = [byte[]]::new($decompressedSize)
    $window = [byte[]]::new(0x1000)
    $windowWrite = 0
    $cursor = 0x10
    $outCursor = 0
    $flags = 0
    $bitsRemaining = 0

    while ($outCursor -lt $decompressedSize) {
        if ($bitsRemaining -eq 0) {
            if ($cursor -ge $Bytes.Length) {
                throw "AKLZ flag stream is truncated."
            }
            $flags = [int]$Bytes[$cursor]
            ++$cursor
            $bitsRemaining = 8
        }

        $literal = (($flags -band 1) -ne 0)
        $flags = $flags -shr 1
        --$bitsRemaining

        if ($literal) {
            if ($cursor -ge $Bytes.Length) {
                throw "AKLZ literal stream is truncated."
            }
            $value = $Bytes[$cursor]
            ++$cursor
            $output[$outCursor] = $value
            ++$outCursor
            $window[$windowWrite] = $value
            $windowWrite = ($windowWrite + 1) -band 0x0fff
            continue
        }

        if ($cursor + 1 -ge $Bytes.Length) {
            throw "AKLZ back-reference stream is truncated."
        }
        $b1 = [int]$Bytes[$cursor]
        $b2 = [int]$Bytes[$cursor + 1]
        $cursor += 2

        $offset = ((($b2 -shr 4) -shl 8) -bor $b1)
        $length = ($b2 -band 0x0f) + 3
        $offset = (0x1000 + $offset - 0x0fee) -band 0x0fff

        for ($i = 0; $i -lt $length -and $outCursor -lt $decompressedSize; ++$i) {
            $value = $window[($offset + $i) -band 0x0fff]
            $output[$outCursor] = $value
            ++$outCursor
            $window[$windowWrite] = $value
            $windowWrite = ($windowWrite + 1) -band 0x0fff
        }
    }

    return $output
}

function Get-ChunkPayloadSize {
    param(
        [byte[]]$Bytes,
        [int]$Offset
    )

    if ($Offset + 8 -gt $Bytes.Length) {
        return $null
    }

    $remaining = $Bytes.Length - $Offset
    $le = [int](Read-U32Le $Bytes ($Offset + 4))
    if ($le -ge 8 -and $le -le ($remaining - 8)) {
        return $le
    }

    $be = [int](Read-U32Be $Bytes ($Offset + 4))
    if ($be -ge 8 -and $be -le ($remaining - 8)) {
        return $be
    }

    return $null
}

function Get-GvrSourceSize {
    param(
        [byte[]]$Bytes,
        [int]$Offset
    )

    $startsWithIndex = (Test-Tag $Bytes $Offset "GCIX") -or (Test-Tag $Bytes $Offset "GBIX")
    $startsWithGvrt = Test-Tag $Bytes $Offset "GVRT"
    if (-not $startsWithIndex -and -not $startsWithGvrt) {
        return $null
    }

    if ($startsWithGvrt) {
        $gvrtPayloadSize = Get-ChunkPayloadSize $Bytes $Offset
        if ($null -eq $gvrtPayloadSize) {
            return $null
        }
        return 8 + $gvrtPayloadSize
    }

    $indexPayloadSize = Get-ChunkPayloadSize $Bytes $Offset
    if ($null -eq $indexPayloadSize) {
        return $null
    }

    $gvrtOffset = $Offset + 8 + $indexPayloadSize
    if (-not (Test-Tag $Bytes $gvrtOffset "GVRT")) {
        return $null
    }

    $gvrtPayloadSize = Get-ChunkPayloadSize $Bytes $gvrtOffset
    if ($null -eq $gvrtPayloadSize) {
        return $null
    }

    $end = $gvrtOffset + 8 + $gvrtPayloadSize
    if ($end -gt $Bytes.Length) {
        return $null
    }

    return $end - $Offset
}

$inputItem = Get-Item -LiteralPath $InputPath
$rawBytes = [System.IO.File]::ReadAllBytes($inputItem.FullName)
$decodedBytes = Expand-Aklz $rawBytes
$sourceKind = if ((Test-Aklz $rawBytes)) { "aklz-decoded" } else { "raw" }

$root = New-Item -ItemType Directory -Force -Path $OutputDir
$extractDir = New-Item -ItemType Directory -Force -Path (Join-Path $root.FullName "extracted_gvr")
$parseDir = New-Item -ItemType Directory -Force -Path (Join-Path $root.FullName "gvr_parse")
$manifestPath = Join-Path $root.FullName "gvr_blocks.tsv"

$seenStarts = @{}
$rows = New-Object System.Collections.Generic.List[string]
$rows.Add("index`tstartOffset`tgvrtOffset`tsourceSize`tfile")
$index = 0

for ($offset = 0; $offset + 8 -le $decodedBytes.Length; ++$offset) {
    if (-not ((Test-Tag $decodedBytes $offset "GCIX") -or (Test-Tag $decodedBytes $offset "GBIX") -or (Test-Tag $decodedBytes $offset "GVRT"))) {
        continue
    }

    $sourceSize = Get-GvrSourceSize $decodedBytes $offset
    if ($null -eq $sourceSize) {
        continue
    }
    if ($seenStarts.ContainsKey($offset)) {
        continue
    }
    $seenStarts[$offset] = $true

    $slice = [byte[]]::new($sourceSize)
    [Array]::Copy($decodedBytes, $offset, $slice, 0, $sourceSize)

    $gvrtOffset = if (Test-Tag $decodedBytes $offset "GVRT") {
        $offset
    } else {
        $offset + 8 + (Get-ChunkPayloadSize $decodedBytes $offset)
    }

    $name = "{0}_{1:x8}.gvr" -f $inputItem.BaseName, $offset
    $path = Join-Path $extractDir.FullName $name
    [System.IO.File]::WriteAllBytes($path, $slice)
    $rows.Add(("{0}`t0x{1:x}`t0x{2:x}`t{3}`t{4}" -f $index, $offset, $gvrtOffset, $sourceSize, $path))
    ++$index
}

[System.IO.File]::WriteAllLines($manifestPath, $rows)

Write-Host "input=$($inputItem.FullName)"
Write-Host "sourceKind=$sourceKind"
Write-Host "rawSize=$($rawBytes.Length)"
Write-Host "decodedSize=$($decodedBytes.Length)"
Write-Host "gvrBlockCount=$index"
Write-Host "manifest=$manifestPath"
Write-Host "extractDir=$($extractDir.FullName)"

if ($index -eq 0 -or $SkipParse) {
    if ($index -eq 0) {
        Write-Host "parseStatus=skipped-no-gvr-blocks"
    } else {
        Write-Host "parseStatus=skipped-by-request"
    }
    exit 0
}

$parser = Resolve-Path -LiteralPath $SpiceFileParsingExe -ErrorAction SilentlyContinue
if ($null -eq $parser) {
    Write-Host "parseStatus=skipped-parser-not-found"
    Write-Host "parser=$SpiceFileParsingExe"
    exit 0
}

& $parser.Path $extractDir.FullName $parseDir.FullName --gvr-only --export-gvr-image-ir
$exitCode = $LASTEXITCODE
Write-Host "parseStatus=ran"
Write-Host "parseExitCode=$exitCode"
Write-Host "parseDir=$($parseDir.FullName)"
exit $exitCode
