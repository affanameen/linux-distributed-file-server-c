// S4 handles .zip files only 
 #include <stdio.h>                       // Include standard I/O functions              
 #include <stdlib.h>                      // Include standard library functions          
 #include <string.h>                      // Include string handling functions           
 #include <unistd.h>                      // Include POSIX API functions                 
 #include <sys/types.h>                   // Include system data types                   
 #include <sys/socket.h>                  // Include socket functions                    
 #include <arpa/inet.h>                   // Include definitions for internet operations 
 #include <netinet/in.h>                  // Include internet protocol family definitions
 #include <errno.h>                       // Include error handling functions            
 #include <sys/stat.h>                    // Include file status and directory functions 
 
 #define SERVER_PORT 4644 // Define server port for S4 
 #define BUFFER_SIZE 1024 // Define buffer size for data transfers
 
 // Function prototypes
 void prcclient(int client_sock);               // Declare function to process client commands
 int create_directories(const char *path);      // Declare function to create directories recursively
 int receive_file(int client_sock, const char *filepath);  // Declare function to receive a file from client
 void error_exit(const char *msg);              // Prints error and exits
 
// main - Sets up the S4 server to listen on SERVER_PORT and handles connections.
 int main() {                                     // Begin main function of S4 server
     int server_sock, client_sock;              // Declare variables for server and client sockets  
     struct sockaddr_in server_addr, client_addr; // Declare structures for server and client addresses
     socklen_t client_addr_len = sizeof(client_addr);  // Determine length of client address structure
 
     if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) // Create TCP socket; check for errors
         error_exit("S4: socket creation failed");  // Exit if socket creation fails
 
     memset(&server_addr, 0, sizeof(server_addr)); // Zero out server address structure
     server_addr.sin_family = AF_INET;            // Set address family to IPv4
     server_addr.sin_addr.s_addr = INADDR_ANY;     // Accept connections from any IP 
     server_addr.sin_port = htons(SERVER_PORT);    // Set port number in network byte order
 
     if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) // Bind socket to address/port; check error
         error_exit("S4: bind failed");           // Exit if binding fails
 
     if (listen(server_sock, 10) < 0)              // Start listening with a backlog of 10 connections
         error_exit("S4: listen failed");         // Exit if listen fails
 
     printf("S4 Server (Zip file handler) listening on port %d...\n", SERVER_PORT); // Inform that S4 is running
 
     while (1) {                                // Loop forever to accept new connections
         client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len); // Accept a new connection; returns client socket
         if (client_sock < 0) {                 // Check if accept failed
             perror("S4: accept failed");       // Print error message if accept fails
             continue;                          // Continue to next connection
         }
         pid_t pid = fork();                    // Fork a child process to handle the client connection
         if (pid < 0) {                         // Check if fork failed
             perror("S4: fork failed");         // Print fork error
             close(client_sock);                // Close client socket on error
             continue;                          // Continue to next connection
         }
         if (pid == 0) {                        // Child process branch
             close(server_sock);                // Child doesn't need the listening socket; close it
             prcclient(client_sock);            // Process client commands in the child process
             exit(0);                           // Exit child process when done
         } else {                               // Parent process branch
             close(client_sock);                // Parent closes its copy of the client socket and continues
         }
     }
     close(server_sock);                        // Close the server socket 
     return 0;                                  // Return success
 }
 
 // prcclient - Processes commands from a connected client.
 // Only "uploadf" (for .zip files) and "exit" are supported.
  
 void prcclient(int client_sock) {              // Begin function to process client commands
     char buffer[BUFFER_SIZE];                  // Buffer to store incoming data from the client
     char *home_dir = getenv("HOME");           // Get the user's home directory from environment variables
     if (!home_dir)                             // If HOME is not set,
         home_dir = ".";                        // default to the current directory
 
     while (1) {                                // Loop to process commands continuously
         memset(buffer, 0, sizeof(buffer));     // Clear the buffer for new data
         int bytes = recv(client_sock, buffer, sizeof(buffer)-1, 0); // Receive data from the client
         if (bytes <= 0)                        // If no data received or connection error occurs,
             break;                             // exit the loop
         buffer[strcspn(buffer, "\r\n")] = 0;     // Remove newline characters from the received message
 
         char *command = strtok(buffer, " ");   // Tokenize the first word as the command
         if (!command)                          // If no command is present,
             continue;                          // skip to the next iteration
 
         if (strcmp(command, "uploadf") == 0) {   // If the command is "uploadf"
             // Expected format: uploadf <filename> <destination_path>

             char *filename = strtok(NULL, " ");  // Extract the filename
             char *destination = strtok(NULL, " "); // Extract the destination path
             if (!filename || !destination) {     // Validate that both parameters are provided
                 send(client_sock, "ERROR: Invalid uploadf command format.\n", 41, 0); // Inform client of format error
                 continue;                      // Continue to next command if parameters are missing
             }
             // Check that the file has a .zip extension.
             char *ext = strrchr(filename, '.');  // Find the last occurrence of '.' to get the extension
             if (!ext || strcmp(ext, ".zip") != 0) { // Verify that the extension exists and equals ".zip"
                 send(client_sock, "ERROR: Only .zip files allowed in S4.\n", 38, 0); // Notify client of invalid extension
                 continue;                      // Continue to next command if extension invalid
             }
             // Create destination directory under $HOME/S4.
             if (create_directories(destination) != 0) { // Try to create necessary directories
                 send(client_sock, "ERROR: Failed to create directory structure.\n", 48, 0); // Inform client if creation fails
                 continue;                      // Continue if directory creation failed
             }
             char local_filepath[512];          // Buffer for constructing full file path
             // Construct the full file path: $HOME/S4/<destination>/<filename>.
             snprintf(local_filepath, sizeof(local_filepath), "%s/%s/%s", home_dir, destination, filename); // Build path where file will be saved
             // Send READY to inform client that we're ready to receive.
             send(client_sock, "READY\n", 6, 0);  // Send "READY" response to client to start file transfer
             if (receive_file(client_sock, local_filepath) == 0) // Attempt to receive and store the file
                 send(client_sock, "File uploaded successfully to S4.\n", 34, 0); // Notify client of successful upload
             else
                 send(client_sock, "ERROR: Failed to receive file in S4.\n", 38, 0); // Notify client of reception failure
         }
         else if (strcmp(command, "exit") == 0) { // If the command is "exit"
             break;                             // Exit the loop and close the connection
         }
         else {                                   // If the command is unrecognized
             send(client_sock, "ERROR: Unknown command in S4.\n", 30, 0); // Notify client about unknown command
         }
     }
     close(client_sock);                        // Close the client socket after processing is complete
 }
 
 // create_directories - Recursively creates the directory structure under $HOME.
 int create_directories(const char *path) {     // Function to create directories recursively
     char full_path[512];                     // Buffer to hold the complete path
     char tmp[512];                           // Temporary buffer for manipulation
     char *p;                                 // Pointer to iterate through the path string
     size_t len;                              // Variable to store the length of the path string
 
     char *home_dir = getenv("HOME");         // Retrieve the HOME directory
     if (!home_dir)                           // If HOME is not set
         home_dir = ".";                      // Default to current directory
     snprintf(full_path, sizeof(full_path), "%s/%s", home_dir, path); // Construct the full path by concatenating HOME and provided path
     strncpy(tmp, full_path, sizeof(tmp));     // Copy full path to temporary buffer
     tmp[sizeof(tmp) - 1] = '\0';               // Ensure the temporary string is null-terminated
     len = strlen(tmp);                       // Calculate the length of the temporary path
     if (tmp[len - 1] == '/')                 // If the path ends with a slash
         tmp[len - 1] = '\0';                 // Remove the trailing slash
 
     for (p = tmp + 1; *p; p++) {             // Iterate through the temporary path starting after the first character
         if (*p == '/') {                     // When a slash is encountered (indicating a subdirectory)
             *p = '\0';                       // Temporarily terminate the string to get the current subdirectory path
             if (access(tmp, F_OK) != 0) {      // Check if the current subdirectory does not exist
                 if (mkdir(tmp, 0755) != 0) {   // Attempt to create the subdirectory with permissions 0755
                     perror("mkdir");         // Print error if directory creation fails
                     return -1;               // Return error code
                 }
             }
             *p = '/';                        // Restore the slash for further processing of the path
         }
     }
     if (access(tmp, F_OK) != 0) {              // After processing, check if the complete directory exists
         if (mkdir(tmp, 0755) != 0) {           // If not, try to create it
             perror("mkdir");                 // Print error message if creation fails
             return -1;                       // Return error code
         }
     }
     return 0;                                // Return success if directories exist or were created
 }
 
 // receive_file - Receives a file from the client and writes it to disk.
 // Expects first a string with the file size, then the file data.
  
 int receive_file(int client_sock, const char *filepath) { // Function to receive file data and save it to "filepath"
     char size_buf[64];                       // Buffer to hold file size (as string)
     int bytes = recv(client_sock, size_buf, sizeof(size_buf)-1, 0); // Receive file size from client
     if (bytes <= 0)                          // If receiving size fails
         return -1;                           // Return error code
     size_buf[bytes] = '\0';                  // Null-terminate the received size string
     long file_size = atol(size_buf);         // Convert size string to a long integer representing file size
     if (file_size <= 0)                      // If file size is not positive
         return -1;                           // Return error code
     FILE *fp = fopen(filepath, "wb");        // Open destination file in binary write mode
     if (!fp) {                               // If file cannot be opened
         perror("fopen");                     // Print error message
         return -1;                           // Return error code
     }
     char buf[BUFFER_SIZE];                   // Buffer to store chunks of incoming file data
     long remaining = file_size;              // Initialize remaining bytes counter with total file size
     while (remaining > 0) {                  // Loop until all expected file data is received
         int to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE; // Determine number of bytes to read in this iteration
         int rec = recv(client_sock, buf, to_read, 0); // Receive a chunk of file data
         if (rec <= 0) {                      // If receiving chunk fails
             fclose(fp);                      // Close the file
             return -1;                       // Return error code
         }
         fwrite(buf, 1, rec, fp);             // Write the received chunk to file
         remaining -= rec;                    // Decrement remaining byte count by number of bytes received
     }
     fclose(fp);                              // Close file after all data has been received
     return 0;                                // Return success code
 }
 
 // send_file - Sends the file at filepath to the client.
 // First sends the file size as a string, then streams the file data.

 int send_file(int client_sock, const char *filepath) { // Function to send a file to the client
     FILE *fp = fopen(filepath, "rb");        // Open the file in binary read mode
     if (!fp) {                               // If file opening fails
         perror("send_file: fopen failed");   // Print error message
         return -1;                           // Return error code
     }
     fseek(fp,0,SEEK_END);                    // Move file pointer to the end to determine size
     long file_size = ftell(fp);              // Get file size using ftell
     rewind(fp);                              // Reset file pointer to the beginning of the file
     char size_str[64];                       // Buffer to hold file size as a string
     snprintf(size_str, sizeof(size_str), "%ld", file_size); // Convert file size to string format
     if(send(client_sock, size_str, strlen(size_str), 0) < 0) { // Send file size string to client
         perror("send_file: sending file size failed"); // Print error if sending size fails
         fclose(fp);                          // Close file
         return -1;                           // Return error code
     }
     char buf[BUFFER_SIZE];                   // Buffer to hold file data chunks
     while(!feof(fp)) {                       // Loop until end-of-file is reached
         size_t n = fread(buf, 1, sizeof(buf), fp);  // Read a chunk of data from the file
         if(n > 0) {                        // If data was read successfully
             if(send(client_sock, buf, n, 0) < 0) {  // Send the data chunk to the client
                 perror("send_file: sending file data failed"); // Print error if sending fails
                 fclose(fp);                  // Close file
                 return -1;                   // Return error code
             }
         }
     }
     fclose(fp);                              // Close the file after all data is sent
     return 0;                                // Return success code
 }
 
 // error_exit - Prints an error message and exits the program.
 void error_exit(const char *msg) {            // Function to print an error message and exit
     perror(msg);                             // Print the error message along with system error details
     exit(EXIT_FAILURE);                      // Terminate the program with a failure status
 }
