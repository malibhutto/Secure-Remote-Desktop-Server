#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080

int init_client_socket();
void send_message_to_server(int clientfd, const char* message);
void handle_upload(int clientfd, const char* filename);
void handle_download(int clientfd, const char* filename);
void handle_screenshot(int clientfd);
void receive_file_from_server(int clientfd, const char* filename);
void receive_message_from_server(int clientfd);
void close_client_connection(int clientfd);

int main() {
    int clientfd = init_client_socket();
    char buffer[1024] = {0};

    read(clientfd, buffer, sizeof(buffer));
    printf("Server: %s\n", buffer);

    memset(buffer, 0, sizeof(buffer));
    printf("Enter username: ");
    fgets(buffer, sizeof(buffer), stdin);
    buffer[strcspn(buffer, "\n")] = '\0';
    send(clientfd, buffer, strlen(buffer), 0);

    memset(buffer, 0, sizeof(buffer));
    read(clientfd, buffer, sizeof(buffer));
    printf("Server: %s\n", buffer);

    memset(buffer, 0, sizeof(buffer));
    printf("Enter password: ");
    fgets(buffer, sizeof(buffer), stdin);
    buffer[strcspn(buffer, "\n")] = '\0';
    send(clientfd, buffer, strlen(buffer), 0);

    memset(buffer, 0, sizeof(buffer));
    read(clientfd, buffer, sizeof(buffer));
    printf("Server: %s\n", buffer);

    if (strncmp(buffer, "Authentication failed", 21) == 0) {
        printf("Disconnected: Authentication failed.\n");
        close(clientfd);
        exit(1);
    }

    printf("Authentication successful. You can now send commands.\n");

    const char* prompt = "Available Commands:\n- send message (default)\n- download <filename>\n- upload <filename>\n- screenshot\n- exit\n";
    printf("%s", prompt);

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        printf("Enter message: ");
        fgets(buffer, sizeof(buffer), stdin);
        buffer[strcspn(buffer, "\n")] = '\0';

        if (strncmp(buffer, "upload ", 7) == 0) {
            char* filename = buffer + 7;
            handle_upload(clientfd, filename);
            continue;
        }

        if (strncmp(buffer, "download ", 9) == 0) {
            char* filename = buffer + 9;
            handle_download(clientfd, filename);
            continue;
        }

        if (strcmp(buffer, "screenshot") == 0) {
            handle_screenshot(clientfd);
            continue;
        } else {
            send_message_to_server(clientfd, buffer);
        }

        if (strcmp(buffer, "exit") == 0) {
            printf("Exiting client...\n");
            break;
        }

        receive_message_from_server(clientfd);
    }

    close_client_connection(clientfd);
    return 0;
}

int init_client_socket() {
    int clientfd;
    struct sockaddr_in server_addr;

    clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd < 0) {
        perror("Error while creating socket");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);

    if (connect(clientfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }

    return clientfd;
}

void send_message_to_server(int clientfd, const char* message) {
    send(clientfd, message, strlen(message), 0);
}

void handle_upload(int clientfd, const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) {
        perror("File open failed");
        return;
    }

    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "upload %s", filename);
    send_message_to_server(clientfd, buffer);
    sleep(1);

    char filebuf[1024];
    int bytes;
    while ((bytes = fread(filebuf, 1, sizeof(filebuf), fp)) > 0) {
        send(clientfd, filebuf, bytes, 0);
    }

    send(clientfd, "__END__", 7, 0);
    fclose(fp);
    printf("File sent.\n");
}

void handle_download(int clientfd, const char* filename) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "download %s", filename);
    send_message_to_server(clientfd, buffer);
    receive_file_from_server(clientfd, filename);
}

void handle_screenshot(int clientfd) {
    send_message_to_server(clientfd, "screenshot");
    receive_file_from_server(clientfd, "screenshot.png");
}

void receive_file_from_server(int clientfd, const char* filename) {
    FILE* fp = fopen(filename, "wb");
    if (!fp) {
        perror("Error opening file for writing");
        return;
    }

    char filebuf[1024];
    int bytes;
    int end_marker_len = strlen("__END__");

    while ((bytes = read(clientfd, filebuf, sizeof(filebuf))) > 0) {
        if (bytes >= end_marker_len && strstr(filebuf, "__END__") != NULL) {
            size_t write_len = bytes - end_marker_len;
            fwrite(filebuf, 1, write_len, fp);
            break;
        }
        fwrite(filebuf, 1, bytes, fp);
    }

    fclose(fp);
    printf("File downloaded successfully.\n");
}

void receive_message_from_server(int clientfd) {
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    read(clientfd, buffer, sizeof(buffer));
    printf("Server: %s\n", buffer);
}

void close_client_connection(int clientfd) {
    close(clientfd);
}


