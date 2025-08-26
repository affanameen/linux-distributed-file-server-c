// Client program for distributed file system

#include <stdio.h>              // Standard I/O functions
#include <stdlib.h>             // Standard library functions
#include <string.h>             // String handling functions
#include <unistd.h>             // POSIX functions
#include <sys/types.h>          // System data types
#include <sys/socket.h>         // Socket functions
#include <arpa/inet.h>          // Internet operations functions
#include <netinet/in.h>         // Internet address structures
#include <errno.h>              // Error handling functions

#define SERVER_IP "127.0.0.1"   // S1 server IP address
#define SERVER_PORT 4641        // S1 server port
#define BUFFER_SIZE 1024        // Buffer size for network operations

// Function prototypes for client operations
void print_menu();  // Display client command menu

/* helper: get base filename from a path */
static const char* base_of_path(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

// Helper function to receive a file from the server
int receive_file_client(int sock, const char *filename) {
    // Receive file size or error message
    char header[BUFFER_SIZE] = {0};
    int bytes_received = recv(sock, header, sizeof(header) - 1, 0);
    if (bytes_received <= 0) {
        printf("Error receiving file header.\n");
        return -1;
    }

    // Check if server responded with error
    if (strncmp(header, "ERR", 3) == 0) {
        printf("ERROR: File not found.\n");
        return -1;
    }

    // Parse file size
    long filesize = atol(header);
    if (filesize <= 0) {
        printf("ERROR: Invalid file size received.\n");
        return -1;
    }

    // Create file and receive content
    FILE *file = fopen(filename, "wb");
    if (!file) {
        perror("Error opening file");
        return -1;
    }

    long total_received = 0;
    char buffer[BUFFER_SIZE];
    while (total_received < filesize) {
        int chunk = recv(sock, buffer, sizeof(buffer), 0);
        if (chunk <= 0) break;
        fwrite(buffer, 1, chunk, file);
        total_received += chunk;
    }

    fclose(file);

    if (total_received == filesize) {
        return 0; // Success
    } else {
        printf("ERROR: Incomplete file received.\n");
        return -1; // Failure
    }
}

// Main function entry point for the client
int main() {
    int sock;
    struct sockaddr_in server_addr;
    char input[BUFFER_SIZE];
    char command[BUFFER_SIZE];
    char filename[BUFFER_SIZE];
    char dest_path[BUFFER_SIZE];
    char filepath[BUFFER_SIZE];
    char filetype[BUFFER_SIZE];
    char directory[BUFFER_SIZE];

    printf("Connecting to S1 at %s:%d...\n", SERVER_IP, SERVER_PORT);

    // Create a socket for communication with S1
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Error creating socket");
        return EXIT_FAILURE;
    }

    // Configure server address struct for S1 connection
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    // Convert and set server IP address
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid server address");
        close(sock);
        return EXIT_FAILURE;
    }

    // Connect to the server (S1)
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to S1 failed");
        close(sock);
        return EXIT_FAILURE;
    }

    printf("Connected to S1.\n");

    print_menu();  // Display available commands to the user

    while (1) {
        printf("Enter command —> ");

        // Read user input command
        if (!fgets(input, sizeof(input), stdin)) {
            printf("Error reading input.\n");
            continue;            // Continue to next command
        }

        // Parse the command name from input
        if (sscanf(input, "%s", command) != 1) {
            printf("Invalid command format. Please try again.\n");
            continue;            // Continue to next command
        }

        /* For multi-arg commands we will NOT send the whole input line.
           We'll print the same "Command sent..." line for consistency,
           then send per-file subcommands below. */
        int is_multi = (strcmp(command, "uploadf")==0) || (strcmp(command, "downlf")==0) || (strcmp(command, "removef")==0);
        if (!is_multi) {
            if (send(sock, input, strlen(input), 0) < 0) {
                perror("Error sending command");
                close(sock);
                continue;
            }
        }
        printf("Command sent to S1: %s\n", input);

        // Handle uploadf — up to 3 files, last token is destination
        if (strcmp(command, "uploadf") == 0) {
            /* Expected: uploadf f1 [f2] [f3] S1/path */
            char tmp[BUFFER_SIZE];
            strncpy(tmp, input, sizeof(tmp));
            tmp[sizeof(tmp)-1] = 0;

            char *tok = strtok(tmp, " \t\r\n");
            char *args[16]; int n=0;
            while ((tok = strtok(NULL, " \t\r\n")) && n < 16) args[n++] = tok;

            if (n < 2) {
                printf("ERROR: Invalid uploadf command format.\n");
                close(sock);
                continue;
            }
            if (n > 4) {
                /* ignore extras beyond 3 files */
                n = 4;
            }
            const char *dest = args[n-1];
            int files_cnt = n - 1; /* up to 3 */

            for (int i = 0; i < files_cnt; i++) {
                const char *onefile = args[i];

                // Send per-file command to S1
                char percmd[BUFFER_SIZE];
                snprintf(percmd, sizeof(percmd), "uploadf %s %s", onefile, dest);
                if (send(sock, percmd, strlen(percmd), 0) < 0) {
                    perror("Error sending command");
                    break;
                }

                // Receive server response (READY or error)
                char response[BUFFER_SIZE] = {0};
                int bytes = recv(sock, response, sizeof(response) - 1, 0);
                if (bytes <= 0) {
                    printf("No response from S1.\n");
                    break;
                }
                printf("Received from S1: %s\n", response);

                // If server is ready, send the file
                if (strncmp(response, "READY", 5) == 0) {
                    FILE *fp = fopen(onefile, "rb");
                    if (!fp) { perror("File open failed"); break; }

                    fseek(fp, 0, SEEK_END);
                    long file_size = ftell(fp);
                    fseek(fp, 0, SEEK_SET);

                    // Send file size as a string (unchanged protocol)
                    char size_str[64];
                    snprintf(size_str, sizeof(size_str), "%ld", file_size);
                    if (send(sock, size_str, strlen(size_str), 0) < 0) {
                        perror("Error sending file size");
                        fclose(fp);
                        break;
                    }

                    // Send file data
                    char file_buffer[BUFFER_SIZE];
                    int read_bytes;
                    long total_sent = 0;

                    while ((read_bytes = (int)fread(file_buffer, 1, sizeof(file_buffer), fp)) > 0) {
                        int sent = (int)send(sock, file_buffer, (size_t)read_bytes, 0);
                        if (sent < 0) {
                            perror("Error sending file data");
                            break;
                        }
                        total_sent += sent;
                    }
                    fclose(fp);

                    printf("File upload completed.\n");
                } else {
                    printf("Upload aborted. Try Again!\n");
                    continue;
                }

                // Read final server confirmation message
                char finalmsg[BUFFER_SIZE] = {0};
                int fb = recv(sock, finalmsg, sizeof(finalmsg)-1, 0);
                if (fb > 0) printf("%s\n", finalmsg);
                else printf("No final response from S1.\n");
            }
        }

        // Handle downloading files (downlf) — up to 2 files
        else if (strncmp(command, "downlf", 6) == 0) {
            /* Expected: downlf path1 [path2] */
            char tmp[BUFFER_SIZE];
            strncpy(tmp, input, sizeof(tmp));
            tmp[sizeof(tmp)-1] = 0;

            char *tok = strtok(tmp, " \t\r\n");
            char *paths[4]; int np = 0;
            while ((tok = strtok(NULL, " \t\r\n")) && np < 4) paths[np++] = tok;

            if (np < 1) {
                printf("ERROR: Invalid downlf command format. Expected: downlf <filepath>\n");
                close(sock);
                continue;
            }
            if (np > 2) np = 2;  // accept at most 2

            for (int i = 0; i < np; i++) {
                const char *filepath_arg = paths[i];

                char *ext = strrchr(filepath_arg, '.');
                if (!ext) { printf("ERROR: File has no extension.\n"); continue; }

                if (strncmp(filepath_arg, "S1/", 3) != 0) { printf("ERROR: Path must start with 'S1/'.\n"); continue; }

                if (strcmp(ext, ".c") != 0 && strcmp(ext, ".pdf") != 0 &&
                    strcmp(ext, ".txt") != 0 && strcmp(ext, ".zip") != 0) {
                    printf("ERROR: Unsupported file extension for download.\n");
                    continue;
                }

                // Send per-file downlf request
                char percmd[BUFFER_SIZE];
                snprintf(percmd, sizeof(percmd), "downlf %s", filepath_arg);
                if (send(sock, percmd, strlen(percmd), 0) < 0) {
                    perror("send downlf");
                    break;
                }

                const char *base = base_of_path(filepath_arg);
                printf("Receiving file and saving as %s...\n", base);

                if (receive_file_client(sock, base) == 0)
                    printf("File downloaded successfully as %s\n", base);
                else
                    printf("ERROR: File not found.\n");
            }
            close(sock);
            continue;
        }

        // Handle deleting files (removef) — up to 2 files
        else if (strcmp(command, "removef") == 0) {
            /* Expected: removef path1 [path2] */
            char tmp[BUFFER_SIZE];
            strncpy(tmp, input, sizeof(tmp));
            tmp[sizeof(tmp)-1] = 0;

            char *tok = strtok(tmp, " \t\r\n");
            char *paths[4]; int np=0;
            while ((tok = strtok(NULL, " \t\r\n")) && np < 4) paths[np++] = tok;

            if (np < 1) {
                printf("ERROR: Invalid removef command format. Expected: removef <filepath>\n");
                close(sock);
                continue;
            }
            if (np > 2) np = 2; // at most 2

            for (int i=0;i<np;i++) {
                char percmd[BUFFER_SIZE];
                snprintf(percmd, sizeof(percmd), "removef %s", paths[i]);
                if (send(sock, percmd, strlen(percmd), 0) < 0) {
                    perror("send removef");
                    break;
                }
                char response[4096] = {0};
                int bytes = recv(sock, response, sizeof(response)-1, 0);
                if (bytes > 0) printf("%s\n", response); else printf("No response received from S1.\n");
            }
        }

        // Handle listing file names in a directory (dispfnames)
        else if (strcmp(command, "dispfnames") == 0) {
            char response[4096] = {0};
            int bytes = recv(sock, response, sizeof(response)-1, 0);
            if (bytes > 0) { printf("%s\n", response); } else { printf("No response received from S1.\n"); }
        }

        // Handle downltar and other commands exactly as before
        else if (strcmp(command, "downltar") == 0) { // If command is "downltar"
            char response[4096] = {0};
            int bytes = recv(sock, response, sizeof(response)-1, 0);
            if (bytes > 0) { printf("%s\n", response); } else { printf("No response received from S1.\n"); }
        }

        // Handle exit command
        else if (strcmp(command, "exit") == 0) {
            printf("Exiting client.\n");
            break; // Exit the loop and close client
        }

        // Handle unknown commands
        else {
            printf("Unknown command. Please try again.\n");
            print_menu();
        }
    }

    // Close the socket after finishing communication
    close(sock);
    return 0; // Return success code
}

// Function to display the client menu options
void print_menu() {
    printf("Select an option:\n");
    printf("i. To upload files use uploadf <filename> <destination_path>\n");
    printf("ii. To download files use downlf <filepath>\n");
    printf("iii. To remove the files use removef <filepath>\n");
    printf("iv. To download tar use downltar <filetype>\n");
    printf("v. to list files use dispfnames <directory>\n");
    printf("Type 'exit' to quit the client.\n");
    printf("*********************************************\n");
}
