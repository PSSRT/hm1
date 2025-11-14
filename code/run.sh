#!/bin/bash

# Launch the first C program
./store &
# Store its PID
STORE_PID=$!

# Launch the second C program
./filter $1 &
# Store its PID
FILTER_PID=$!

# Function to kill all processes
kill_processes() {
    kill $STORE_PID
    kill $FILTER_PID
}

# Trap SIGINT (Ctrl+C) to call the kill_processes function
trap kill_processes SIGINT

# Wait for user input
read -p "Press 'q' to exit: " input

# If user input is 'q', call the kill_processes function
if [ "$input" == "q" ]; then
    kill_processes
    echo "" > signal.txt 
fi
