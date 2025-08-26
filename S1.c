// S1 server communicates with the client
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#define SERVER_PORT 4641
#define BUFFER_SIZE 1024
#define PORT_S2 4642
#define PORT_S3 4643
#define PORT_S4 4644

// Structure for target server info
// Contains information about the target server for file operations
typedef struct {
    const char *server_id;                   // Identifier: "S2", "S3", or "S4"
    const char *ip;                          // IP address (e.g., "127.0.0.1")
    int port;
}TargetServer;

// Function prototypes 
void prcclient(int client_sock);
int create_directories(const char *path);
int receive_file(int client_sock, const char *filepath);
int send_file(int client_sock, const char *filepath);
int forward_file(const char *local_filepath, const char *filename, const char *target_dest, const char *target_ip, int target_port);   
int request_tar_from_target(TargetServer target, const char *filetype, const char *temp_tar_path);
void recursive_list_files(const char *dir_path, char ***file_array, int *count, int *capacity);
int compare_string(const void *a, const void *b);
void error_exit(const char *msg);

// main - Sets up the server socket on SERVER_PORT and handles incoming connections.
// Entry point of S1 server
int main() {
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)  
        error_exit("S1: socket creation failed");  // Exit if socket creation fails

    memset(&server_addr, 0, sizeof(server_addr));  // Clear server address structure
    server_addr.sin_family = AF_INET;          // Set address family to IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;   // Accept connections on any network interface
    server_addr.sin_port = htons(SERVER_PORT);  // Set server port in network byte order
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)  // Bind socket to the address and port
        error_exit("S1: bind failed");         // Exit if bind fails
    
    if (listen(server_sock, 10) < 0)           // Listen for incoming connections with a backlog of 10
        error_exit("S1: listen failed");       
    
    printf("S1 Server listening on port %d...\n", SERVER_PORT);  // Inform that server is up and running

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_addr_len); // Accept incoming connections
        if (client_sock < 0) {                 // Check for accept errors
            perror("S1: accept failed");   // Log accept failure
            continue;                       
        }
        pid_t pid = fork();                 // Create a new process
        if (pid < 0) {                         // Check for fork errors
            perror("S1: fork failed");         
            close(client_sock);               
            continue;                         
        }
        if (pid == 0) {                        // Child process: handles client's commands
            close(server_sock);               
            prcclient(client_sock);            // Process client commands
            exit(0);                           
        } else {
            close(client_sock);                
        }
    }
    close(server_sock);                        
    return 0;                                  // End program successfully
}

