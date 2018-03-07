This File is associated with two source code files, Server code and Client code.

To compile each source code file use the following command:

gcc -w -pthread `pkg-config --cflags opencv` -o Filename Filename.c `pkg-config --libs opencv` -lvlc 

Where Filename is Server for server code, and Client for client code.

Note that you must have libvlc-dev and libopencv-dev libraries installed on your machine to compile this code.

If compilation did not work, we provided an executable file for each of server and client code, where you need to give 'x' permission to each file.

The Server code takes only one command line argument, The TCP port to listent to RTSP connections throw it.

The client code takes 4 command line arguments, The server IP, Server TCP port, FileName to be streamed, and RTP port for client.

This code support only ".mpg" videos as there is many different types, where each one needs different streaming and parsing methods.

We noted that if the to codes are working on the same machine, the client will not receive all frames sent by the server, 
so please test these two codes on different machines.

The sever side id done by: Alaa AbuHantash and Mais Tawalbeh.
The client side is done by: Yousef Hadder and Ammar Omar.
