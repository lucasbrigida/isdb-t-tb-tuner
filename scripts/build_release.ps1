<#
.SYNOPSIS
    Script de automação para compilação, empacotamento e validação de releases do ISDB-T/ISDB-Tb Tuner Player.

.DESCRIPTION
    Este script automatiza:
    1. A validação da plataforma escolhida (suporte atual: Windows).
    2. O suporte à compilação multi-arquitetura: 64 bits (x64) e 32 bits (x86).
    3. A extração da versão a partir de CMakeLists.txt.
    4. A compilação do projeto em modo Release (CMake / MinGW) para cada arquitetura.
    5. A execução da suíte de testes unitários determinísticos (CTest).
    6. A verificação e coleta de dependências de 32 e 64 bits (RTL-SDR, Zadig, MPV), scripts e licença (GPLv3).
    7. O empacotamento dos arquivos .zip em releases/<plataforma>/yyyymmdd_v<versao>_<arch>.zip.
    8. A validação estrita do conteúdo e cabeçalhos PE (x86-64 vs i386) dos .zip gerados.
    9. O cálculo e exibição do hash SHA-256 de cada release.

.PARAMETER Platform
    Plataforma de destino: 'windows' (padrão), 'linux' ou 'macos'.

.PARAMETER Arch
    Arquitetura de destino: 'all' (padrão, gera ambos os zips), 'x64' ou 'x86'.

.PARAMETER SkipTests
    Pula a execução dos testes unitários CTest.

.PARAMETER SkipBuild
    Pula a compilação do executável caso os binários já estejam prontos.

.PARAMETER Clean
    Limpa os diretórios de build antes de compilar.

.EXAMPLE
    .\scripts\build_release.ps1
    .\scripts\build_release.ps1 -Arch all
    .\scripts\build_release.ps1 -Arch x64
    .\scripts\build_release.ps1 -Arch x86
    .\scripts\build_release.ps1 -Platform windows -Arch all -SkipTests
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('windows', 'linux', 'macos')]
    [string]$Platform = 'windows',

    [Parameter(Position = 1)]
    [ValidateSet('all', 'x64', 'x86', '64', '32')]
    [string]$Arch = 'all',

    [switch]$SkipTests,
    [switch]$SkipBuild,
    [switch]$Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Garante execução a partir da raiz do repositório
$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $repoRoot

Write-Host "`n==============================================================================" -ForegroundColor Cyan
Write-Host "  ISDB-T / ISDB-Tb Tuner Player - Automação de Release ($Platform - $Arch)" -ForegroundColor Cyan
Write-Host "==============================================================================`n" -ForegroundColor Cyan

# 1. Validação de Plataforma
if ($Platform -ne 'windows') {
    Write-Host "[INFO] Plataforma selecionada: '$Platform'" -ForegroundColor Yellow
    Write-Host "[AVISO] O ambiente atual suporta compilação e empacotamento nativo no Windows (x64 e x86)." -ForegroundColor Yellow
    Write-Host "[AVISO] O empacotamento nativo para $Platform requer um host correspondente ou toolchain dedicado." -ForegroundColor Yellow
    Write-Host "[INFO] O diretório 'releases/$Platform' está pronto para futuras releases." -ForegroundColor Green
    exit 0
}

# Normaliza lista de arquiteturas
$targetArchs = @()
if ($Arch -eq 'all') {
    $targetArchs = @('x64', 'x86')
} elseif ($Arch -in @('x64', '64')) {
    $targetArchs = @('x64')
} elseif ($Arch -in @('x86', '32')) {
    $targetArchs = @('x86')
}

# 2. Obtenção da Versão e Data
$version = "1.0.0"
if (Test-Path "CMakeLists.txt") {
    $cmakeContent = Get-Content "CMakeLists.txt" -Raw
    if ($cmakeContent -match 'project\s*\(\s*isdbtb_rx\s+VERSION\s+([0-9\.]+)') {
        $version = $matches[1]
    }
}
$date = Get-Date -Format 'yyyyMMdd'
$releasesDir = Join-Path $repoRoot "releases\$Platform"
New-Item -ItemType Directory -Force -Path $releasesDir | Out-Null