// prcclient - Processes commands received from a client
void prcclient(int client_sock) {              // Function to handle a clients session
    char buffer[BUFFER_SIZE];                  // Buffer for storing received commands
    char *home_dir = getenv("HOME");           // Get user's home directory from environment variables
    if (!home_dir)
        home_dir = ".";                        
    
    while (1) {                                
        memset(buffer, 0, sizeof(buffer));     // Clear the buffer before each new command
        int bytes_received = recv(client_sock, buffer, sizeof(buffer)-1, 0);  
        if (bytes_received <= 0)               
            break;
        buffer[strcspn(buffer, "\r\n")] = 0;   // Remove any newline characters from the command
        
        char *command = strtok(buffer, " ");  
        if (!command)
            continue;                         
        
        if (strcmp(command, "uploadf") == 0) {   // Handle 'uploadf' command

            // uploadf <filename> <destination_path>
            char *filename = strtok(NULL, " ");  // Get filename parameter
            char *destination = strtok(NULL, " ");  // Get destination path parameter
            if (!filename || !destination) {    
                send(client_sock, "ERROR: Invalid uploadf command format.\n", 41, 0); // error handling for incorrect format
                continue;
            }
            // Get file extension
            char *ext = strrchr(filename, '.');  
            if (!ext) {                          // If no extension found
                send(client_sock, "ERROR: File has no extension.\n", 30, 0);  
                continue;                        
            }
        
            // Ensure the destination path is valid and starts with "S1/"
            if (strncmp(destination, "S1/", 3) != 0) {  
                send(client_sock, "ERROR: Path must start with 'S1/'.\n", 34, 0);  // Send error message if not
                continue;               
            }
            if (create_directories(destination) != 0) {  // Attempt to create necessary directories
                send(client_sock, "ERROR: Failed to create local directory structure.\n", 52, 0);  // Error on failure
                continue;                        // Continue to next command
            }
            char local_filepath[512]; 
            snprintf(local_filepath, sizeof(local_filepath), "%s/%s/%s", home_dir, destination, filename); // string path construction

            if (strcmp(ext, ".c") == 0) {        
                send(client_sock, "READY\n", 6, 0); 
                if (receive_file(client_sock, local_filepath) == 0)  // file received successfully
                    send(client_sock, "File uploaded successfully in S1.\n", 34, 0);  
                else
                    send(client_sock, "ERROR: Failed to receive .c file.\n", 34, 0); 
            } else if (strcmp(ext, ".pdf") == 0 || strcmp(ext, ".txt") == 0 || strcmp(ext, ".zip") == 0) {  
                send(client_sock, "READY\n", 6, 0);  
                if (receive_file(client_sock, local_filepath) != 0) {  // file reception failed
                    send(client_sock, "ERROR: Failed to receive file for forwarding.\n", 48, 0);
                    continue;                    
                }
                TargetServer target;           // variable to hold the target server
                if (strcmp(ext, ".pdf") == 0) {         
                    target.server_id = "S2";            
                    target.ip = "127.0.0.1";            
                    target.port = PORT_S2;             
                } else if (strcmp(ext, ".txt") == 0) {  
                    target.server_id = "S3";            
                    target.ip = "127.0.0.1";            
                    target.port = PORT_S3;              
                } else if (strcmp(ext, ".zip") == 0) {  
                    target.server_id = "S4";            
                    target.ip = "127.0.0.1";            
                    target.port = PORT_S4;              
                } else {  
                    send(client_sock, "ERROR: Unsupported file type.\n", 31, 0); // error handling for unsupported file type  
                    continue;                   
                }
                char target_dest[512];           
                // Replace leading "S1" with the target server's identifier. 
                // This ensures the file is sent to the correct server.
                if (strncmp(destination, "S1", 2) == 0)
                    snprintf(target_dest, sizeof(target_dest), "%s%s", target.server_id, destination + 2);  // destination modification
                else
                    strncpy(target_dest, destination, sizeof(target_dest));
                printf("Forwarding %s to %s at %s:%d...\n", filename, target.server_id, target.ip, target.port);  // Log the forwarding action
                if (forward_file(local_filepath, filename, target_dest, target.ip, target.port) == 0) {  // Attempt to forward the file
                    if (remove(local_filepath) == 0)  // If forwarding succeeds then delete the local copy
                        send(client_sock, "File created successfully.\n", 27, 0);  // Notify success
                    else
                        send(client_sock, "Success but local deletion in S1 failed.\n", 50, 0);  // file created but local deletion in S1 failed
                } else {
                    send(client_sock, "ERROR: Forwarding failed.\n", 26, 0);  // Report forwarding failure
                }
            } else {
                send(client_sock, "ERROR: Unsupported file type.\n", 31, 0);  // Error for unknown file type uploads
            }
        }
        
        
else if (strcmp(command, "downlf") == 0) {      // to download file
    // Validate command format
    // Expected format: downlf <filepath>
    char *filepath_arg = strtok(NULL, " ");    // Extract the filepath from the command
    if (!filepath_arg) { // check that filepath was provided
        send(client_sock, "ERROR: Invalid downlf command format. Expected: downlf <filepath>\n", 68, 0);  // Inform client of format error
        continue; // continue to next command
        // 
    }

    char *ext = strrchr(filepath_arg, '.'); // Get the file extension from the provided path
    if (!ext) {  // If no extension is found
        send(client_sock, "ERROR: File has no extension.\n", 30, 0);  // Notify client about missing extension
        continue; // continue to next command
    }
    // Check that the path begins with "S1/"
    if (strncmp(filepath_arg, "S1/", 3) != 0) {  // Ensure the file path starts with "S1/"
        send(client_sock, "ERROR: Path must start with 'S1/'.\n", 34, 0);  // Inform client of the proper path format
        continue; // Continue to next command
    }

    if (strcmp(ext, ".c") != 0 && strcmp(ext, ".pdf") != 0 &&
        strcmp(ext, ".txt") != 0 && strcmp(ext, ".zip") != 0) {  // Only allow .c, .pdf, .txt, and .zip files
        send(client_sock, "ERROR: Unsupported file extension for download.\n", 50, 0);  // Send error if extension is unsupported
        continue; // continue to next command
    }

    char full_filepath[512];                   // Buffer to store the full filesystem path of the file
    // For .c files, use as-is; for others, substitute the corresponding base directory.
    if (strcmp(ext, ".c") == 0) { // If downloading a .c file
            snprintf(full_filepath, sizeof(full_filepath), "%s/%s", getenv("HOME"), filepath_arg);  // Use the given path directly
    }
    else if (strcmp(ext, ".pdf") == 0) {         // For .pdf files
        char *subpath = NULL;                    // Pointer for subpath within the directory
        
        if (strncmp(filepath_arg, "S1/", 3) == 0)
            subpath = filepath_arg + 3;          // Skip "S1/" prefix
        else
            subpath = filepath_arg;              // Use the entire string if no recognized prefix
        snprintf(full_filepath, sizeof(full_filepath), "%s/%s/%s", getenv("HOME"), "S2", subpath);  // Construct full path using S2 directory
    }
    else if (strcmp(ext, ".txt") == 0) {         // For .txt files
        char *subpath = NULL;                    // Pointer for subpath
        
        if (strncmp(filepath_arg, "S1/", 3) == 0)
            subpath = filepath_arg + 3;          // Skip "S1/" prefix
        else
            subpath = filepath_arg;              // Use as-is if prefix is not found
        snprintf(full_filepath, sizeof(full_filepath), "%s/%s/%s", getenv("HOME"), "S3", subpath);  // Build full path using S3 directory
    }
    else if (strcmp(ext, ".zip") == 0) {         // For .zip files
        char *subpath = NULL;                    // Pointer for subpath
        
        if (strncmp(filepath_arg, "S1/", 3) == 0)
            subpath = filepath_arg + 3;          // Remove "S1/" prefix
        else
            subpath = filepath_arg;              // Otherwise, use as provided
        snprintf(full_filepath, sizeof(full_filepath), "%s/%s/%s", getenv("HOME"), "S4", subpath);  // Construct path using S4 directory
    }
    else { // Fallback for unsupported file types
        send(client_sock, "ERROR: Unsupported file type for download.\n", 45, 0);  // Notify client of error
        continue; // continue to next command
    }
    
    // Check that the file exists and is a regular file.
    struct stat path_stat; // Structure for checking file status
    if (stat(full_filepath, &path_stat) != 0 || !S_ISREG(path_stat.st_mode)) {  // Verify file existence and that it is a regular file
        send(client_sock, "ERROR: Specified path is not a file.\n", 38, 0);  // Send error if file does not exist
        continue; // continue to next command
    }
    if (send_file(client_sock, full_filepath) != 0)  // Attempt to send the requested file to the client
        send(client_sock, "ERROR: Failed to send file. File may not exist.\n", 49, 0);  // Inform the client if sending fails
}

else if (strcmp(command, "removef") == 0) { // process 'removef' command to delete a file
    // Expected: removef <filepath>
    char *filepath_arg = strtok(NULL, " "); // Extract file path argument
    if (!filepath_arg) { // validate that a filepath is provided
        send(client_sock, "ERROR: Invalid removef command format. Expected: removef <filepath>\n", 69, 0);  // error message for invalid format
        continue; // continue to next command
    }
    // Check that the path begins with "S1/"
    if (strncmp(filepath_arg, "S1/", 3) != 0) {  // Verify that the file path starts with "S1/"
        send(client_sock, "ERROR: Path must start with 'S1/'.\n", 34, 0);  // Notify client about proper path format
        continue; // continue to next command
    }
    char *ext = strrchr(filepath_arg, '.');     // Extract file extension from the provided path
    if (!ext) {                                  // If extension is missing
        send(client_sock, "ERROR: File has no extension.\n", 30, 0);  // Error message for missing extension
        continue; // continue to next command
    }
    char full_filepath[512];                   // Buffer for constructing the complete file path
    if (strcmp(ext, ".c") == 0) {                // For .c files
            snprintf(full_filepath, sizeof(full_filepath), "%s/%s", home_dir, filepath_arg);  // Build path directly
    }
    else if (strcmp(ext, ".pdf") == 0) { // For .pdf files use the S2 directory
        char *subpath = NULL; // Temporary pointer for subpath
        if (strncmp(filepath_arg, "S1/", 3)==0)
            subpath = filepath_arg+3;            // Remove 'S1/' prefix
        else
            subpath = filepath_arg;              // Use as-is if no prefix
        snprintf(full_filepath, sizeof(full_filepath), "%s/%s/%s", home_dir, "S2", subpath);  // Construct path in S2
    }
    else if (strcmp(ext, ".txt") == 0) {         // For .txt files use the S3 directory
        char *subpath = NULL;                    // Temporary pointer for subpath
        
        if (strncmp(filepath_arg, "S1/", 3)==0)
            subpath = filepath_arg+3;            // Remove 'S1/' prefix
        else
            subpath = filepath_arg;              // Use as-is if no prefix
        snprintf(full_filepath, sizeof(full_filepath), "%s/%s/%s", home_dir, "S3", subpath);  // Build path in S3
    }
    else if (strcmp(ext, ".zip") == 0) {         // For .zip files use the S4 directory
        char *subpath = NULL;                    // Temporary pointer for subpath
        
        if (strncmp(filepath_arg, "S1/", 3)==0)
            subpath = filepath_arg+3;            // Remove 'S1/' prefix
        else
            subpath = filepath_arg;              // Otherwise, use as provided
        snprintf(full_filepath, sizeof(full_filepath), "%s/%s/%s", home_dir, "S4", subpath);  // Build path in S4
    }
    else { // If file type is unsupported for removal
        send(client_sock, "ERROR: Unsupported file type for removal.\n", 44, 0);  // Send error message
        continue; // continue to next command
    }
    struct stat path_stat; // Structure for checking the file's status
    if (stat(full_filepath, &path_stat) != 0 || !S_ISREG(path_stat.st_mode)) {  // Check if file exists and is regular
        send(client_sock, "ERROR: Specified path or file is not valid.\n", 55, 0);  
        continue; // continue to next command
    }
    if (remove(full_filepath) == 0) // Attempt to remove the file
        send(client_sock, "File removed successfully.\n", 29, 0);  // Inform client of success
    else
        send(client_sock, "ERROR: Failed to remove file. File may not exist.\n", 51, 0);  // Inform client of failure
}

else if (strcmp(command, "dispfnames") == 0) {  // Process 'dispfnames' command to list file names in a directory
    // Expected format: dispfnames S1/folder1/folder2 (or deeper)
    char *dir_arg = strtok(NULL, " ");         
    if (!dir_arg) {                            // Validate argument
        send(client_sock, "ERROR: Invalid dispfnames command format. Expected: dispfnames <directory>\n", 70, 0);  // Error message if missing
        continue;                              // Continue to next command
    }
    // Check that the path begins with "S1/"
    if (strncmp(dir_arg, "S1/", 3) != 0) {        // Ensure directory path starts with "S1/"
        send(client_sock, "ERROR: Path must start with 'S1/'.\n", 34, 0); 
        continue;                              // Continue to next command
    }
    // Extract the relative path after "S1/"
    char relative[512];                        // Buffer for relative path
    strcpy(relative, dir_arg + 3);             
     
    // Verify that the directory exists under S1.
    char check_path[512];                      // Buffer to hold full check path
    snprintf(check_path, sizeof(check_path), "%s/S1/%s", home_dir, relative);  
    struct stat st;                            // Structure for file/directory status
    if (stat(check_path, &st) != 0 || !S_ISDIR(st.st_mode)) {  
        send(client_sock, "ERROR: Path does not exist.\n", 29, 0);  
        continue;                              // Continue to next command
    }
    
    // Now gather files from the directories for each group in the required order.
    // Order: .c from S1, .pdf from S2, .txt from S3, .zip from S4
    const char *extensions[] = { ".c", ".pdf", ".txt", ".zip" };  
    const char *bases[] = { "S1", "S2", "S3", "S4" };  // Corresponding base directories for each file type
    
    // Buffer to hold the combined file names.
    char combined[8192];                       // Large buffer to accumulate file names
    combined[0] = '\0';                   
    
    for (int i = 0; i < 4; i++) {              // Loop over each file extension group
        char dir_path[512];                    
        // Construct the full directory path for this group.
        snprintf(dir_path, sizeof(dir_path), "%s/%s/%s", home_dir, bases[i], relative);  // Build path using home directory, base, and relative path
        
        // Check if the directory exists; if not, skip this group.
        if (stat(dir_path, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;                        // Skip if directory doesn't exist
        
        // Open the directory.
        DIR *d = opendir(dir_path);            
        if (d == NULL)
            continue;                        // Skip if unable to open directory
        // Allocate an array to store the matching file names.
        int capacity = 10, count = 0;         
        char **files = malloc(capacity * sizeof(char *));  // Allocate array of string pointers
        if (!files) {                          // If allocation fails
            closedir(d);                     
            continue;                        // Skip to next group
        }
        struct dirent *entry;                  // Pointer to read directory entries
        while ((entry = readdir(d)) != NULL) { 
            // Skip "." and ".."
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;                    // Ignore current and parent directory entries
            // Check the file extension.
            char *file_ext = strrchr(entry->d_name, '.');  // Get the extension of the file
            if (file_ext && strcmp(file_ext, extensions[i]) == 0) { 
                if (count >= capacity) {       
                    capacity *= 2;             // Double the capacity
                    files = realloc(files, capacity * sizeof(char *));  // Reallocate more space
                }
                files[count] = strdup(entry->d_name);  // Duplicate the file name and store it
                count++;                  
            }
        }
        closedir(d);                           // Close the directory after processing
        // Sort the file names for this file type.
        if (count > 0)
            qsort(files, count, sizeof(char *), compare_string);  // Alphabetically sort the file names
        // Append the sorted file names to the combined buffer.
        for (int j = 0; j < count; j++) {        
            strncat(combined, files[j], sizeof(combined) - strlen(combined) - 1);  
            strncat(combined, "\n", sizeof(combined) - strlen(combined) - 1);  
            free(files[j]);                  // Free the allocated string for the file name
        }
        free(files);                         // Free the file array pointer
    }
    if (strlen(combined) == 0)            
        strcpy(combined, "No files found.\n"); // Set the message to inform the client
    
    send(client_sock, combined, strlen(combined), 0);  
}
else if (strcmp(command, "downltar") == 0) {   // Process 'downltar' command to send a tar archive of files
    // Expected format: downltar <filetype>
    char *filetype = strtok(NULL, " ");        
    if (!filetype) {                           // Validate that filetype is provided
        send(client_sock, "ERROR: Invalid downltar command format. Expected: downltar <filetype>\n", 68, 0);  // Send error message
        continue; // continue to next command
    }
    
    if (strcmp(filetype, ".c") == 0) {          
        // Create tar archive for .c files from $HOME/S1.
        char tar_path[256];                    // Buffer for the tar file path
        snprintf(tar_path, sizeof(tar_path), "%s/cfiles.tar", home_dir);  // Construct tar file path for .c files
        char cmd[2048];                        
        /* Using 'find' to list all .c files under $HOME/S1, then piping those paths into tar.
         * This preserves the relative directory structure.
         */
        snprintf(cmd, sizeof(cmd),
                 "find \"%s/S1\" -type f -name \"*.c\" | tar -cf %s -T -",  
                 home_dir, tar_path);           // Build command to create tar archive of .c files
        if (system(cmd) != 0) {                
            send(client_sock, "ERROR: Failed to create tar file for .c files.\n", 48, 0);  
            continue;                        // Continue to next command
        }
        if (send_file(client_sock, tar_path) == 0)  // Send the tar file to the client
            remove(tar_path);                
        else
            send(client_sock, "ERROR: Failed to send tar file.\n", 31, 0);  // Inform client if sending fails
    }
    else if (strcmp(filetype, ".pdf") == 0) {    // If tar archive requested for .pdf files
        // Create tar archive for .pdf files from $HOME/S2.
        char tar_path[256];                 
        snprintf(tar_path, sizeof(tar_path), "%s/pdf.tar", home_dir);  // Construct tar file path for PDFs
        char cmd[2048];                       
        snprintf(cmd, sizeof(cmd),
                 "find \"%s/S2\" -type f -name \"*.pdf\" | tar -cf %s -T -",  
                 home_dir, tar_path);           
        if (system(cmd) != 0) {                // Execute the command and check for errors
            send(client_sock, "ERROR: Failed to create tar file for .pdf files.\n", 48, 0);  // Inform client of error
            continue;                        // Continue to next command
        }
        if (send_file(client_sock, tar_path) == 0)  // Send the tar file
            remove(tar_path);                
        else
            send(client_sock, "ERROR: Failed to send tar file.\n", 31, 0);  // Error message on failure
    }
    else if (strcmp(filetype, ".txt") == 0) {    // If tar archive requested for .txt files
        // Create tar archive for .txt files from $HOME/S3.
        char tar_path[256];                    // Buffer for tar file path
        snprintf(tar_path, sizeof(tar_path), "%s/text.tar", home_dir); 
        char cmd[2048];                        // Buffer for tar command
        snprintf(cmd, sizeof(cmd),
                 "find \"%s/S3\" -type f -name \"*.txt\" | tar -cf %s -T -",  
                 home_dir, tar_path);           
        if (system(cmd) != 0) {                // Execute command; check for errors
            send(client_sock, "ERROR: Failed to create tar file for .txt files.\n", 48, 0);  // Inform client of error
            continue;                        // Continue to next command
        }
        if (send_file(client_sock, tar_path) == 0)  
            remove(tar_path);                // Delete the tar file after sending
        else
            send(client_sock, "ERROR: Failed to send tar file.\n", 31, 0);  
    }
    else {                                    
        send(client_sock, "ERROR: Unsupported filetype for downltar.\n", 44, 0);  // Send error message
    }
}


else if (strcmp(command, "exit") == 0) {      
            break;                           
        }
        else { // For any unrecognized command
            send(client_sock, "ERROR: Invalid command. Try again!\n", 35, 0);  // Inform the client about an invalid command
        }
    }
    close(client_sock); // close the client socket when done
}


// recursive_list_files - Recursively traverses dir_path and collects the base names of all regular files.
 
void recursive_list_files(const char *dir_path, char ***file_array, int *count, int *capacity) {  // Recursively list files in a directory
    DIR *d = opendir(dir_path);                // Open the directory for reading
    if (d == NULL)
        return;                              
    struct dirent *entry;                    
    char full_path[512];                      
    struct stat st;                          // File status structure
    while ((entry = readdir(d)) != NULL) {    
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;                        // Skip "." and ".." entries
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);  // Build full path for the entry
        if (stat(full_path, &st) == 0) {       
            if (S_ISDIR(st.st_mode))
                recursive_list_files(full_path, file_array, count, capacity);  // Recurse into subdirectories
            else if (S_ISREG(st.st_mode)) {   // If it's a regular file
                if (*count >= *capacity) {   
                    *capacity *= 2;           // Double the capacity
                    *file_array = realloc(*file_array, sizeof(char*) * (*capacity));  // Reallocate array with new capacity
                }
                (*file_array)[*count] = strdup(entry->d_name);  // Duplicate file name and store it
                (*count)++; // Increment count of files
            }
        }
    }
    closedir(d); // close the directory
}
// compare_string - Comparator function for qsort.
int compare_string(const void *a, const void *b) {  // Comparator for sorting strings alphabetically
    const char *s1 = *(const char **) a;      
    const char *s2 = *(const char **) b;      
    return strcmp(s1, s2);                   
}

// request_tar_from_target - Contacts a target server (S2 or S3) to request a tar file.
// It sends "downltar <filetype>" and saves the received file to temp_tar_path.
int request_tar_from_target(TargetServer target, const char *filetype, const char *temp_tar_path) {  // Request tar archive from target server
    int sock = socket(AF_INET, SOCK_STREAM, 0);  
    if (sock < 0) {                       
        perror("request_tar_from_target: socket creation failed");  // Print error message
        return -1;                         // Return error code
    }
    struct sockaddr_in target_addr;          // Structure for target server address
    memset(&target_addr, 0, sizeof(target_addr)); 
    target_addr.sin_family = AF_INET;         
    target_addr.sin_port = htons(target.port);  
    if (inet_pton(AF_INET, target.ip, &target_addr.sin_addr) <= 0) {  // Convert target IP from text to binary form
        perror("request_tar_from_target: invalid target address");  
        close(sock); // Close the socket
        return -1; // Return error code
    }
    if (connect(sock, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0) {  
        perror("request_tar_from_target: connection to target server failed");  
        close(sock); 
        return -1; 
    }
    char cmd[256];                      
    snprintf(cmd, sizeof(cmd), "downltar %s\n", filetype);  // Build command string (e.g., "downltar .pdf")
    if (send(sock, cmd, strlen(cmd), 0) < 0) {  
        perror("request_tar_from_target: sending command failed");  // Print error if sending fails
        close(sock); 
        return -1; 
    }
    int ret = receive_file(sock, temp_tar_path);  
    close(sock);
    return ret; 
}
// send_file - Sends the file at 'filepath' to the client - It sends the file size as a string followed by the file data.
int send_file(int client_sock, const char *filepath) {  
    FILE *fp = fopen(filepath, "rb");       
    if (!fp) {                               
        perror("send_file: fopen failed");   
        return -1;                           
    }
    fseek(fp, 0, SEEK_END);                  
    long file_size = ftell(fp);              
    rewind(fp);                              // Reset file pointer to beginning
    char size_str[64];                       // Buffer to store the size as a string
    snprintf(size_str, sizeof(size_str), "%ld", file_size);  // Convert file size to string
    if (send(client_sock, size_str, strlen(size_str), 0) < 0) { 
        perror("send_file: sending file size failed");  
        fclose(fp); 
        return -1; 
    }
    char file_buf[BUFFER_SIZE]; // Buffer to hold chunks of file data
    while (!feof(fp)) { 
        size_t n = fread(file_buf, 1, sizeof(file_buf), fp);  
        if (n > 0) { // If data was read
            if (send(client_sock, file_buf, n, 0) < 0) {  
                perror("send_file: sending file data failed");  // Print error if send fails
                fclose(fp); 
                return -1; 
            }
        }
    }
    fclose(fp); // Close the file after sending
    return 0; // Return success
}

// receive_file - Receives file data from the client and writes it to disk - expects first a string representing the file size, 
// then the file data.
int receive_file(int client_sock, const char *filepath) {  
    char size_buf[64];                       
    int bytes_read = recv(client_sock, size_buf, sizeof(size_buf)-1, 0);  // Read file size from client
    if (bytes_read <= 0)                    
        return -1;                          
    size_buf[bytes_read] = '\0';             
    long file_size = atol(size_buf);         
    if (file_size <= 0)                      
        return -1;                           
    FILE *fp = fopen(filepath, "wb");      
    if (!fp) {                               
        perror("fopen");                    
        return -1;                           
    }
    char file_buf[BUFFER_SIZE];              // Buffer to store chunks of file data
    long remaining = file_size;              // Track remaining bytes to be received
    while (remaining > 0) {                  
        int to_read = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;  
        int received = recv(client_sock, file_buf, to_read, 0);  
        if (received <= 0) {                 // Check if receiving failed
            fclose(fp);                   
            return -1;                       
        }
        fwrite(file_buf, 1, received, fp);   // Write received data to file
        remaining -= received;               // Decrease remaining byte count
    }
    fclose(fp);                             
    return 0;                               
}

// forward_file - Forwards a local file from S1 to a target server.
// Opens the file, connects to the target server, sends an "uploadf" command, waits for "READY", and then sends file size and file data.
 
int forward_file(const char *local_filepath, const char *filename,
                 const char *target_dest, const char *target_ip, int target_port) {  
    FILE *fp = fopen(local_filepath, "rb");  
    if (!fp) {                               
        perror("forward_file: fopen failed");  
        return -1;                        
    }
    fseek(fp, 0, SEEK_END);                  
    long file_size = ftell(fp);             
    rewind(fp);                             
    int sock = socket(AF_INET, SOCK_STREAM, 0); 
    if (sock < 0) {                         
        perror("forward_file: socket creation failed");  
        fclose(fp);                       
        return -1;                         
    }
    struct sockaddr_in target_addr;          // Structure to store target server address
    memset(&target_addr, 0, sizeof(target_addr));  
    target_addr.sin_family = AF_INET;        
    target_addr.sin_port = htons(target_port);  // Set target port in network byte order
    if (inet_pton(AF_INET, target_ip, &target_addr.sin_addr) <= 0) {  // Convert target IP address from text to binary format
        perror("forward_file: invalid target address");  
        fclose(fp);                       
        close(sock);                       
        return -1;                        
    }
    if (connect(sock, (struct sockaddr *)&target_addr, sizeof(target_addr)) < 0) {  // Connect to the target server
        perror("forward_file: connection to target server failed");  // Print error if connection fails
        fclose(fp);                        
        close(sock);                      
        return -1;                       
    }
    char cmd[BUFFER_SIZE];                   // Buffer for constructing the upload command for target server
    snprintf(cmd, sizeof(cmd), "uploadf %s %s", filename, target_dest);  
    if (send(sock, cmd, strlen(cmd), 0) < 0) { 
        perror("forward_file: sending command failed");  
        fclose(fp);                        
        close(sock);                       
        return -1;                         
    }
    char response[BUFFER_SIZE];              // Buffer to store response from target server
    memset(response, 0, sizeof(response));   
    int bytes = recv(sock, response, sizeof(response)-1, 0);  // Receive target server's response
    if (bytes <= 0 || strncmp(response, "READY", 5) != 0) { 
        fprintf(stderr, "forward_file: target server did not send READY\n");  
        fclose(fp);                       
        close(sock);                       
        return -1;                         
    }
    char size_str[64];                       
    snprintf(size_str, sizeof(size_str), "%ld", file_size); 
    if (send(sock, size_str, strlen(size_str), 0) < 0) {  
        perror("forward_file: sending file size failed");  
        fclose(fp);                        
        close(sock);                    
        return -1;                          
    }
    char file_buf[BUFFER_SIZE];              
    while (!feof(fp)) {                      
        size_t n = fread(file_buf, 1, sizeof(file_buf), fp);  // Read a chunk of the file
        if (n > 0) {                       
            if (send(sock, file_buf, n, 0) < 0) {  
                perror("forward_file: sending file data failed");  
                fclose(fp);               
                close(sock);               
                return -1;                
            }
        }
    }
    memset(response, 0, sizeof(response));   // Clear the response buffer before final response
    bytes = recv(sock, response, sizeof(response)-1, 0);  // Receive final response from target server
    if (bytes > 0)
        printf("Target server response: %s\n", response);  // Log the final response from target server
    fclose(fp);                             
    close(sock);                            
    return 0;                                
}

// create_directories
int create_directories(const char *path) {     
    char full_path[512];                     // Buffer for the full directory path
    char tmp[512];                           // Temporary buffer for manipulation
    char *p;                                 
    size_t len;                              
    
    // Get the home directory; if not set, use the current directory.
    char *home_dir = getenv("HOME");         // Retrieve user's HOME directory
    if (!home_dir)
        home_dir = ".";                      
    
    // Build the full path: "$HOME/path"
    snprintf(full_path, sizeof(full_path), "%s/%s", home_dir, path);  // Combine home directory with provided path
    
    // Copy full_path into a temporary buffer we can modify.
    strncpy(tmp, full_path, sizeof(tmp));     // Copy the full path to a temporary variable
    tmp[sizeof(tmp) - 1] = '\0';              
    len = strlen(tmp);                      
    
    // Remove trailing slash, if any.
    if(tmp[len - 1] == '/')                  
        tmp[len - 1] = '\0';                 
    
    // Create intermediate directories by iterating over the path.
    for(p = tmp + 1; *p; p++) {             
        if(*p == '/') {                   
            *p = '\0';                       
            if(access(tmp, F_OK) != 0) {       
                if(mkdir(tmp, 0755) != 0) {    
                    perror("mkdir");         
                    return -1;               
                }
            }
            *p = '/';                        // Restore the slash after checking/creating directory
        }
    }
    
    // Create the final directory if it does not exist.
    if(access(tmp, F_OK) != 0) {              // Check if the complete directory path exists
        if(mkdir(tmp, 0755) != 0) {           // Create the final directory if necessary
            perror("mkdir");                 
            return -1;                       
        }
    }
    
    return 0;  // return success
}

// error_exit - Prints an error message and exits.
void error_exit(const char *msg) {           
    perror(msg);                          
    exit(EXIT_FAILURE);                   
}
