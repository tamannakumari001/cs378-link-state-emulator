#include <bits/stdc++.h>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <cstring>

using namespace std;

int main(){
    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(5000);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));

    string s = "";
    while(true){
        cout << "Enter your message rahh: ";
        getline(cin, s);

        if (s == "EXIT")
            break;

        const char* msg = s.c_str();
        send(clientSocket, msg, strlen(msg), 0);
    }

    close(clientSocket);
}