param([string]$QtPrefix = (Join-Path $PSScriptRoot 'dependencies/mingw64'), [string]$CompilerPrefix = 'C:/msys64/mingw64', [switch]$Package)
$ErrorActionPreference = 'Stop'
$ModernRoot = $PSScriptRoot
$env:PATH = "$QtPrefix/bin;$CompilerPrefix/bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$QtPrefix/share/qt6/plugins"
$env:QT_QPA_FONTDIR = 'C:/Windows/Fonts'
python -X utf8 "$ModernRoot/tools/check_translations.py"
if ($LASTEXITCODE) { throw 'Cataloghi delle lingue incompleti' }
cargo test --locked --manifest-path "$ModernRoot/core/Cargo.toml"
if ($LASTEXITCODE) { throw 'Test del motore falliti' }
cargo build --release --locked --manifest-path "$ModernRoot/core/Cargo.toml"
if ($LASTEXITCODE) { throw 'Compilazione del motore fallita' }
cmake -S $ModernRoot -B "$ModernRoot/build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$QtPrefix;$CompilerPrefix" "-DCMAKE_CXX_COMPILER=$CompilerPrefix/bin/g++.exe"
if ($LASTEXITCODE) { throw 'Configurazione GUI fallita' }
cmake --build "$ModernRoot/build" --parallel 4
if ($LASTEXITCODE) { throw 'Compilazione GUI fallita' }
Copy-Item -LiteralPath "$ModernRoot/core/target/release/sn-index.exe" -Destination "$ModernRoot/build/sn-index.exe"
ctest --test-dir "$ModernRoot/build" --output-on-failure
if ($LASTEXITCODE) { Get-Content "$ModernRoot/build/qt-results.txt"; throw 'Test GUI falliti' }
foreach ($suite in @('core_integration.py','workflow_integration.py')) {
    python "$ModernRoot/tests/$suite"
    if ($LASTEXITCODE) { throw "Test di integrazione falliti: $suite" }
}
if ($Package) {
    python "$ModernRoot/tools/package.py" --qt-prefix $QtPrefix
    if ($LASTEXITCODE) { throw 'Creazione pacchetto fallita' }
}
