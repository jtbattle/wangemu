ECHO OFF
REM build the wang2200.org website via the Template Toolkit templating system.
REM run "build_html --all" to force regeneration of everything

echo -------- creating pc help files --------

call ttree --binmode=1 --eval_perl --define os=pc  --dest=../html     --file=./ttree_html.cfg
echo Copying image files
xcopy /D /I /Q /R /K /Y img-pc ..\html\img

echo -------- creating os x help files --------

call ttree --binmode=1 --eval_perl --define os=mac --dest=../html_osx --file=./ttree_html.cfg
echo Copying image files
xcopy /D /I /Q /R /K /Y img-mac ..\html_osx\img
