s/^main()/nfsd_main(sock)/
s/RPC_ANYSOCK/sock, NFS_MAXDATA, NFS_MAXDATA/
s/#include <\(.*\)>/#include "\1"/
