/**
 * ToreroServe: A Lean Web Server
 * COMP 375 - Project 02
 *
 * This program should take two arguments:
 * 	1. The port number on which to bind and listen for connections
 * 	2. The directory out of which to serve files.
 *
 *
 * Author 1: Branko Andrews bandrews@sandiego.edu
 * Author 2: Calvin Ferraro csferraro@sandiego.edu
 */

// standard C libraries
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

// operating system specific libraries
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "BoundedBuffer.hpp"

// C++ standard libraries
#include <vector>
#include <thread>
#include <string>
#include <iostream>
#include <system_error>
#include <fstream>
#include <sstream>  
#include <regex>
// Import Boost's Filesystem and shorten its namespace to "fs"
#include <filesystem>
#include <algorithm>
#include <iterator> 
namespace fs = std::filesystem;

using std::cout;
using std::string;
using std::vector;
using std::thread;

// This will limit how many clients can be waiting for a connection.
static const int BACKLOG = 10;

// forward declarations

int    createSocketAndListen(const int port_num);
void   acceptConnections(const int server_sock, string dir);
void   handleClient(string dir);
void   sendData(int socked_fd, const char *data, size_t data_length);
int    receiveData(int socked_fd, char *dest, size_t buff_size);
void   sendBadReq(int src);
bool   fileExists(string filename);
string getFileRequest(string request_string);
void   fileToBuffer(int src, string filename);
string getFileExtension(string filename);
bool   isImage(string extension);
bool   regexFormatCorrect(string request);
void   readAndSendFileData(int src, string filePath);
void   pageNotFound(int src);
string generateHTMLDir(vector<string> files);
const  string binaryExtensions[] = {"png", "jpg", "gif"};
BoundedBuffer buff(BACKLOG);

int main(int argc, char** argv) 
{
	if (argc != 3) {
		
		cout << "INCORRECT USAGE!\nUse like: torero-serve <port number> <document root directory>\n";
		exit(1);
	}

    /* Read the port number from the first command line argument. */
    int port = std::stoi(argv[1]);

	/* Create a socket and start listening for new connections on the
	 * specified port. */
	int server_sock = createSocketAndListen(port);
	
	string dir(argv[2]);

	/* Now let's start accepting connections. */
	acceptConnections(server_sock, dir);

		
    close(server_sock);


	return 0;
}

/**
 * Sends message over given socket, raising an exception if there was a problem
 * sending.
 *
 * @param socket_fd The socket to send data over.
 * @param data The data to send.
 * @param data_length Number of bytes of data to send.
 */
void sendData(int socked_fd, const char *data, size_t data_length) 
{
	int totalBytesSent = 0;

	while (totalBytesSent != (int)data_length)
	{
	  int num_bytes_sent = send(socked_fd, data, data_length, 0);
	  
	  totalBytesSent += num_bytes_sent;
	  
	  if (num_bytes_sent == -1) 
	  {
		std::error_code ec(errno, std::generic_category());
		throw std::system_error(ec, "send failed");
	  }
	  
	}
}

/**
 * Receives message over given socket, raising an exception if there was an
 * error in receiving.
 *
 * @param socket_fd The socket to send data over.
 * @param dest The buffer where we will store the received data.
 * @param buff_size Number of bytes in the buffer.
 * @return The number of bytes received and written to the destination buffer.
 */
int receiveData(int socked_fd, char *dest, size_t buff_size) 
{
	int num_bytes_received = recv(socked_fd, dest, buff_size, 0);

	if (num_bytes_received == -1) {
		std::error_code ec(errno, std::generic_category());
		throw std::system_error(ec, "recv failed");
	}
	return num_bytes_received;
}

/**
 * Sends the 400 BAD REQUEST message
 *
 * @param src The socket where the message is sent from
 */
void sendBadReq(int src)
{
  string buffer = "";
  buffer += "HTTP/1.0 400 BAD REQUEST\r\n\r\n";
  sendData(src, buffer.c_str(), buffer.size());
}

/**
 * Sends the 404 PAGE NOT FOUND HTML message
 *
 * @param src the socket where the message is sent
 */
void pageNotFound(int src)
{
  string headerBuffer = "";
  
  string buffer = "";
  
  buffer += "<!DOCTYPE HTML PUBLIC -//IETF//DTD HTML 2.0//EN>\n";
  buffer += "<html>\n";
  buffer += "<header>\n";
  buffer += "<title>Page Not Found</title>\n";
  buffer += "</header>\n";
  buffer += "<body>\n";
  buffer += "<h1>\n";
  buffer += "HTTP ERROR 404\n";
  buffer += "</h1>\n";
  buffer += "<p>\n";
  buffer += "Page not Found\n";
  buffer += "</p>\n";
  buffer += "</body>\n";
  buffer += "</html>\n";
 
  headerBuffer += "HTTP/1.0 404 PAGE NOT FOUND\r\n";
  headerBuffer += "Content-length: " + std::to_string(buffer.size()) + "\r\n";
  headerBuffer += "Content-Type: text/html\r\n\r\n";
  
  sendData(src, headerBuffer.c_str(), headerBuffer.size());
  sendData(src, buffer.c_str(), buffer.size()); 
 
}

