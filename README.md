Overview
========
How to build a simple Web server using Native Threads aproach?

each connection is handled by a single process or native thread.
When using an thead pool, multiple process are created in advance
(prefork in Apache.). To keep it simple, a process is spawned each time a
connection is received.
other ways are:
Event-driven: Node(before), lighttpd
One process Per Core: nginx. Node

* main: daemon itself and ignore child programs,
creates a socket for incomming brower request, looping in accepting request,
and start child processes to handle them.

* web: called in child process, one process for each web request. transmit the requested
file to the brower and exit

main keep the listen fd and child process keep the connection fd.


usage: 
shoot it, and it will host a simple static webserver on current directory

Make it dynamic via CGI
request:

```
[METHOD] [REQUEST-URI] HTTP/[VER]
[fieldname1]: [field-value1]
[fieldname2]: [field-value2]

[request body, if any for PUT and POST]
```

response:

```
HTTP/1.0 200 OK
Server: foo
key: value pair
....
<!DOCTYPE>
....
```

TODO
===

add CGI, make it dynamic
in root/cgi-bin contain the file,



REF:
===
http://www.ibm.com/developerworks/systems/library/es-nweb/
http://beej.us/guide/bgnet/
http://csapp.cs.cmu.edu/

