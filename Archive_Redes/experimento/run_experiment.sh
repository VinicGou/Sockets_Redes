#!/bin/bash

IP="127.0.0.1" # Ou o IP da maquina servidora na rede local
PORT=8080
DURATION=10    # 10 segundos por teste
BLOCK_SIZE=65536
REPS=5         # 5 repeticoes para cada numero de clientes

echo "clientes,repeticao,bytes,tempo_s,throughput_mbps" > resultados.csv

for N in {1..10}; do
    for R in $(seq 1 $REPS); do
        echo "Executando: $N cliente(s) | Repeticao $R/$REPS..."
        
        # Inicia o servidor em background e captura a linha CSV_OUT
        ./bench_server $PORT $N $BLOCK_SIZE > server_tmp.log &
        SERVER_PID=$!
        sleep 0.5 # Aguarda o bind da porta

        # Dispara N clientes simultaneamente em background
        CLIENT_PIDS=()
        for ((i=1; i<=N; i++)); do
            ./bench_client $IP $PORT $DURATION $BLOCK_SIZE &
            CLIENT_PIDS+=($!)
        done

        # Aguarda todos os clientes terminarem
        for pid in "${CLIENT_PIDS[@]}"; do
            wait $pid
        done

        # Aguarda o servidor processar o fechamento e encerramos o processo
        sleep 0.5
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null

        # Extrai a metrica e salva no CSV
        LINE=$(grep "CSV_OUT" server_tmp.log | cut -d',' -f3-)
        echo "$N,$R,$LINE" >> resultados.csv
    done
done

rm -f server_tmp.log
echo "Experimentos concluidos! Dados salvos em resultados.csv."