// This server handles file transfers for text files.              
 #include <stdio.h>             // Standard I/O functions              
 #include <stdlib.h>            // Standard library routines           
 #include <string.h>            // String handling functions           
 #include <unistd.h>            // POSIX API functions                 
 #include <sys/types.h>         // Data types for system calls         
 #include <sys/socket.h>        // Socket functions                    
 #include <arpa/inet.h>         // Definitions for internet operations 
 #include <netinet/in.h>        // Internet protocol family definitions
 #include <errno.h>             // Error reporting functions           
 #include <sys/stat.h>          // File status and directory functions 
 #include <dirent.h>            // Directory traversal functions       
 
 #define SERVER_PORT 4643       // S3 server listens on port 4643      
 #define BUFFER_SIZE 1024       // Buffer size for data transfers      
 
 // Function prototypes
 void prcclient(int client_sock);  // Process a connected client's commands
 int create_directories(const char *path);  // Recursively create directory structure
 int receive_file(int client_sock, const char *filepath);  // Receive a file from the client and save it
 int send_file(int client_sock, const char *filepath);  // Send a file to the client
 void error_exit(const char *msg); // Print an error message and exit
 
 // main - Sets up the S3 server socket, listens on SERVER_PORT, and forks a process for each connection.
 // Main function of S3 server
 int main() {                                      
     int server_sock, client_sock;                 // Variables for server and client socket descriptors
     struct sockaddr_in server_addr, client_addr;  // Structures for server and client addresses
     socklen_t client_addr_len = sizeof(client_addr);  // Length of client address structure
 
     if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)  // Create TCP socket; check for errors
         error_exit("S3: socket creation failed"); // Exit if socket creation fails
     memset(&server_addr, 0, sizeof(server_addr)); // Zero out server address structure
     server_addr.sin_family = AF_INET;             // Set address family to IPv4
     server_addr.sin_addr.s_addr = INADDR_ANY;     // Bind to any available network interface
     server_addr.sin_port = htons(SERVER_PORT);    // Set port number in network byte order
 
     if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)  // Bind the socket to our address
         error_exit("S3: bind failed");             // Exit if binding fails
     if (listen(server_sock, 10) < 0) {             // Start listening for incoming connections (max 10 pending)
         error_exit("S3: listen failed");           // Exit if listen fails
     }
     printf("S3 Server (Text file handler) listening on port %d...\n", SERVER_PORT); // Print server startup message
 
     while (1) { // Loop forever to accept new client connections
         client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len); // Accept a new client connection
         if (client_sock < 0) {                    // Check if accept failed
             perror("S3: accept failed");          // Print error message if accept fails
             continue;                             // Continue to next connection attempt
         }
         pid_t pid = fork();                       // Create a new process to handle the client
         if (pid < 0) {                            // Check if fork failed
             perror("S3: fork failed");            // Print error message if fork fails
             close(client_sock);                   // Close client socket on failure
             continue;                             // Continue to next connection
         }
         if (pid == 0) {                           // Child process executes this block
             close(server_sock);                   // Child process closes the listening socket
             prcclient(client_sock);               // Process the client's commands in the child process
             exit(0);                              // Exit child process after handling client
         } else {                                  // Parent process executes this block
             close(client_sock);                   // Parent closes its copy of the client socket
         }
     }
     close(server_sock);                           // Close the server socket (unreachable in this infinite loop)
     return 0;                                     // Return success status (never reached)
 }
 
 // prcclient - Processes commands from the connected client.
 void prcclient(int client_sock) {               
     char buffer[BUFFER_SIZE];                   // Buffer to store incoming command data
     char *home_dir = getenv("HOME");            // Get the user's home directory
     if (!home_dir)                              // If HOME is not set
         home_dir = ".";                         // Default to current directory
 
     while (1) {                                 // Loop to continuously process commands until exit
         memset(buffer, 0, sizeof(buffer));      // Clear the buffer for the next command
         int bytes_recv = recv(client_sock, buffer, sizeof(buffer)-1, 0);  // Receive command from client
         if (bytes_recv <= 0)                      // If no data received or error occurs
             break;                              // Exit the loop
         buffer[strcspn(buffer, "\r\n")] = 0;      // Remove newline characters from the received command
 
         char *command = strtok(buffer, " ");      // Tokenize the command (first word)
         if (!command)                             // If no command is found
             continue;                           // Skip to the next iteration
 
         if (strcmp(command, "uploadf") == 0) {      // Check if command is "uploadf"
             // Expected format: uploadf <filename> <destination_path>
             char *filename = strtok(NULL, " ");   // Get filename parameter
             char *destination = strtok(NULL, " ");  // Get destination directory parameter
             if (!filename || !destination) {       // Validate both parameters
                 send(client_sock, "ERROR: Invalid uploadf command format.\n", 41, 0); // Send error if missing parameters
                 continue;                         // Continue to next command
             }
             char *ext = strrchr(filename, '.');    // Find the file extension in filename
             if (!ext || strcmp(ext, ".txt") != 0) {  // Check if file extension is missing or not .txt
                 send(client_sock, "ERROR: Only .txt files allowed in S3.\n", 38, 0); // Send error message
                 continue;                         // Continue processing next command
             }
             if (create_directories(destination) != 0) {  // Create necessary directories under $HOME/S3
                 send(client_sock, "ERROR: Failed to create directory structure.\n", 48, 0); // Inform client if directory creation fails
                 continue;                         // Continue to next command
             }
             char local_filepath[512];             // Buffer to build full file path
             // File stored under $HOME/S3 destination: destination should be under S3
             snprintf(local_filepath, sizeof(local_filepath), "%s/%s/%s", home_dir, destination, filename); // Build complete file path
             send(client_sock, "READY\n", 6, 0);      // Send READY signal to client to start file transfer
             if (receive_file(client_sock, local_filepath) == 0)  // Receive file data and store it
                 send(client_sock, "File uploaded successfully to S3.\n", 34, 0); // Inform client that upload succeeded
             else
                 send(client_sock, "ERROR: Failed to receive file in S3.\n", 38, 0); // Inform client of failure
         }
         else if (strcmp(command, "downltar") == 0) {  // Check if command is "downltar"
             // Expected: downltar .txt
             char *filetype = strtok(NULL, " ");  // Get filetype parameter (should be ".txt")
             if (!filetype || strcmp(filetype, ".txt") != 0) {  // Validate filetype is provided and equals ".txt"
                 send(client_sock, "ERROR: Invalid downltar command for S3. Expected: downltar .txt\n", 64, 0); // Send error if invalid
                 continue;                         // Continue to next command
             }
             // Create tar archive of all .txt files under $HOME/S3.
             char tar_path[256];                  // Buffer for tar file path
             snprintf(tar_path, sizeof(tar_path), "%s/text.tar", home_dir);  // Build path for the text tar archive
             char cmd[1024];                      // Buffer for command string
             snprintf(cmd, sizeof(cmd), "tar -cf %s --wildcards '*.txt' -C \"%s/S3\" .", tar_path, home_dir); // Build command to create tar archive
             if (system(cmd) != 0) {              // Execute command and check for failure
                 send(client_sock, "ERROR: Failed to create tar file for .txt files.\n", 50, 0); // Inform client if tar fails
                 continue;                      // Continue to next command
             }
             if (send_file(client_sock, tar_path) == 0)  // Send the tar archive to the client
                 remove(tar_path);              // Remove tar archive from local storage after sending
             else
                 send(client_sock, "ERROR: Failed to send tar file.\n", 31, 0); // Inform client if sending fails
         }
         else if (strcmp(command, "exit") == 0) {   // Check if command is "exit"
             break;                             // Exit the command-processing loop
         }
         else {                                  // For any unknown command
             send(client_sock, "ERROR: Unknown command in S3.\n", 30, 0); // Inform client the command is invalid
         }
     }
     close(client_sock);                          // Close client socket when finished
 }
 
 // create_directories - Recursively creates the directory structure under $HOME.
 int create_directories(const char *path) {       
     char full_path[512];                         // Buffer for complete directory path
     char tmp[512];                               // Temporary buffer for modifications
     char *p;                                     // Pointer to iterate through the path string
     size_t len;                                  // Variable to hold string length
 
     char *home_dir = getenv("HOME");             // Get HOME directory
     if (!home_dir)
         home_dir = ".";                          // Default to current directory if HOME is not set
     snprintf(full_path, sizeof(full_path), "%s/%s", home_dir, path);  // Build full path by concatenating home directory and provided path
     strncpy(tmp, full_path, sizeof(tmp));         // Copy full path to temporary buffer
     tmp[sizeof(tmp) - 1] = '\0';                   // Ensure null termination
     len = strlen(tmp);                           // Calculate length of the temp string
     if(tmp[len-1]=='/')                         // If the path ends with '/'
         tmp[len-1] = '\0';                       // Remove trailing slash
 
     for(p = tmp+1; *p; p++) {                     // Iterate over the path starting from the second character
         if(*p=='/') {                           // If a '/' is encountered
             *p='\0';                           // Temporarily terminate the string here to isolate a subdirectory
             if(access(tmp, F_OK) != 0) {         // Check if the subdirectory does not exist
                 if(mkdir(tmp,0755) != 0) {       // Create the subdirectory with permissions 0755
                     perror("mkdir");           // Print error if mkdir fails
                     return -1;                 // Return error code
                 }
             }
             *p='/';                            // Restore the '/' character for further processing
         }
     }
     if(access(tmp, F_OK) != 0) {                  // Check if the final directory exists
         if(mkdir(tmp,0755) != 0) {               // Create the final directory if not present
             perror("mkdir");                   // Report error if creation fails
             return -1;                         // Return error code
         }
     }
     return 0;                                   // Return success
 }
 
