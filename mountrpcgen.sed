s/^main()/mountd_main(sock)/
s/RPC_ANYSOCK/sock, 4096, 4096/
s/#include <\(.*\)>/#include "\1"/
