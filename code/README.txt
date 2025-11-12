1) To compile the demo, use the command:
gcc -o filter filter.c rt-lib.c store.c -lm -lrt -lpthread -Wall

or, only:

gcc -o filter filter.c rt-lib.c -lm -lrt -lpthread -Wall

2) Run the filter app:
./filter

To test the demo: 

1) In a different shell launch the python script: 
python3 live_plot.py



USEFUL GITHUB COMMANDS:

git pull - Get the latest code

git checkout <branchname> - Switch to another branch

git checkout -b <branchname> - create and switch to branch the new branch

git add <filename> - stage the changes of a modified file named filename

git add .  - stage the changes of all the files modified

git commit -m  “message here” - commits local changes

git push origin (branchname) - Push local branch so its visible for everybody, this does not merge it to the mainbranch, but it makes it possible to create a pull request from Github and then merge it.
