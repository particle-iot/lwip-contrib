@echo off
rem ATTENTION: this deletes the output folder first!
set devenv="c:\Program Files (x86)\Microsoft Visual Studio 10.0\Common7\IDE\devenv"
set covbuild=%1
rem set covbuild=e:\Downloads\cov-analysis-win64-2017.07\bin\cov-build.exe
set covoutput=cov-int
set zip7="c:\Program Files\7-Zip\7z.exe"

pushd %~dp0

rd /s /1 %covoutput%
del %covoutput%.zip

%covbuild% --dir %covoutput% %devenv% lwip_test.sln /build Debug 

%zip7% a %covoutput%.zip %covoutput%
popd