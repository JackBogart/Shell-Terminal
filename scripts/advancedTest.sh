echo ====================
echo CORRECT TOKENIZATION
echo ====================
cd redirect_tests
echo foo>f1
cat<f1>f2
echo ====================
echo REDIRECT/PIPE OF PWD
echo ====================
pwd > file
cd ..
echo ====================
echo NO CRASH TEST
echo ====================
cd redirect_tests/ > some_file