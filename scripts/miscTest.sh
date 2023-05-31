echo =======================
echo ESCAPE SEQUENCE TEST
echo =======================
echo foo\>bar
echo foo\|cat
touch foo\ bar
echo foo\
bar
cd wildcard_tests
echo a\*z
cd ..
echo =======================
echo DIRECTORY WILDCARD TEST
echo =======================
ls wild*/a*z
echo =======================
echo HOME DIRECTORY TEST
echo =======================
ls ~/
cd
pwd