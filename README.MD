# Redis From Scratch

A lightweight implementation of Redis server in C. 

Redis (REmote DIctionary Server) is an open-source, in-memory data storage and retrieval system. 
Building Redis from scratch helps greatly in understanding network programming and implementation of data structures.



"What I cannot create, I do not understand."
-Richard Feynmann.

# Compile
```
g++ -Wall -Wextra -O2 -g src/server.cpp -o /bin/server -std=c++17
g++ -Wall -Wextra -O2 -g src/server_kevent.cpp -o /bin/server_kevent -std=c++17
g++ -Wall -Wextra -O2 -g src/client.cpp -o /bin/client -std=c++17
g++ -Wall -Wextra -O2 -g src/client2.cpp -o /bin/client2 -std=c++17
```

# Run

Run server
```
./bin/server
./bin/server_kevent
```
To demonstrate sequential execution
```
./client1; ./client2;
```
To demonstrate parallel execution
```
./client1 & ./client2 
```
# References

https://app.codecrafters.io/courses/redis/introduction

https://build-your-own.org/redis/
