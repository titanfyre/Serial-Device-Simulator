param([switch]$AddressSanitizer,[string]$SdkDirectory,[switch]$Screenshot)
$ErrorActionPreference='Stop'
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Visual Studio 2022 C++ desktop tools are required.'}
$build=Join-Path $PSScriptRoot $(if($AddressSanitizer){'obj/Asan'}elseif($SdkDirectory){'obj/Production'}else{'obj/Build'})
$args=@('-S',$PSScriptRoot,'-B',$build,'-G','Visual Studio 17 2022','-A','x64')
if($AddressSanitizer){
 $args+='-DENABLE_ASAN=ON'
 $toolset=Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory|Where-Object {[version]$_.Name -ge [version]'14.38.0' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll'))}|Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1
 if(!$toolset){throw 'Install the current Visual Studio C++ AddressSanitizer component.'}
 $savedPath=$env:PATH
 $env:PATH=(Join-Path $toolset.FullName 'bin\Hostx64\x64')+';'+$env:PATH
}else{$args+='-DENABLE_ASAN=OFF'}
if($SdkDirectory){$args+=('-DSDK_DIRECTORY='+$SdkDirectory)}
try{
& cmake @args
if($LASTEXITCODE){throw 'CMake configuration failed.'}
$buildArgs=@();if($AddressSanitizer){$buildArgs=@('--',('/p:VCToolsVersion='+$toolset.Name),'/p:PreferredToolArchitecture=x64')}
& cmake --build $build --config Release --parallel @buildArgs
if($LASTEXITCODE){throw 'Compilation failed.'}
if($AddressSanitizer){
 Copy-Item -LiteralPath (Join-Path $toolset.FullName 'bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll') -Destination (Join-Path $build 'bin') -Force
 Write-Host ('ASAN compiler and runtime: MSVC '+$toolset.Name)
}
& ctest --test-dir $build -C Release --output-on-failure --timeout 30
if($LASTEXITCODE){throw 'Offline tests failed.'}
if($Screenshot){$process=Start-Process -FilePath (Join-Path $build 'bin/SerialDeviceSimulator.exe') -ArgumentList @('--screenshot',(Join-Path $PSScriptRoot 'docs/app.png')) -WindowStyle Hidden -Wait -PassThru;if($process.ExitCode){throw 'Offline preview rendering failed.'}}
}finally{if($AddressSanitizer){$env:PATH=$savedPath}}