/**
 * Sends a file to the buffer to be sent out
 *
 * @param src The socket where the message is sent from
 * @param filename The file to be sent out
 */
void fileToBuffer(int src, string filename)
{
    std::ifstream file(filename, std::ios::binary);

    const unsigned int buffer_size = fs::file_size(filename);
    char file_data[buffer_size];
    int bytes_read = 0;

    while (!file.eof()) 
    {
      file.read(file_data, buffer_size); // read up to buffer_size bytes into file_data buffer
      bytes_read += file.gcount(); // find out how many bytes we actually read
    }
    sendData(src, file_data, buffer_size);
    file.close();
}

void readAndSendFileData(int src, string filePath) 
{    
    string fileExtension = getFileExtension(filePath);
    string buffer = ""; 
    buffer += "HTTP/1.0 200 OK\r\n";
    string contentType;
    int file_size;
    string lengthLine;
    string genDir;
    
    if(fileExtension.compare("")==0)
    {	
        vector<string> fileList;  
        bool flag = true; 
        
        for(const auto & entry : fs::directory_iterator(filePath))
        {
            fileList.push_back( entry.path());
            
            if(entry.path().filename().compare("index.html") == 0)
            {
              filePath += "/index.html";
              file_size = fs::file_size(filePath);
              lengthLine = "Content-length: " + std::to_string(file_size) + "\r\n"; 
              buffer += lengthLine;
              buffer += "Content-Type: text/html charset=iso-8859-1\r\n\r\n";
              sendData(src, buffer.c_str(), buffer.size());
              buffer = "";
              fileToBuffer(src, filePath);
              flag = false;
              break;
            }
            fileList.back() = fileList.back().substr(3);
        }
        if(flag)
        {
            file_size = generateHTMLDir(fileList).size();
            lengthLine = "Content-length: " + std::to_string(file_size) + "\r\n"; 
            buffer += lengthLine;
            buffer += "Content-Type: text/html charset=iso-8859-1\r\n\r\n";
            buffer += generateHTMLDir(fileList);
            sendData(src, buffer.c_str(), buffer.size());
        }
    }
    else
    {
        if(isImage(fileExtension))
        {
            contentType = "Content-Type: image/" + fileExtension + "\r\n\r\n";
        }
        else if(fileExtension.compare("pdf") == 0)
        {
            contentType = "Content-Type: application/" + fileExtension + "\r\n\r\n";
        }
        else
        {
            contentType = "Content-Type: text/" + fileExtension + " charset=iso-8859-1\r\n\r\n";
        }
        file_size = fs::file_size(filePath);
        lengthLine = "Content-length: " + std::to_string(file_size) + "\r\n";
        buffer += lengthLine;
        buffer += contentType;
        sendData(src, buffer.c_str(), buffer.size());
        buffer = "";
        fileToBuffer(src, filePath);
    }
}

/**
 * Generate an HTML to represent a directory if no index.html file is
 * available
 *
 * @param files A list of files to be included in the HTML
 */
string generateHTMLDir(vector<string> files)
{
	string text = "<html>\n<body>\n<ul>\n";
	for(vector<string>:: iterator it = files.begin(); it!=files.end(); ++it)
	{
		text += "\t<li><a href=\"" + *it + "/\">" + *it + "</a></li>\n";
	}
	text += "</ul>\n</body>\n</html>";
	return text;
}
	
string getFileRequest(string request_string)
{
  std::stringstream check1(request_string); 
  
  string intermediate; 
  
  string fileRequest;
  
  getline(check1, intermediate, ' ');
  
  getline(check1, fileRequest, ' ');
  
  return fileRequest;
}

/**
 * Gets the file extension from the file name
 *
 * @param filename The filename
 */
string getFileExtension(string filename)
{
	std::stringstream reader(filename);

	string name;

	string extension;

	getline(reader, name, '.');

	getline(reader, extension, '.');

	return extension;

}

bool isImage(string extension)
{
    return std::find(std::begin(binaryExtensions), std::end(binaryExtensions), extension) != std::end(binaryExtensions);
}

/**
 * Checks to see if the GET request matches the regex format
 *
 * @param request the request to be checked
 */
bool regexFormatCorrect(string request) 
{
	std::stringstream reader(request);
	string requestLine;
	getline(reader, requestLine, '\n');
  string reg = "GET ([ ]*(/[a-zA-Z0-9_\\-\\.]*)*)[ ]*HTTP/([0-9])\\.([0-9])[\r\n]*";
	std::regex http_request_regex(reg, std::regex_constants::ECMAScript);
  std::smatch request_match;
	bool ret = std::regex_match(requestLine, request_match, http_request_regex);
  return ret;
}

