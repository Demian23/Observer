set fileway=%~dp0ARM64\Debug\
echo %fileway%
PAUSE
C:\Windows\System32\sc.exe create observer type= filesys binPath= %fileway%Observer.sys
PAUSE
%~dp0\Installer.exe %~dp0\temp.txt %~dp0\RegistryConfiguration.txt %~dp0
PAUSE
C:\Windows\System32\fltmc.exe load observer
PAUSE
%fileway%\ObserverClient.exe
PAUSE