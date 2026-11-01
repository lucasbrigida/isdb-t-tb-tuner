<#
.SYNOPSIS
    Script de automação para compilação, empacotamento e validação de releases do ISDB-T/ISDB-Tb Tuner Player.

.DESCRIPTION
    Este script automatiza:
    1. A validação da plataforma escolhida (suporte atual: Windows x64).
    2. A extração da versão a partir de CMakeLists.txt.
    3. A compilação do projeto em modo Release (CMake / MinGW).
    4. A execução da suíte de testes unitários determinísticos (CTest).
    5. A verificação e coleta de dependências, utilitários (Zadig, MPV), scripts e licença (GPLv3).
    6. O empacotamento do arquivo .zip em releases/<plataforma>/yyyymmdd_v<versao>.zip.
    7. A validação estrita do conteúdo do .zip gerado (integridade de arquivos e tamanhos).
    8. O cálculo e exibição do hash SHA-256.

.PARAMETER Platform
    Plataforma de destino: 'windows' (padrão), 'linux' ou 'macos'.

.PARAMETER SkipTests
    Pula a execução dos testes unitários CTest.

.PARAMETER SkipBuild
    Pula a compilação do executável caso os binários já estejam prontos em build/.

.PARAMETER Clean
    Limpa o diretório build/ antes de compilar.

.EXAMPLE
    .\scripts\build_release.ps1 -Platform windows
    .\scripts\build_release.ps1 -Platform windows -SkipTests
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('windows', 'linux', 'macos')]
    [string]$Platform = 'windows',

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
Write-Host "  ISDB-T / ISDB-Tb Tuner Player - Automação de Release ($Platform)" -ForegroundColor Cyan
Write-Host "==============================================================================`n" -ForegroundColor Cyan

# 1. Validação de Plataforma
if ($Platform -ne 'windows') {
    Write-Host "[INFO] Plataforma selecionada: '$Platform'" -ForegroundColor Yellow
    Write-Host "[AVISO] O ambiente atual suporta compilação e empacotamento nativo no Windows x64." -ForegroundColor Yellow
    Write-Host "[AVISO] O empacotamento nativo para $Platform requer um host correspondente ou toolchain dedicado." -ForegroundColor Yellow
    Write-Host "[INFO] O diretório 'releases/$Platform' está pronto para futuras releases." -ForegroundColor Green
    exit 0
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
$releaseName = "${date}_v${version}"
$zipFileName = "${releaseName}.zip"
$releasesDir = Join-Path $repoRoot "releases\$Platform"
$zipPath = Join-Path $releasesDir $zipFileName
$stagingDir = Join-Path $repoRoot "build\release_staging"

New-Item -ItemType Directory -Force -Path $releasesDir | Out-Null

Write-Host "⚙️  Metadados da Release:" -ForegroundColor White
Write-Host "   - Versão do Projeto: $version" -ForegroundColor Gray
Write-Host "   - Data do Build:     $date" -ForegroundColor Gray
Write-Host "   - Arquivo Alvo:      $zipPath" -ForegroundColor Gray
Write-Host ""

# 3. Limpeza opcional
if ($Clean) {
    Write-Host "🧹 Limpando diretório build/..." -ForegroundColor Yellow
    if (Test-Path "build") {
        Remove-Item -Recurse -Force "build"
    }
}

# 4. Compilação Release
if (-not $SkipBuild) {
    if (-not (Test-Path "build")) {
        Write-Host "🔨 Configurando CMake (MinGW Makefiles, Release)..." -ForegroundColor Cyan
        cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
        if ($LASTEXITCODE -ne 0) { throw "Falha na configuração do CMake." }
    }
    Write-Host "🔨 Compilando isdbtb_rx..." -ForegroundColor Cyan
    cmake --build build -j
    if ($LASTEXITCODE -ne 0) { throw "Falha na compilação do executável." }
} else {
    Write-Host "⏩ Compilação ignorada (-SkipBuild ativo)." -ForegroundColor Yellow
}

# 5. Execução dos Testes Unitários
if (-not $SkipTests) {
    Write-Host "🧪 Executando suíte de testes unitários (CTest)..." -ForegroundColor Cyan
    ctest --test-dir build --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "Erro: Um ou mais testes unitários falharam! A release foi cancelada."
    }
    Write-Host "✅ Todos os testes foram aprovados com 100% de sucesso!" -ForegroundColor Green
} else {
    Write-Host "⏩ Testes unitários ignorados (-SkipTests ativo)." -ForegroundColor Yellow
}

