// S2 server handles file transfers for pdf files. 
 #include <stdio.h>               // Standard I/O functions            
 #include <stdlib.h>              // Standard library routines         
 #include <string.h>              // String handling                   
 #include <unistd.h>              // POSIX API (fork, close, etc.)     
 #include <sys/types.h>           // Data types used in system calls   
 #include <sys/socket.h>          // Socket functions                  
 #include <arpa/inet.h>           // Definitions for internet operation
 #include <netinet/in.h>          // Internet family of protocols      
 #include <errno.h>               // Error reporting                   
 #include <sys/stat.h>            // File status and directory function
 #include <dirent.h>              // Directory traversal functions     
 
 #define SERVER_PORT 4642         // Define server port for S2
 #define BUFFER_SIZE 1024         // Define buffer size for data transfers
 
 // Function prototypes
 void prcclient(int client_sock);   // process commands for a client connected to S2
 int create_directories(const char *path);  // create directory structure recursively
 int receive_file(int client_sock, const char *filepath);  // receive a file from the client
 int send_file(int client_sock, const char *filepath);  // send a file to the client
 void error_exit(const char *msg);  // print error message and exit

 // main - Sets up the server to listen on SERVER_PORT and processes each connection.
// Main function of S2 server
 int main() {
     int server_sock, client_sock;  // Variables for server and client socket descriptors
     struct sockaddr_in server_addr, client_addr;  // Structures to hold server and client addresses
     socklen_t client_addr_len = sizeof(client_addr);  // Length of client address structure
 
     if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)  // Create TCP socket; check for errors
         error_exit("S2: socket creation failed");  // Exit if socket creation fails
 
     memset(&server_addr, 0, sizeof(server_addr));  // Clear server address structure
     server_addr.sin_family = AF_INET;  // Set address family to IPv4
     server_addr.sin_addr.s_addr = INADDR_ANY;  // Bind to any available network interface
     server_addr.sin_port = htons(SERVER_PORT);  // Set port number, converting to network byte order
 
     if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)  // Bind socket to address and port
         error_exit("S2: bind failed");  // Exit if bind fails
 
     if (listen(server_sock, 10) < 0)  // Begin listening for incoming connections (max 10 pending)
         error_exit("S2: listen failed");  // Exit if listen fails
 
     printf("S2 Server (PDF handler) listening on port %d...\n", SERVER_PORT);  // Inform that server is ready
 
     while (1) {  // Server loop to accept clients forever
         client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len);  // Accept a new client connection
         if (client_sock < 0) {  // If accept fails
             perror("S2: accept failed");  // Print error message
             continue;  // continue to next loop iteration
         }
         pid_t pid = fork();  // Fork a child process to handle the client
         if (pid < 0) {  // Check for fork failure
             perror("S2: fork failed");  // Print error message
             close(client_sock);  // Close client socket if fork fails
             continue;  // Continue to next loop iteration
         }
         if (pid == 0) {  // Child process: handle client communication
             close(server_sock);  // Child doesn't need the listening socket
             prcclient(client_sock);  // Process client commands in child process
             exit(0);  // Terminate the child process when done
         } else {  // Parent process
             close(client_sock);  // Close the client socket in parent as child handles it
         }
     }
 
     close(server_sock);  // Close the server socket (this line is normally unreachable)
     return 0;  // End program successfully
 }
 
 //prcclient - Processes commands from a connected client.

 void prcclient(int client_sock) {  // Function to process client commands on S2
     char buffer[BUFFER_SIZE];  // Buffer for incoming data/commands
     char *home_dir = getenv("HOME");  // Get the HOME environment variable for directory base
     if (!home_dir)
         home_dir = ".";  // Default to current directory if HOME not set
 
     while (1) {  // Loop to continuously process commands from client
         memset(buffer, 0, sizeof(buffer));  // Clear the buffer for a new command
         int bytes_recv = recv(client_sock, buffer, sizeof(buffer) - 1, 0);  // Receive command from client
         if (bytes_recv <= 0)  // If no data received or connection closed, break out of loop
             break;
         buffer[strcspn(buffer, "\r\n")] = 0;  // Remove any newline characters from the received command
 
         char *command = strtok(buffer, " ");  // Tokenize the command string (first word is command)
         if (!command)  // If no command found, continue to next iteration
             continue;
 
         if (strcmp(command, "uploadf") == 0) {  // Check if the command is "uploadf"
             // Expected: uploadf <filename> <destination_path>
             char *filename = strtok(NULL, " ");  // Extract filename from the command
             char *destination = strtok(NULL, " ");  // Extract destination path from the command
             if (!filename || !destination) {  // Validate that both parameters are provided
                 send(client_sock, "ERROR: Invalid uploadf command format.\n", 41, 0);  // Send error message if format invalid
                 continue;  // Continue processing next command
             }
             // Check extension: only allow .pdf
             char *ext = strrchr(filename, '.');  // Find the file extension in the filename
             if (!ext || strcmp(ext, ".pdf") != 0) {  // If extension not found or not .pdf
                 send(client_sock, "ERROR: Only .pdf files allowed in S2.\n", 38, 0);  // Inform client that only PDF files are permitted
                 continue;  // Continue processing next command
             }
             // Create destination directory under $HOME/S2
             if (create_directories(destination) != 0) {  // Call function to create necessary directories
                 send(client_sock, "ERROR: Failed to create directory structure.\n", 48, 0);  // Send error if directory creation fails
                 continue;  // Continue processing next command
             }
             char local_filepath[512];  // Buffer to hold the full local file path
             // Construct file path: $HOME/destination/filename
             snprintf(local_filepath, sizeof(local_filepath), "%s/%s/%s", home_dir, destination, filename);  // Build the complete file path
             // Signal readiness.
             send(client_sock, "READY\n", 6, 0);  // Send "READY" signal to client to begin file transfer
             if (receive_file(client_sock, local_filepath) == 0)  // Receive file data and store it locally
                 send(client_sock, "File uploaded successfully to S2.\n", 34, 0);  // Inform client of successful upload
             else
                 send(client_sock, "ERROR: Failed to receive file in S2.\n", 38, 0);  // Report error if file reception fails
         }
         else if (strcmp(command, "downltar") == 0) {  // Check if command is "downltar"
             // Expected: downltar .pdf
             char *filetype = strtok(NULL, " ");  // Extract filetype (should be ".pdf")
             if (!filetype || strcmp(filetype, ".pdf") != 0) {  // Validate that filetype is provided and equals ".pdf"
                 send(client_sock, "ERROR: Invalid downltar command for S2. Expected: downltar .pdf\n", 64, 0);  // Send error if not valid
                 continue;  // Continue processing next command
             }
             // Create a tar archive of all .pdf files in $HOME/S2.
             char tar_path[256];  // Buffer for tar archive path
             snprintf(tar_path, sizeof(tar_path), "%s/pdf.tar", home_dir);  // Build path for the PDF tar archive
             char cmd[1024];  // Buffer for the system command to generate tar archive
             // The command tars all PDF files under $HOME/S2.
             snprintf(cmd, sizeof(cmd), "tar -cf %s --wildcards '*.pdf' -C \"%s/S2\" .", tar_path, home_dir);  // Construct tar command
             if (system(cmd) != 0) {  // Execute tar command; if nonzero return code, it's an error
                 send(client_sock, "ERROR: Failed to create tar file for .pdf files.\n", 50, 0);  // Inform client about tar creation error
                 continue;  // Continue processing next command
             }
             if (send_file(client_sock, tar_path) == 0)  // Send the generated tar file to the client
                 remove(tar_path);  // Remove the tar archive file from local storage after sending
             else
                 send(client_sock, "ERROR: Failed to send tar file.\n", 31, 0);  // Inform client if sending fails
         }
         else if (strcmp(command, "exit") == 0) {  // Check if command is "exit"
             break;  // Break out of the processing loop to terminate connection
         }
         else {  // For any unsupported command
             send(client_sock, "ERROR: Unknown command in S2.\n", 30, 0);  // Inform client that the command is not recognized
         }
     }
     close(client_sock);  // Close the client socket when finished processing commands
 }
 
 //create_directories - Recursively creates the directory structure under $HOME.
 int create_directories(const char *path) {  // Function to create directories recursively under user's HOME
     char full_path[512];  // Buffer to hold full path
     char tmp[512];        // Temporary buffer for manipulation of the path
     char *p;              // Pointer for iterating through the path string
     size_t len;           // Variable to hold the length of the temporary string
 
     char *home_dir = getenv("HOME");  // Get the home directory from environment variables
     if (!home_dir)
         home_dir = ".";  // Use current directory if HOME is not set
     snprintf(full_path, sizeof(full_path), "%s/%s", home_dir, path);  // Build the complete path by concatenating home dir and provided path
     strncpy(tmp, full_path, sizeof(tmp));  // Copy full path into temporary buffer for editing
     tmp[sizeof(tmp) - 1] = '\0';  // Ensure the temporary string is null-terminated
     len = strlen(tmp);  // Get the length of the temporary path
     if (tmp[len - 1] == '/')  // If there is a trailing slash
         tmp[len - 1] = '\0';  // Remove it by replacing with null character
     
     for (p = tmp + 1; *p; p++) {  // Iterate through the temporary path starting from second character
         if (*p == '/') {  // When a '/' is encountered (indicating a directory boundary)
             *p = '\0';  // Temporarily terminate the string to form the current directory path
             if (access(tmp, F_OK) != 0) {  // Check if the directory does not exist
                 if (mkdir(tmp, 0755) != 0) {  // Attempt to create the directory with permissions 0755
                     perror("mkdir");  // Print error if mkdir fails
                     return -1;  // Return error code
                 }
             }
             *p = '/';  // Restore the '/' character for further processing
         }
     }
     if (access(tmp, F_OK) != 0) {  // After processing, check if the complete directory exists
         if (mkdir(tmp, 0755) != 0) {  // If not, try to create it
             perror("mkdir");  // Print error if mkdir fails
             return -1;  // Return error code
         }
     }
     return 0;  // Return success if all directories are created or already exist
 }
 
 // receive_file - Receives file data from the client and writes it to disk.
 // Expects first a string representing the file size, then the file data.
  
 int receive_file(int client_sock, const char *filepath) {  // Function to receive a file from client and save to 'filepath'
     char size_buf[64];  // Buffer to store the file size received as string
     int bytes_read = recv(client_sock, size_buf, sizeof(size_buf) - 1, 0);  // Receive the file size string from client
     if (bytes_read <= 0)  // If no bytes received or error occurred
         return -1;  // Return error code
     size_buf[bytes_read] = '\0';  // Null-terminate the received string
     long file_size = atol(size_buf);  // Convert file size string to a long integer
     if (file_size <= 0)  // Validate that file size is positive
         return -1;  // Return error code if invalid
     FILE *fp = fopen(filepath, "wb");  // Open the destination file in binary write mode
     if (!fp) {  // Check if the file could not be opened
         perror("fopen");  // Print error message
         return -1;  // Return error code
     }
     char file_buf[BUFFER_SIZE];  // Buffer to hold chunks of file data
     long remaining = file_size;  // Initialize remaining bytes counter with file size
     while (remaining > 0) {  // Loop until all bytes are received
         int to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;  // Calculate number of bytes to read in this iteration
         int rec = recv(client_sock, file_buf, to_read, 0);  // Receive a chunk of data from client
         if (rec <= 0) {  // If receiving fails
             fclose(fp);  // Close the file
             return -1;  // Return error code
         }
         fwrite(file_buf, 1, rec, fp);  // Write the received data chunk to file
         remaining -= rec;  // Decrement the remaining byte counter
     }
     fclose(fp);  // Close the file after finishing reception
     return 0;  // Return success code
 }
 
 // send_file - Sends the file located at filepath to the client.
  
 int send_file(int client_sock, const char *filepath) {  // Function to send a file to the client
     FILE *fp = fopen(filepath, "rb");  // Open the file to be sent in binary read mode
     if (!fp) {  // Check if file opening failed
         perror("send_file: fopen failed");  // Print error message
         return -1;  // Return error code
     }
     fseek(fp, 0, SEEK_END);  // Seek to end of file to determine its size
     long file_size = ftell(fp);  // Get the size of the file
     rewind(fp);  // Reset file pointer to start of file
     char size_str[64];  // Buffer to hold the file size as a string
     snprintf(size_str, sizeof(size_str), "%ld", file_size);  // Convert file size to string format
     if (send(client_sock, size_str, strlen(size_str), 0) < 0) {  // Send the file size string to the client
         perror("send_file: sending file size failed");  // Print error if sending fails
         fclose(fp);  // Close the file
         return -1;  // Return error code
     }
     char file_buf[BUFFER_SIZE];  // Buffer for reading file data in chunks
     while (!feof(fp)) {  // Loop until the end-of-file is reached
         size_t n = fread(file_buf, 1, sizeof(file_buf), fp);  // Read a chunk of data from file
         if (n > 0) {  // If data was read successfully
             if (send(client_sock, file_buf, n, 0) < 0) {  // Send the chunk to the client
                 perror("send_file: sending file data failed");  // Print error if sending fails
                 fclose(fp);  // Close the file
                 return -1;  // Return error code
             }
         }
     }
     fclose(fp);  // Close the file after sending all data
     return 0;  // Return success code
 }
 
// error_exit - Prints an error message and exits.
 void error_exit(const char *msg) {  // Function to print error message and terminate the program
     perror(msg);  // Print the error message along with system error details
     exit(EXIT_FAILURE);  // Exit the program with a failure status
 }
