#include "stdio.h"
#include "rpc/rpc.h"
#include "mount.h"

static void mountprog_1();

mountd_main(sock)
{
	SVCXPRT *transp;

	(void)pmap_unset(MOUNTPROG, MOUNTVERS);

	transp = svcudp_create(sock, 4096, 4096);
	if (transp == NULL) {
		(void)fprintf(stderr, "cannot create udp service.\n");
		exit(1);
	}
	if (!svc_register(transp, MOUNTPROG, MOUNTVERS, mountprog_1, IPPROTO_UDP)) {
		(void)fprintf(stderr, "unable to register (MOUNTPROG, MOUNTVERS, udp).\n");
		exit(1);
	}
	svc_run();
	(void)fprintf(stderr, "svc_run returned\n");
	exit(1);
}

static void
mountprog_1(rqstp, transp)
	struct svc_req *rqstp;
	SVCXPRT *transp;
{
	union {
		dirpath mountproc_mnt_1_arg;
		dirpath mountproc_umnt_1_arg;
	} argument;
	char *result;
	bool_t (*xdr_argument)(), (*xdr_result)();
	char *(*local)();

	switch (rqstp->rq_proc) {
	case MOUNTPROC_NULL:
		xdr_argument = xdr_void;
		xdr_result = xdr_void;
		local = (char *(*)()) mountproc_null_1;
		break;

	case MOUNTPROC_MNT:
		xdr_argument = xdr_dirpath;
		xdr_result = xdr_fhstatus;
		local = (char *(*)()) mountproc_mnt_1;
		break;

	case MOUNTPROC_DUMP:
		xdr_argument = xdr_void;
		xdr_result = xdr_mountlist;
		local = (char *(*)()) mountproc_dump_1;
		break;

	case MOUNTPROC_UMNT:
		xdr_argument = xdr_dirpath;
		xdr_result = xdr_void;
		local = (char *(*)()) mountproc_umnt_1;
		break;

	case MOUNTPROC_UMNTALL:
		xdr_argument = xdr_void;
		xdr_result = xdr_void;
		local = (char *(*)()) mountproc_umntall_1;
		break;

	case MOUNTPROC_EXPORT:
		xdr_argument = xdr_void;
		xdr_result = xdr_exports;
		local = (char *(*)()) mountproc_export_1;
		break;

	case MOUNTPROC_EXPORTALL:
		xdr_argument = xdr_void;
		xdr_result = xdr_exports;
		local = (char *(*)()) mountproc_exportall_1;
		break;

	default:
		svcerr_noproc(transp);
		return;
	}
	bzero((char *)&argument, sizeof(argument));
	if (!svc_getargs(transp, xdr_argument, &argument)) {
		svcerr_decode(transp);
		return;
	}
	result = (*local)(&argument, rqstp);
	if (result != NULL && !svc_sendreply(transp, xdr_result, result)) {
		svcerr_systemerr(transp);
	}
	if (!svc_freeargs(transp, xdr_argument, &argument)) {
		(void)fprintf(stderr, "unable to free arguments\n");
		exit(1);
	}
}