# 6. Verificação de Arquivos Obrigatórios
Write-Host "`n🔍 Verificando integridade das dependências e ferramentas..." -ForegroundColor Cyan

$requiredSourceFiles = @(
    "build\isdbtb_rx.exe",
    "build\librtlsdr.dll",
    "build\libusb-1.0.dll",
    "build\libwinpthread-1.dll",
    "LICENSE",
    "scripts\play_mpv.bat",
    "tools\mpv\mpv.exe",
    "tools\mpv\mpv.conf",
    "tools\mpv\d3dcompiler_43.dll"
)

foreach ($file in $requiredSourceFiles) {
    if (-not (Test-Path $file)) {
        throw "Erro: Arquivo obrigatório não encontrado: $file"
    }
}

# Checa ou baixa o zadig.exe se ausente
$zadigPath = "tools\zadig.exe"
if (-not (Test-Path $zadigPath)) {
    Write-Host "⬇️  Zadig não encontrado localmente. Baixando Zadig 2.9 oficial..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path "tools" | Out-Null
    curl.exe -L -o $zadigPath https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $zadigPath)) {
        throw "Erro ao baixar o utilitário Zadig."
    }
}
Write-Host "✅ Todas as ferramentas e dependências estão presentes." -ForegroundColor Green

# 7. Montagem do Diretório de Staging
Write-Host "`n📦 Montando estrutura da release em staging..." -ForegroundColor Cyan
if (Test-Path $stagingDir) {
    Remove-Item -Recurse -Force $stagingDir
}
New-Item -ItemType Directory -Force -Path "$stagingDir\scripts", "$stagingDir\tools\mpv" | Out-Null

Copy-Item "build\isdbtb_rx.exe" "$stagingDir\"
if (Test-Path "build\librtlsdr.dll") {
    Copy-Item "build\librtlsdr.dll" "$stagingDir\"
    Copy-Item "build\libusb-1.0.dll" "$stagingDir\"
    Copy-Item "build\libwinpthread-1.dll" "$stagingDir\"
} else {
    Copy-Item "tools\rtl-sdr\librtlsdr.dll" "$stagingDir\"
    Copy-Item "tools\rtl-sdr\libusb-1.0.dll" "$stagingDir\"
    Copy-Item "tools\rtl-sdr\libwinpthread-1.dll" "$stagingDir\"
}
Copy-Item $zadigPath "$stagingDir\zadig.exe"
Copy-Item "LICENSE" "$stagingDir\LICENSE"

# Copia README da release
if (Test-Path "docs\RELEASE_README.md") {
    Copy-Item "docs\RELEASE_README.md" "$stagingDir\README.md"
} elseif (Test-Path "README.md") {
    Copy-Item "README.md" "$stagingDir\README.md"
} else {
    throw "Arquivo README.md não encontrado para inclusão na release."
}

if (Test-Path "README_en.md") {
    Copy-Item "README_en.md" "$stagingDir\README_en.md"
}

# Scripts
Copy-Item "scripts\play_mpv.bat" "$stagingDir\scripts\"
if (Test-Path "scripts\play_mpv.ps1") {
    Copy-Item "scripts\play_mpv.ps1" "$stagingDir\scripts\"
}

# Ferramentas MPV
Copy-Item "tools\mpv\mpv.exe" "$stagingDir\tools\mpv\"
Copy-Item "tools\mpv\mpv.conf" "$stagingDir\tools\mpv\"
Copy-Item "tools\mpv\d3dcompiler_43.dll" "$stagingDir\tools\mpv\"

# 8. Criação do Arquivo ZIP
Write-Host "🗜️  Comprimindo arquivos para $zipPath..." -ForegroundColor Cyan
if (Test-Path $zipPath) {
    Remove-Item -Force $zipPath
}

