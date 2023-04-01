John Bogart - jgb128
Evan Leeds - ejl159

Extensions:
Home directory
Multiple Pipes
(DIRECTORY WILDCARDS MIGHT WORK BY ACCIDENT?)

Testing Plan/Writeup:
We had simple batch mode sh files to test batch mode: 
    batch_input1.txt
    batch_input2.txt
    batch_input3.txt
    batch_input4.txt

Other than that batch and interactive use the same code so we tested
in interactive.

1. We tested standard commands such as cd, pwd, ls, echo, cat, etc. They work, along with executables.

2. Wildcards. We made a file called "echo" and ran ech* Hello World!
    It printed out Hello World, showing that wildcards had some functionality.
    We then made subdirectories and tried to cd to them with a wildcard in
    the name and it worked.

3. Redirection. We approached redirection by reading each character 1 by 1.
    We ran commands such as echo Success>Test1 and then called cat Test1.
    When the terminal printed out "Success", we knew that redirection worked.
    We tested both ways and both worked consistently.

4. Pipes. We called pipes recursively by doing the part before the pipe, then using that as input for the next pipe like a big chain. This made it so instead
of only one pipe working, it worked for the extension as well giving us limitless pipes.
    We tested this with a variety of commands, my favorite was this:
    ls -la | grep .sh | sort -r | head -n 10
    We felt this was a very hard test to pass and it did so successfully.

5. Home directory
    When cd is called without another argument, we directed to HOME.
    When ~/ is used, it is referenced from the home directory.

I left a couple files for you to try out yourself. Have fun!
Reach out if there are any problems, we had none! - jgb128@scarletmail.rutgers.edu
