To compile the filter file, open a terminal and use the command:

gcc -o filter filter.c rt-lib.c -lm -lrt -lpthread -Wall


Compile the store file:

gcc -o store store.c rt-lib.c -lm -lrt -lpthread -Wall


Run the filter and the store app simultaneously with:

bash run.sh


You can choose a specific filter with:

bash run.sh 1 
bash run.sh 2
bash run.sh 3
bash run.sh 4

They all represent different filtering applications. They respectively are: 
- mean moving average 
- butterworth filter 2^nd order
- butterworth filter 4^th order
- Kalman filter

You can also add the option: -s -n or -f to run the original signal, the one with noise or the filtered one.
For istance:

bash run .sh 4 -f 

lets you visualize the filtered signal, using the Kalman filter
 

Finally open a different shell and launch the python script: 

python3 live_plot.py


In the code provided by the instructor, after compiling filter run it with:

./filter -s 
./filter -n
./filter -f

to plot only one of the signals 


USEFUL GITHUB COMMANDS:

git clone https://github.com/PSSRT/hm1.git - Clone the repositiory

git clone --branch <branch_name> --single-branch https://github.com/PSSRT/hm1.git - Clone a branch in the repo

git pull - Get the latest code

git checkout <branchname> - Switch to another branch

git checkout -b <branchname> - create and switch to branch the new branch

git add <filename> - stage the changes of a modified file named filename

git add .  - stage the changes of all the files modified

git commit -m  “message here” - commits local changes

git push origin (branchname) - Push local branch so its visible for everybody, this does not merge it to the mainbranch, but it makes it possible to create a pull request from Github and then merge it.