Write-Host "⚙️  Metadados Globais da Release:" -ForegroundColor White
Write-Host "   - Versão do Projeto: $version" -ForegroundColor Gray
Write-Host "   - Data do Build:     $date" -ForegroundColor Gray
Write-Host "   - Arquiteturas Alvo: $($targetArchs -join ', ')" -ForegroundColor Gray
Write-Host "   - Diretório Release: $releasesDir" -ForegroundColor Gray
Write-Host ""

# 3. Garantir Dependências Externas Necessárias (Zadig, RTL-SDR e MPV)
function Ensure-GlobalDependencies {
    # 3.1 Zadig (universal 32/64 bits)
    $zadigPath = "tools\zadig.exe"
    if (-not (Test-Path $zadigPath)) {
        Write-Host "⬇️  Zadig não encontrado localmente. Baixando Zadig 2.9 oficial..." -ForegroundColor Yellow
        New-Item -ItemType Directory -Force -Path "tools" | Out-Null
        curl.exe -L -o $zadigPath https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $zadigPath)) {
            throw "Erro ao baixar o utilitário Zadig."
        }
    }

    # 3.2 RTL-SDR x64
    New-Item -ItemType Directory -Force -Path "tools\rtl-sdr\x64" | Out-Null
    if (Test-Path "tools\rtl-sdr\librtlsdr.dll") {
        Copy-Item "tools\rtl-sdr\*.dll" "tools\rtl-sdr\x64\" -Force
    }

    # 3.3 RTL-SDR x86
    New-Item -ItemType Directory -Force -Path "tools\rtl-sdr\x86" | Out-Null
    if (-not (Test-Path "tools\rtl-sdr\x86\librtlsdr.dll")) {
        Write-Host "⬇️  Baixando librtlsdr.dll oficial Osmocom de 32 bits..." -ForegroundColor Yellow
        $w32Zip = "tools\rtl-sdr\x86\rtlsdr_w32.zip"
        curl.exe -L -o $w32Zip https://ftp.osmocom.org/binaries/windows/rtl-sdr/rtl-sdr-32bit-20261018.zip
        tar.exe -xf $w32Zip --strip-components=1 -C "tools\rtl-sdr\x86"
        Remove-Item -Force $w32Zip
    }

    # 3.4 MPV x86
    New-Item -ItemType Directory -Force -Path "tools\mpv\x86" | Out-Null
    if (-not (Test-Path "tools\mpv\x86\mpv.exe")) {
        $mpv32Archive = "tools\mpv\x86\mpv32.7z"
        if (-not (Test-Path $mpv32Archive)) {
            Write-Host "⬇️  Baixando MPV oficial estável de 32 bits (i686 v0.41.0)..." -ForegroundColor Yellow
            curl.exe -L -o $mpv32Archive "https://sourceforge.net/projects/mpv-player-windows/files/release/mpv-0.41.0-i686.7z/download"
        }
        if (Test-Path $mpv32Archive) {
            Write-Host "📦 Extraindo MPV 32 bits..." -ForegroundColor Cyan
            tar.exe -xf $mpv32Archive -C "tools\mpv\x86"
        }
    }
    if ((Test-Path "tools\mpv\mpv.conf") -and -not (Test-Path "tools\mpv\x86\mpv.conf")) {
        Copy-Item "tools\mpv\mpv.conf" "tools\mpv\x86\mpv.conf" -Force
    }
}

Ensure-GlobalDependencies

