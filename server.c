#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>

#define PORT 8080
#define MAX_CONCURRENT_CLIENTS 2

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
sem_t file_semaphore;

void* handleclient(void* arg);
int init_server_socket();
void accept_client_connection(int serverfd);
void handle_upload(int clientsock, char* filename);
void handle_download(int clientsock, char* filename);
void handle_screenshot(int clientsock);
void write_file_to_disk(int clientsock, FILE* fp);
void send_file_to_client(int clientsock, FILE* fp);
void send_reply_to_client(int clientsock, const char* message);
void shutdown_server(int serverfd);
void log_message(const char* message);
int authenticate(const char* username, const char* password);

int main() {
    sem_init(&file_semaphore, 0, MAX_CONCURRENT_CLIENTS);
    int serverfd = init_server_socket();
    while (1) {
        accept_client_connection(serverfd);
    }
    shutdown_server(serverfd);
    sem_destroy(&file_semaphore);
    return 0;
}

int authenticate(const char* username, const char* password) {
    FILE* fp = fopen("users.txt", "r");
    if (!fp) {
        perror("User file open error");
        return 0;
    }

    char line[256];
    char stored_user[128];
    char stored_pass[128];

    while (fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%s %s", stored_user, stored_pass) == 2) {
            if (strcmp(username, stored_user) == 0 && strcmp(password, stored_pass) == 0) {
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
    return 0;
}

int init_server_socket() {
    int serverfd;
    struct sockaddr_in server_addr;

    serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(serverfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    if (listen(serverfd, 5) < 0) {
        perror("Listen failed");
        exit(1);
    }

    printf("Server started. To shut down the server, press Ctrl + C.\n");
    log_message("Server listening on port...");
    fflush(stdout);

    return serverfd;
}

void accept_client_connection(int serverfd) {
    struct sockaddr_in client_addr;
    socklen_t addr_size;
    addr_size = sizeof(client_addr);
    int* newsockfd = malloc(sizeof(int));
    *newsockfd = accept(serverfd, (struct sockaddr*)&client_addr, &addr_size);

    if (*newsockfd < 0) {
        perror("Accept failed");
        free(newsockfd);
        return;
    }

    log_message("Client connected. Creating thread...");

    pthread_t tid;
    if (pthread_create(&tid, NULL, handleclient, newsockfd) != 0) {
        perror("Thread creation failed");
        close(*newsockfd);
        free(newsockfd);
    } else {
        pthread_detach(tid);
    }
}

void* handleclient(void* arg) {
    int clientsock = *((int*)arg);
    free(arg);
    char buffer[1024] = {0};
    char username[128] = {0};
    char password[128] = {0};

    send(clientsock, "Enter username:", strlen("Enter username:"), 0);
    int bytes = read(clientsock, username, sizeof(username) - 1);
    if (bytes <= 0) { close(clientsock); pthread_exit(NULL); }
    username[bytes] = '\0';

    send(clientsock, "Enter password:", strlen("Enter password:"), 0);
    bytes = read(clientsock, password, sizeof(password) - 1);
    if (bytes <= 0) { close(clientsock); pthread_exit(NULL); }
    password[bytes] = '\0';

    if (!authenticate(username, password)) {
        send(clientsock, "Authentication failed. Goodbye.", strlen("Authentication failed. Goodbye."), 0);
        close(clientsock);
        pthread_exit(NULL);
    }

    send(clientsock, "Authentication successful. Welcome!", strlen("Authentication successful. Welcome!"), 0);
    printf("Client '%s' authenticated successfully.\n", username);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int bytes = read(clientsock, buffer, sizeof(buffer) - 1);
        if (bytes <= 0) {
            printf("Client disconnected (read error or closed).\n");
            break;
        }

        buffer[bytes] = '\0';
        printf("Client: %s\n", buffer);

        if (strcmp(buffer, "exit") == 0) {
            printf("Client sent exit. Closing connection.\n");
            break;
        }

        if (strncasecmp(buffer, "upload ", 7) == 0) {
            char* filename = buffer + 7;
            handle_upload(clientsock, filename);
            continue;
        }

        if (strncasecmp(buffer, "download ", 9) == 0) {
            char* filename = buffer + 9;
            handle_download(clientsock, filename);
            continue;
        }

        if (strcmp(buffer, "screenshot") == 0) {
            handle_screenshot(clientsock);
            continue;
        }

        send_reply_to_client(clientsock, "Message received");
    }

    close(clientsock);
    pthread_exit(NULL);
}

void handle_upload(int clientsock, char* filename) {
    sem_wait(&file_semaphore);
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error opening file");
        send_reply_to_client(clientsock, "Error opening file");
        sem_post(&file_semaphore);
        return;
    }
    write_file_to_disk(clientsock, fp);
    fclose(fp);
    printf("File received and saved.\n");
    send_reply_to_client(clientsock, "File transfer successful");
    sem_post(&file_semaphore);
}

void handle_download(int clientsock, char* filename) {
    sem_wait(&file_semaphore);
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        perror("Error opening file");
        send_reply_to_client(clientsock, "File not found");
        sem_post(&file_semaphore);
        return;
    }

    send_file_to_client(clientsock, fp);
    fclose(fp);
    printf("File sent to client.\n");
    sem_post(&file_semaphore);
}

void handle_screenshot(int clientsock) {
    system("gnome-screenshot -f /tmp/screenshot.png");

    FILE* fp = fopen("/tmp/screenshot.png", "rb");
    if (!fp) {
        perror("Error opening screenshot file");
        send_reply_to_client(clientsock, "Error capturing screenshot");
        return;
    }

    send_file_to_client(clientsock, fp);
    fclose(fp);
    printf("Screenshot sent to client.\n");
}

void write_file_to_disk(int clientsock, FILE* fp) {
    char filebuf[1024];
    int bytes;
    while (1) {
        bytes = read(clientsock, filebuf, sizeof(filebuf) - 1);
        if (bytes <= 0) break;

        filebuf[bytes] = '\0';

        if (strstr(filebuf, "__END__") != NULL) {
            char* end_marker = strstr(filebuf, "__END__");
            size_t write_len = end_marker - filebuf;
            fwrite(filebuf, 1, write_len, fp);
            break;
        }

        fwrite(filebuf, 1, bytes, fp);
    }
}

void send_file_to_client(int clientsock, FILE* fp) {
    char filebuf[1024];
    int bytes;
    while ((bytes = fread(filebuf, 1, sizeof(filebuf), fp)) > 0) {
        send(clientsock, filebuf, bytes, 0);
    }

    send(clientsock, "__END__", 7, 0);
}

void send_reply_to_client(int clientsock, const char* message) {
    send(clientsock, message, strlen(message), 0);
}

void log_message(const char* message) {
    pthread_mutex_lock(&log_mutex);
    printf("%s\n", message);
    pthread_mutex_unlock(&log_mutex);
}

void shutdown_server(int serverfd) {
    close(serverfd);
    log_message("Server shutdown complete.");
}