# Utiliza a API nativa .NET ZipFile para garantir 100% de compatibilidade com o Windows Explorer (evita prefixo './' do bsdtar que faz o Explorer mostrar pasta vazia)
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stagingDir, $zipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)

if (-not (Test-Path $zipPath)) {
    throw "Erro ao criar o arquivo ZIP da release."
}

# 9. Validação Estrita do Conteúdo do Arquivo ZIP Gerado
Write-Host "`n🔎 Validando conteúdo do arquivo .zip gerado..." -ForegroundColor Cyan

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipArchive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path $zipPath).Path)

$expectedEntries = @{
    "isdbtb_rx.exe"               = 500000;   # > 500 KB
    "librtlsdr.dll"               = 50000;    # > 50 KB
    "libusb-1.0.dll"              = 50000;    # > 50 KB
    "libwinpthread-1.dll"         = 20000;    # > 20 KB
    "zadig.exe"                   = 1000000;  # > 1 MB
    "LICENSE"                     = 10000;    # > 10 KB
    "README.md"                   = 2000;     # > 2 KB
    "README_en.md"                = 2000;     # > 2 KB
    "scripts/play_mpv.bat"        = 200;      # > 200 bytes
    "tools/mpv/mpv.exe"           = 50000000; # > 50 MB
    "tools/mpv/mpv.conf"          = 500;      # > 500 bytes
    "tools/mpv/d3dcompiler_43.dll"= 1000000;  # > 1 MB
}

$zipEntriesMap = @{}
foreach ($entry in $zipArchive.Entries) {
    $normalized = $entry.FullName.Replace('\', '/').TrimStart('./')
    if (-not [string]::IsNullOrWhiteSpace($normalized) -and -not $normalized.EndsWith('/')) {
        $zipEntriesMap[$normalized] = $entry.Length
    }
}
$zipArchive.Dispose()

$validationFailed = $false
foreach ($expectedKey in $expectedEntries.Keys) {
    if (-not $zipEntriesMap.ContainsKey($expectedKey)) {
        Write-Host "❌ ARQUIVO AUSENTE NO ZIP: $expectedKey" -ForegroundColor Red
        $validationFailed = $true
    } else {
        $size = $zipEntriesMap[$expectedKey]
        $minSize = $expectedEntries[$expectedKey]
        if ($size -lt $minSize) {
            Write-Host "❌ ARQUIVO COM TAMANHO ANÔMALO: $expectedKey ($size bytes < $minSize bytes)" -ForegroundColor Red
            $validationFailed = $true
        } else {
            Write-Host "  ✓ $expectedKey ($([math]::Round($size / 1KB, 1)) KB)" -ForegroundColor Green
        }
    }
}

if ($validationFailed) {
    throw "Erro: Falha na validação do pacote .zip gerado! Um ou mais arquivos obrigatórios estão corrompidos ou ausentes."
}
Write-Host "✅ Validação de integridade do .zip aprovada com sucesso!" -ForegroundColor Green

# 10. Limpeza do Diretório de Staging
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $stagingDir

# 11. Hash Criptográfico e Sumário
$zipItem = Get-Item $zipPath
$zipSizeMB = [math]::Round($zipItem.Length / 1MB, 2)
$hash = (Get-FileHash $zipPath -Algorithm SHA256).Hash

Write-Host "`n==============================================================================" -ForegroundColor Green
Write-Host "  🎉 RELEASE GERADA E VALIDADA COM SUCESSO!" -ForegroundColor Green
Write-Host "==============================================================================" -ForegroundColor Green
Write-Host "  Plataforma:  $Platform" -ForegroundColor White
Write-Host "  Arquivo:     $($zipItem.Name)" -ForegroundColor White
Write-Host "  Caminho:     $zipPath" -ForegroundColor White
Write-Host "  Tamanho:     $zipSizeMB MB ($($zipItem.Length) bytes)" -ForegroundColor White
Write-Host "  SHA-256:     $hash" -ForegroundColor Yellow
Write-Host "==============================================================================`n" -ForegroundColor Green

