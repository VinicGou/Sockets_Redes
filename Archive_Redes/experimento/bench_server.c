#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_BUFFER_SIZE 65536

typedef struct {
    int fd;
    size_t buffer_size;
    unsigned long long bytes_received;
    double duration_sec;
} ClientStat;

pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned long long total_bytes_all_clients = 0;
int active_clients = 0;
int completed_clients = 0;
int expected_clients = 0;
struct timespec first_byte_time, last_byte_time;
int measurement_started = 0;

void *handle_client(void *arg) {
    ClientStat *stat = (ClientStat *)arg;
    char *buffer = malloc(stat->buffer_size);
    if (!buffer) {
        perror("malloc buffer");
        close(stat->fd);
        free(stat);
        return NULL;
    }

    struct timespec start, end;
    int first_read = 1;

    while (1) {
        ssize_t n = recv(stat->fd, buffer, stat->buffer_size, 0);

        if (n > 0) {
            if (first_read) {
                clock_gettime(CLOCK_MONOTONIC, &start);
                pthread_mutex_lock(&stats_mutex);
                if (!measurement_started) {
                    first_byte_time = start;
                    measurement_started = 1;
                }
                pthread_mutex_unlock(&stats_mutex);
                first_read = 0;
            }
            stat->bytes_received += (unsigned long long)n;
        } else if (n == 0) {
            break; // Cliente encerrou a conexao
        } else {
            if (errno == EINTR) continue;
            perror("recv");
            break;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    stat->duration_sec = (end.tv_sec - start.tv_sec) + 
                         (end.tv_nsec - start.tv_nsec) / 1e9;

    pthread_mutex_lock(&stats_mutex);
    last_byte_time = end;
    total_bytes_all_clients += stat->bytes_received;
    completed_clients++;

    if (completed_clients == expected_clients) {
        double total_time = (last_byte_time.tv_sec - first_byte_time.tv_sec) +
                            (last_byte_time.tv_nsec - first_byte_time.tv_nsec) / 1e9;
        
        // Taxa em Mbps (Megabits por segundo = 10^6 bits/s)
        double throughput_mbps = (total_bytes_all_clients * 8.0) / (total_time * 1e6);

        printf("\n=== Resultado da Bateria (%d clientes) ===\n", expected_clients);
        printf("Total de Bytes Recebidos : %llu bytes\n", total_bytes_all_clients);
        printf("Tempo Total de Medicao   : %.4f s\n", total_time);
        printf("Throughput Agregado      : %.2f Mbps\n", throughput_mbps);
        printf("CSV_OUT,%d,%llu,%.4f,%.2f\n", 
               expected_clients, total_bytes_all_clients, total_time, throughput_mbps);
        fflush(stdout);

        // Reseta estado para o proximo experimento
        total_bytes_all_clients = 0;
        completed_clients = 0;
        measurement_started = 0;
    }
    pthread_mutex_unlock(&stats_mutex);

    close(stat->fd);
    free(buffer);
    free(stat);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Uso: %s <porta> <num_clientes_esperados> [tam_bloco]\n", argv[0]);
        return EXIT_FAILURE;
    }

    int port = atoi(argv[1]);
    expected_clients = atoi(argv[2]);
    size_t buf_size = (argc == 4) ? (size_t)atoi(argv[3]) : DEFAULT_BUFFER_SIZE;

    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        return EXIT_FAILURE;
    }

    if (listen(server_fd, 128) == -1) {
        perror("listen");
        close(server_fd);
        return EXIT_FAILURE;
    }

    printf("Servidor aguardando na porta %d para %d cliente(s) (Bloco: %zu bytes)...\n", 
           port, expected_clients, buf_size);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        ClientStat *stat = calloc(1, sizeof(ClientStat));
        stat->fd = client_fd;
        stat->buffer_size = buf_size;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, stat) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(stat);
            continue;
        }
        pthread_detach(tid);
    }

    close(server_fd);
    return EXIT_SUCCESS;
}