# 4. Função Principal de Build, Teste e Empacotamento para cada Arquitetura
function Build-And-Package-Release([string]$targetArch) {
    Write-Host "`n==============================================================================" -ForegroundColor Magenta
    Write-Host "  Processando Arquitetura: $targetArch" -ForegroundColor Magenta
    Write-Host "==============================================================================" -ForegroundColor Magenta

    $buildDir = "build_$targetArch"
    $stagingDir = "$buildDir\release_staging"
    $zipArchTag = if ($targetArch -eq 'x64') { 'win64' } else { 'win32' }
    $zipFileName = "${date}_v${version}_${zipArchTag}.zip"
    $zipPath = Join-Path $releasesDir $zipFileName

    # Limpeza opcional
    if ($Clean -and (Test-Path $buildDir)) {
        Write-Host "🧹 Limpando diretório $buildDir..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildDir
    }

    # Compilação
    if (-not $SkipBuild) {
        if (-not (Test-Path $buildDir)) {
            Write-Host "🔨 Configurando CMake ($targetArch, Release)..." -ForegroundColor Cyan
            cmake -S . -B $buildDir -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release "-DTARGET_ARCH=$targetArch" -DISDBT_ENABLE_GRISDBT_SYNC=ON
            if ($LASTEXITCODE -ne 0) { throw "Falha na configuração do CMake para $targetArch." }
        }
        Write-Host "🔨 Compilando isdbtb_rx ($targetArch)..." -ForegroundColor Cyan
        cmake --build $buildDir -j
        if ($LASTEXITCODE -ne 0) { throw "Falha na compilação do executável para $targetArch." }
    } else {
        Write-Host "⏩ Compilação ignorada (-SkipBuild ativo)." -ForegroundColor Yellow
    }

    # Testes Unitários
    if (-not $SkipTests) {
        Write-Host "🧪 Executando testes unitários CTest ($targetArch)..." -ForegroundColor Cyan
        ctest --test-dir $buildDir --output-on-failure -E BenchSync
        if ($LASTEXITCODE -ne 0) {
            throw "Erro: Um ou mais testes unitários falharam para $targetArch! Release abortada."
        }
        Write-Host "✅ Testes unitários para $targetArch aprovados com sucesso!" -ForegroundColor Green
    } else {
        Write-Host "⏩ Testes unitários ignorados (-SkipTests ativo)." -ForegroundColor Yellow
    }

    # Montagem do Staging
    Write-Host "`n📦 Montando release $targetArch em staging ($stagingDir)..." -ForegroundColor Cyan
    if (Test-Path $stagingDir) {
        Remove-Item -Recurse -Force $stagingDir
    }
    New-Item -ItemType Directory -Force -Path "$stagingDir\scripts", "$stagingDir\tools\mpv" | Out-Null

    # Executável principal
    $mainExe = "$buildDir\isdbtb_rx.exe"
    if (-not (Test-Path $mainExe)) {
        throw "Erro: Executável principal não encontrado em $mainExe"
    }
    Copy-Item $mainExe "$stagingDir\"

    # DLLs de RTL-SDR de acordo com a arquitetura
    Copy-Item "tools\rtl-sdr\$targetArch\*.dll" "$stagingDir\" -Force

    # Ferramenta MPV
    if ($targetArch -eq 'x64') {
        $mpvSrc = if (Test-Path "tools\mpv\x64\mpv.exe") { "tools\mpv\x64" } else { "tools\mpv" }
        Copy-Item "$mpvSrc\mpv.exe" "$stagingDir\tools\mpv\"
        Copy-Item "$mpvSrc\mpv.conf" "$stagingDir\tools\mpv\"
        if (Test-Path "$mpvSrc\d3dcompiler_43.dll") {
            Copy-Item "$mpvSrc\d3dcompiler_43.dll" "$stagingDir\tools\mpv\"
        }
    } else {
        Copy-Item "tools\mpv\x86\mpv.exe" "$stagingDir\tools\mpv\"
        Copy-Item "tools\mpv\x86\mpv.conf" "$stagingDir\tools\mpv\"
        if (Test-Path "tools\mpv\x86\d3dcompiler_43.dll") {
            Copy-Item "tools\mpv\x86\d3dcompiler_43.dll" "$stagingDir\tools\mpv\"
        }
    }

    # Zadig, Licença e Documentação
    Copy-Item "tools\zadig.exe" "$stagingDir\zadig.exe"
    Copy-Item "LICENSE" "$stagingDir\LICENSE"
    if (Test-Path "docs\RELEASE_README.md") {
        Copy-Item "docs\RELEASE_README.md" "$stagingDir\README.md"
    } elseif (Test-Path "README.md") {
        Copy-Item "README.md" "$stagingDir\README.md"
    }
    if (Test-Path "README_en.md") {
        Copy-Item "README_en.md" "$stagingDir\README_en.md"
    }
    Copy-Item "scripts\play_mpv.bat" "$stagingDir\scripts\"
    if (Test-Path "scripts\play_mpv_x86.bat") {
        Copy-Item "scripts\play_mpv_x86.bat" "$stagingDir\scripts\"
    }
    if (Test-Path "scripts\play_mpv.ps1") {
        Copy-Item "scripts\play_mpv.ps1" "$stagingDir\scripts\"
    }

    # Compressão ZIP (.NET ZipFile)
    Write-Host "🗜️  Comprimindo arquivos para $zipPath..." -ForegroundColor Cyan
    if (Test-Path $zipPath) {
        Remove-Item -Force $zipPath
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory($stagingDir, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

    if (-not (Test-Path $zipPath)) {
        throw "Erro ao criar o arquivo ZIP da release ($zipPath)."
    }

    # Validação Estrita do Conteúdo e Arquitetura PE
    Write-Host "🔎 Validando integridade e arquitetura PE do .zip gerado..." -ForegroundColor Cyan
    $expectedPeArch = if ($targetArch -eq 'x64') { 'pei-x86-64' } else { 'pei-i386' }

    $exeArchOut = objdump.exe -f "$stagingDir\isdbtb_rx.exe" 2>&1 | Out-String
    if ($exeArchOut -notmatch $expectedPeArch) {
        throw "Erro Crítico: isdbtb_rx.exe não possui a arquitetura esperada $expectedPeArch!"
    }
    Write-Host "  ✓ Arquitetura PE de isdbtb_rx.exe confirmada: $expectedPeArch" -ForegroundColor Green

    $mpvArchOut = objdump.exe -f "$stagingDir\tools\mpv\mpv.exe" 2>&1 | Out-String
    if ($mpvArchOut -notmatch $expectedPeArch) {
        throw "Erro Crítico: tools/mpv/mpv.exe não possui a arquitetura esperada $expectedPeArch!"
    }
    Write-Host "  ✓ Arquitetura PE de tools/mpv/mpv.exe confirmada: $expectedPeArch" -ForegroundColor Green

    # Teste de fumaça: executa mpv --version no staging para garantir que não ocorram crashes ou erros de OpenSSL
    $mpvSmokeOut = & "$stagingDir\tools\mpv\mpv.exe" --version 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $mpvSmokeOut -match "assertion failed" -or $mpvSmokeOut -match "OpenSSL internal error") {
        throw "Erro Crítico: tools/mpv/mpv.exe falhou no teste de fumaça: $mpvSmokeOut"
    }
    Write-Host "  ✓ Teste de fumaça do MPV ($targetArch) aprovado sem erros de runtime/OpenSSL" -ForegroundColor Green

    # Limpeza de staging
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $stagingDir

    # Resumo
    $zipItem = Get-Item $zipPath
    $zipSizeMB = [math]::Round($zipItem.Length / 1MB, 2)
    $hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash

    $script:ReleaseResults.Add([PSCustomObject]@{
        Arch    = $targetArch
        Tag     = $zipArchTag
        File    = $zipItem.Name
        Path    = $zipPath
        SizeMB  = $zipSizeMB
        Bytes   = $zipItem.Length
        Hash    = $hash
    })
}

# 5. Execução do Loop de Releases
$script:ReleaseResults = [System.Collections.Generic.List[object]]::new()
foreach ($a in $targetArchs) {
    Build-And-Package-Release -targetArch $a
}

# 6. Sumário Final
Write-Host "`n==============================================================================" -ForegroundColor Green
Write-Host "  🎉 TODAS AS RELEASES FORAM GERADAS E VALIDADAS COM SUCESSO!" -ForegroundColor Green
Write-Host "==============================================================================" -ForegroundColor Green
foreach ($r in $script:ReleaseResults) {
    $rArch = $r.Arch
    $rTag = $r.Tag
    $rFile = $r.File
    $rPath = $r.Path
    $rSizeMB = $r.SizeMB
    $rBytes = $r.Bytes
    $rHash = $r.Hash
    Write-Host "  [Release $rArch / $rTag]" -ForegroundColor White
    Write-Host "    Arquivo:  $rFile" -ForegroundColor Gray
    Write-Host "    Caminho:  $rPath" -ForegroundColor Gray
    Write-Host "    Tamanho:  ${rSizeMB} MB (${rBytes} bytes)" -ForegroundColor Gray
    Write-Host "    SHA-256:  $rHash" -ForegroundColor Yellow
    Write-Host ""
}
Write-Host "==============================================================================`n" -ForegroundColor Green
