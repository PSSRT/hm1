To compile the demo:
gcc -o filter filter.c rt-lib.c -lm -lrt -lpthread -Wall

To test the demo: 

1) In a different shell launch the python script: 
python3 live_plot.py

2) Run the filter app:
./filter