// receive_file - Receives a file from the client and writes it to disk.
// Expects first a string with the file size, then the file data.
  
 int receive_file(int client_sock, const char *filepath) {
     char size_buf[64];                        // Buffer to hold the file size string
     int bytes = recv(client_sock, size_buf, sizeof(size_buf)-1, 0);  // Receive file size from client
     if (bytes <= 0)                           // Check if receiving failed
         return -1;                           // Return error code
     size_buf[bytes] = '\0';                   // Null-terminate the size string
     long file_size = atol(size_buf);          // Convert the received size string to a long integer
     if (file_size <= 0)                       // Verify that the file size is positive
         return -1;                           // Return error if invalid size
     FILE *fp = fopen(filepath, "wb");         // Open the destination file for binary write
     if (!fp) {                                // Check if file open failed
         perror("fopen");                      // Print error message
         return -1;                           // Return error code
     }
     char buf[BUFFER_SIZE];                    // Buffer for chunked file data
     long remaining = file_size;               // Set remaining bytes to the file size
     while (remaining > 0) {                   // Loop until all data is received
         int to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE; // Determine bytes to read in this iteration
         int rec = recv(client_sock, buf, to_read, 0);  // Receive a chunk of file data
         if (rec <= 0) {                       // If reception fails
             fclose(fp);                       // Close the file
             return -1;                       // Return error code
         }
         fwrite(buf, 1, rec, fp);              // Write the received data to file
         remaining -= rec;                     // Decrement the remaining bytes
     }
     fclose(fp);                               // Close the file after writing is complete
     return 0;                                 // Return success
 }
 
