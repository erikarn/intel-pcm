
REM change path to your VCVARS.BAT
CALL "c:\Program Files (x86)\Microsoft Visual Studio 11.0\VC\bin\vcvars32.bat"

for %%p in (PCM) do (
   @echo Building %%p
   chdir %%p_Win
   vcupgrade -overwrite %%p.vcproj
   msbuild  %%p.vcxproj
   chdir ..
)

   @echo Building Intelpcm.dll
   chdir Intelpcm.dll
   vcupgrade -overwrite Intelpcm.dll.vcproj
   msbuild  Intelpcm.dll.vcxproj
   chdir ..

   @echo Building PCM-Service 
   chdir PCM-Service_Win
   vcupgrade -overwrite PCMService.vcproj
   msbuild  PCMService.vcxproj
   chdir ..


for %%p in (PCM-MSR PCM-TSX PCM-Memory PCM-NUMA PCM-PCIE PCM-Power) do (
   @echo Building %%p
   chdir %%p_Win
   vcupgrade -overwrite %%p-win.vcproj
   msbuild  %%p-win.vcxproj
   chdir ..
)