/**
 * Checks to see if the file exists in the file architecture
 *
 * @param filename file that we are looking for
 */
bool fileExists(string filename)
{
    std::ifstream f(filename.c_str());
    return f.good();
}

/**
 * Receives a request from a connected HTTP client and sends back the
 * appropriate response.
 *
 * @note After this function returns, client_sock will have been closed (i.e.
 * may not be used again).
 *
 * @param client_sock The client's socket file descriptor.
 */
void handleClient(string dir) {
	while(true)
	{
		int client_sock = buff.getItem();

		// Step 1: Receive the request message from the client
		char received_data[2048];
		int bytes_received = receiveData(client_sock, received_data, 2048);
	
		// Turn the char array into a C++ string for easier processing.
		string request_string(received_data, bytes_received);
	
	    string filePath = dir + getFileRequest(request_string);
		if(!regexFormatCorrect(request_string))
		{ 
			  sendBadReq(client_sock);
		}   
		else if(!fs::exists(filePath))
		{ 
			  pageNotFound(client_sock);
		}
		else
		{
			  readAndSendFileData(client_sock, filePath);
		}
		close(client_sock);
	}
}

/**
 * Creates a new socket and starts listening on that socket for new
 * connections.
 *
 * @param port_num The port number on which to listen for connections.
 * @returns The socket file descriptor
 */
int createSocketAndListen(const int port_num) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Creating socket failed");
        exit(1);
    }

    /* 
	 * A server socket is bound to a port, which it will listen on for incoming
     * connections.  By default, when a bound socket is closed, the OS waits a
     * couple of minutes before allowing the port to be re-used.  This is
     * inconvenient when you're developing an application, since it means that
     * you have to wait a minute or two after you run to try things again, so
     * we can disable the wait time by setting a socket option called
     * SO_REUSEADDR, which tells the OS that we want to be able to immediately
     * re-bind to that same port. See the socket(7) man page ("man 7 socket")
     * and setsockopt(2) pages for more details about socket options.
	 */
    int reuse_true = 1;

	  int retval; // for checking return values

    retval = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse_true,
                        sizeof(reuse_true));

    if (retval < 0) {
        perror("Setting socket option failed");
        exit(1);
    }

    /*
	 * Create an address structure.  This is very similar to what we saw on the
     * client side, only this time, we're not telling the OS where to connect,
     * we're telling it to bind to a particular address and port to receive
     * incoming connections.  Like the client side, we must use htons() to put
     * the port number in network byte order.  When specifying the IP address,
     * we use a special constant, INADDR_ANY, which tells the OS to bind to all
     * of the system's addresses.  If your machine has multiple network
     * interfaces, and you only wanted to accept connections from one of them,
     * you could supply the address of the interface you wanted to use here.
	 */
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_num);
    addr.sin_addr.s_addr = INADDR_ANY;

    /* 
	 * As its name implies, this system call asks the OS to bind the socket to
     * address and port specified above.
	 */
    retval = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (retval < 0) {
        perror("Error binding to port");
        exit(1);
    }

    /* 
	 * Now that we've bound to an address and port, we tell the OS that we're
     * ready to start listening for client connections. This effectively
	 * activates the server socket. BACKLOG (a global constant defined above)
	 * tells the OS how much space to reserve for incoming connections that have
	 * not yet been accepted.
	 */
    retval = listen(sock, BACKLOG);
    if (retval < 0) {
        perror("Error listening for connections");
        exit(1);
    }

	return sock;
}

/**
 * Sit around forever accepting new connections from client.
 *
 * @param server_sock The socket used by the server.
 */
void acceptConnections(const int server_sock, string dir) 
{

	for(int i = 0; i < 8; i++)
	{
		thread consumer(handleClient, dir);
		consumer.detach();	
	}
	while (true) 
	{
        // Declare a socket for the client connection.
	    int sock;

        /* 
		 * Another address structure.  This time, the system will automatically
         * fill it in, when we accept a connection, to tell us where the
         * connection came from.
		 */
        struct sockaddr_in remote_addr;
        unsigned int socklen = sizeof(remote_addr); 

        /* 
		 * Accept the first waiting connection from the server socket and
         * populate the address information.  The result (sock) is a socket
         * descriptor for the conversation with the newly connected client.  If
         * there are no pending connections in the back log, this function will
         * block indefinitely while waiting for a client connection to be made.
         */
        sock = accept(server_sock, (struct sockaddr*) &remote_addr, &socklen);
        if (sock < 0) 
		    {
            perror("Error accepting connection");
            exit(1);
        }
		buff.putItem(sock);
    }
}