// send_file - Sends the file at filepath to the client.
// First sends the file size as a string, then streams the file data.
 int send_file(int client_sock, const char *filepath) { 
     FILE *fp = fopen(filepath, "rb");         // Open the file in binary read mode
     if (!fp) {                                // Check if file open failed
         perror("send_file: fopen failed");    // Print error message
         return -1;                           // Return error code
     }
     fseek(fp,0,SEEK_END);                     // Seek to end to determine file size
     long file_size = ftell(fp);               // Get the file size
     rewind(fp);                               // Rewind to beginning of file
     char size_str[64];                        // Buffer to hold file size as a string
     snprintf(size_str, sizeof(size_str), "%ld", file_size);  // Convert file size to string
     if(send(client_sock, size_str, strlen(size_str), 0) < 0) {  // Send file size string to client
         perror("send_file: sending file size failed");  // Print error if send fails
         fclose(fp);                          // Close file
         return -1;                           // Return error code
     }
     char buf[BUFFER_SIZE];                    // Buffer for file data chunks
     while(!feof(fp)) {                        // Loop until end-of-file is reached
         size_t n = fread(buf, 1, sizeof(buf), fp);  // Read a chunk from the file
         if(n > 0) {                         // If data is read
             if(send(client_sock, buf, n, 0) < 0) {  // Send the chunk to the client
                 perror("send_file: sending file data failed");  // Print error if sending fails
                 fclose(fp);                  // Close file
                 return -1;                   // Return error code
             }
         }
     }
     fclose(fp);                               // Close the file after sending
     return 0;                                 // Return success
 }
 
// error_exit - Prints an error message and exits.
 void error_exit(const char *msg) {           
     perror(msg);                              // Print the error message along with system error details
     exit(EXIT_FAILURE);                       // Exit the program with failure status
 }
 
