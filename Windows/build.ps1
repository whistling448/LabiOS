$ErrorActionPreference = 'Stop'
Push-Location -LiteralPath $PSScriptRoot
try {
    $flags = @('-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
               '-fno-builtin', '-fno-stack-protector', '-nostdlib', '-Wl,-e,mainCRTStartup')
    & gcc @flags -o parent.exe parent.c io.c -lkernel32
    if ($LASTEXITCODE -ne 0) { throw "Не удалось собрать parent.exe (код $LASTEXITCODE)" }
    & gcc @flags -o child.exe child.c numbers.c io.c -lkernel32 -lshell32
    if ($LASTEXITCODE -ne 0) { throw "Не удалось собрать child.exe (код $LASTEXITCODE)" }
} finally {
    Pop-Location
}
