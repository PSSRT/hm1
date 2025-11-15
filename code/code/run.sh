#!/bin/bash

# --- Parsing dei parametri ---
FILTER_CHOICE=$1      # primo argomento = numero filtro
shift                 # sposta gli argomenti, ora rimangono solo i flag

# Se non ci sono flag, abilito tutti (-s -n -f)
if [ $# -eq 0 ]; then
    STORE_FLAGS="-s -n -f"
else
    STORE_FLAGS="$@"   # usa tutti i flag passati
fi

# --- Avvio dei processi ---
./store $STORE_FLAGS &
STORE_PID=$!

./filter $FILTER_CHOICE &
FILTER_PID=$!

# --- Funzione per terminare ---
kill_processes() {
    kill $STORE_PID 2>/dev/null
    kill $FILTER_PID 2>/dev/null
}

trap kill_processes SIGINT

# Attesa input utente
read -p "Press 'q' to exit: " input

if [ "$input" == "q" ]; then
    kill_processes
    echo "" > signal.txt
fi

