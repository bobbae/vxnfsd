#include "vxWorks.h"
#include "rpc/rpc.h"
#include "stdio.h"
#include "stdioLib.h"
#include "in.h"
#include "ioLib.h"
#include "socket.h"
#include "nfs_prot.h"
#include "stat.h"
#include "iosLib.h"
#include "memLib.h"
#include "limits.h"
#include "ctype.h"
#include "errno.h"
#include "errnoLib.h"
#include "fcntl.h"
#include "pathLib.h"
#include "mount.h"
#include "blkIo.h"
#include "rt11FsLib.h"
#include "dosFsLib.h"
#include "dirent.h"
#include "stdarg.h"
#include "hashLib.h"
#include "symLib.h"

#define MAX_EXPORTS 20		/* XXX */

char           *nfsd_exportfs[] = {	/* XXX */
	"/rt11/",
	"/msdog/",
	"/",
	"/usr",
	"/home",
	"/hack",
	"/maxtor",
	0
};

typedef struct {
	int             type;
	char            opaque[NFS_FHSIZE - sizeof(int)];
}               vxfh_t;

typedef struct {
	u_int           mode;
	u_int           uid;
	u_int           gid;
	u_int           size;
	u_int           atime;
	u_int           mtime;
}               mysattr_t;

void            nfsd();
void            mountd();
char           *fh2path();
void            nfsd_debug(char *,...);
void            nfsd_debugno();
void            path_to_all_lower();

int             nfsd_debug_on = 1;	/* XXX */
int             nfsd_task_id = 0;
int             nfsd_task_priority = 100;
int             nfsd_task_options = 0;
int             nfsd_task_stacksize = 10 * 1024;	/* too big? */
int             mountd_task_id = 0;
int             mountd_task_priority = 99;
int             mountd_task_options = 0;
int             mountd_task_stacksize = 10 * 1024;

int             rt11_create_size = 100 * 1024;	/* XXX */

int             msdog_bytes_per_block = 512;

int             nfsd_optimal_tsize = 8196;

SYMTAB_ID       nfsd_pathtab;
static char     nfsd_buffer[NFS_MAXDATA], path_buffer[PATH_MAX];

extern int      rt11FsDrvNum, dosFsDrvNum;

#define NFSD_MSDOS_TYPE 'm'

void
nfs_start(char *pathtab_name)
{
	FILE           *fp;
	char            path[PATH_MAX];
	int             id;

	if ((nfsd_task_id = taskSpawn("tNfsd", nfsd_task_priority,
				      nfsd_task_options, nfsd_task_stacksize,
				      nfsd, 0)) != ERROR &&
	    (mountd_task_id = taskSpawn("tMountd", mountd_task_priority,
				 mountd_task_options, mountd_task_stacksize,
					mountd, 0)) != ERROR) {
		nfsd_debug("nfs_start: nfsd and mountd started OK\n");

		if (!pathtab_name)
			return;

		nfsd_pathtab = symTblCreate(8, FALSE, memSysPartId);

		if (!nfsd_pathtab) {
			nfsd_debug("nfs_start: can't create nfsd_pathtab\n");
			return;
		}
		if ((fp = fopen(pathtab_name, "r")) == 0) {
			symTblDelete(nfsd_pathtab);
			nfsd_debug("nfs_start: can't open pathtab file <%s>\n",
				   pathtab_name);
			return;
		}
		while (fgets(nfsd_buffer, sizeof(nfsd_buffer), fp)) {
			sscanf(nfsd_buffer, "%s %d", path, &id);

			path_to_all_lower(path);

			if (symAdd(nfsd_pathtab, path, (char *) id,
				   (SYM_TYPE) NFSD_MSDOS_TYPE, 0) == ERROR) {
				symTblDelete(nfsd_pathtab);
				nfsd_debug("nfs_start: can't add path <%s>\n",
					   path);
				return;
			}
			nfsd_debug("nfs_start: added path <%s> id %x\n",
				   path, id);
		}

		nfsd_debug("nfs_start: %d paths added to pathtab\n",
			   nfsd_pathtab->nsymbols);
	} else {
		nfsd_debug("nfs_start: didn't work\n");

		if (nfsd_task_id != ERROR)
			taskDelete(nfsd_task_id);
	}
}

ftype
mode2type(u_long mode)
{
	switch (mode & S_IFMT) {
	case S_IFIFO:
		return NFFIFO;

	case S_IFCHR:
		return NFCHR;

	case S_IFDIR:
		return NFDIR;

	case S_IFBLK:
		return NFBLK;

	case S_IFREG:
		return NFREG;

	case S_IFLNK:
		return NFLNK;

	case S_IFSOCK:
		return NFSOCK;
	}

	return NFBAD;
}

void
path_to_all_lower(char *pathp)
{
	for (; pathp && *pathp; pathp++) {
		if (isupper(*pathp))
			*pathp = tolower(*pathp);
	}
}

char           *
fh2path(vxfh_t * fhp)
{
	int            *value;
	SYM_TYPE        type;

	if (fhp->type == rt11FsDrvNum)
		return (fhp->opaque);

	if (fhp->type == dosFsDrvNum) {
		value = (int *) fhp->opaque;

		/*
		 * XXX optimize -- prolly just can return
		 * ((SYMBOL)value)->name if symbol names are alloc'ed out of
		 * a separate memory partition, in exactly duplicate order.
		 */
		if (symFindByValue(nfsd_pathtab, *value, path_buffer,
				   value, &type) == ERROR) {
			nfsd_debug("fh2path failed for fstype %d, val = %d\n",
				   fhp->type, *value);
			return;
		}
		path_to_all_lower(path_buffer);

		return path_buffer;
	}
	nfsd_debug("fh2path failed fhp->type = %d\n", fhp->type);

	return NULL;
}

STATUS
newfh(vxfh_t * vxfhp, char *fullpath)
{
	DEV_HDR        *devhdrp;
	SYMBOL         *sp, *sp2;
	int             fd;
	char           *valuep, type;

	if ((fd = open(fullpath, O_RDONLY, 0)) < 0)
		return ERROR;

	if ((devhdrp = iosFdDevFind(fd)) == NULL) {
		close(fd);
		return ERROR;
	}
	if (devhdrp->drvNum == rt11FsDrvNum) {
		vxfhp->type = rt11FsDrvNum;
		strcpy(vxfhp->opaque, fullpath);
		close(fd);
		return OK;
	}
	if (devhdrp->drvNum == dosFsDrvNum) {
		path_to_all_lower(fullpath);

		if (symFindByName(nfsd_pathtab, fullpath, &valuep,
				  &type) == OK) {
			vxfhp->type = dosFsDrvNum;
			*((int *) (vxfhp->opaque)) = (int) valuep;

			close(fd);

			nfsd_debug("newfh: return existing valuep %x <%s>\n",
				   valuep, fullpath);

			return OK;
		}
		if (!(sp = symAlloc(nfsd_pathtab, fullpath, 0,
				    (SYM_TYPE) NFSD_MSDOS_TYPE, 0))) {
			nfsd_debug("newfh: can't symAlloc sp %x <%s>\n",
				   sp, fullpath);
			nfsd_debugno();
			close(fd);
			return ERROR;
		}
		sp->value = (char *) sp;

		if (symTblAdd(nfsd_pathtab, sp) == ERROR) {
			/*
			 * XXX -- only try 2 times if it fails. highly
			 * unlikely hash will collide two times in a row with
			 * different values. but this is obviously imperfect.
			 */
			if (!(sp2 = symAlloc(nfsd_pathtab, fullpath, 0,
					  (SYM_TYPE) NFSD_MSDOS_TYPE, 0))) {
				nfsd_debug("newfh: can't symAlloc sp2 <%s>\n",
					   fullpath);
				close(fd);
				symFree(nfsd_pathtab, sp);
				return ERROR;
			}
			symFree(nfsd_pathtab, sp);
			sp = sp2;
			sp->value = (char *) sp;

			if (symTblAdd(nfsd_pathtab, sp) == ERROR) {
				nfsd_debugno();
				nfsd_debug("newfh: couldn't add <%s>\n",
					   fullpath);
				symFree(nfsd_pathtab, sp);
				close(fd);

				return ERROR;
			}
		}
		nfsd_debug("newfh: <%s> added as <%x>\n",
			   fullpath, sp);

		nfsd_debug("newfh: total %d paths are in pathtab now.\n",
			   nfsd_pathtab->nsymbols);

		vxfhp->type = dosFsDrvNum;
		*((int *) (vxfhp->opaque)) = (int) sp;

		close(fd);
		return OK;
	}
	close(fd);
	return ERROR;
}

void
nfsd()
{
	struct sockaddr_in sin;
	int             sock;

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(NFS_PORT);
	sin.sin_addr.s_addr = INADDR_ANY;

	if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		nfsd_debug("nfsd: can't create nfsd socket\n");
		return;
	}
	if (bind(sock, &sin, sizeof(sin)) < 0) {
		nfsd_debug("nfsd: can't bind to port %d\n", NFS_PORT);
		return;
	}
	rpcTaskInit();

	nfsd_main(sock);
}

void           *
nfsproc_null_2()
{
	static char     dummy = (char) 0;
	return ((void *) &dummy);
}

int
rt11id(DEV_HDR * devhdrp, char *pathp)
{
	RT_VOL_DESC    *volp = (RT_VOL_DESC *) devhdrp;
	REQ_DIR_ENTRY   rde;
	int             fd;

	if (strcmp(devhdrp->name, pathp) == 0)
		return 0;

	pathp += strlen(pathp) - 1;

	while (*pathp-- != '/');

	pathp += 2;

	fd = open(devhdrp->name, O_RDONLY, 0);
	if (fd < 0)
		return -1;

	for (rde.entryNum = 0; ioctl(fd, FIODIRENTRY, (int) &rde) == OK;
	     rde.entryNum++) {
		if (strcmp(rde.name, pathp) == 0) {
			close(fd);
			return rde.entryNum + 1;
		}
	}

	close(fd);
	return -1;
}


STATUS
get_attrstat(char *pathp, struct fattr * fattrp, vxfh_t * fhp)
{
	struct stat     stat_holder;
	DEV_HDR        *devhdrp;
	int             fd = 0, junk;

	if (stat(pathp, &stat_holder) == OK) {
		fattrp->type = mode2type(stat_holder.st_mode);
		fattrp->mode = stat_holder.st_mode;
		fattrp->nlink = stat_holder.st_nlink;
		fattrp->uid = stat_holder.st_uid;
		fattrp->gid = stat_holder.st_gid;
		fattrp->size = stat_holder.st_size;
		fattrp->blocksize = stat_holder.st_blksize;
		fattrp->rdev = stat_holder.st_rdev;
		fattrp->blocks = stat_holder.st_blocks;
		fattrp->fileid = 0xfeedface;	/* XXX */
		fattrp->atime.seconds = stat_holder.st_atime;
		fattrp->mtime.seconds = stat_holder.st_mtime;
		fattrp->ctime.seconds = stat_holder.st_ctime;

		if ((fd = open(pathp, O_RDONLY, 0)) &&
		    (devhdrp = iosFdDevFind(fd))) {
			close(fd);
			fattrp->fsid = devhdrp->drvNum;

			if (devhdrp->drvNum == rt11FsDrvNum) {
				/*
				 * hand kludge up reasonable values for RT11
				 */
				fattrp->mode |= S_IRWXU | S_IRWXG | S_IRWXO;

				/*
				 * i dunno why rt11 does this! but fix it.
				 */
				if (fattrp->mode & S_IFIFO) {
					fattrp->mode &= ~S_IFIFO;
					fattrp->mode |= S_IFREG;
				}
				fattrp->blocksize = RT_BYTES_PER_BLOCK;
				fattrp->blocks = fattrp->size /
					RT_BYTES_PER_BLOCK;

				if (fattrp->blocks == 0 && fattrp->size > 0)
					fattrp->blocks = 1;

				fattrp->fileid = rt11id(devhdrp, pathp);
			} else if (devhdrp->drvNum == dosFsDrvNum) {
				/*
				 * XXX msdog filesys don't return blocksize
				 * and blocks... hack on top of hack.
				 */
				fattrp->blocksize = msdog_bytes_per_block;

				fattrp->blocks =
					fattrp->size / msdog_bytes_per_block;

				if (fattrp->blocks == 0 && fattrp->size > 0)
					fattrp->blocks = 1;

				fattrp->fileid = *((int *) (fhp->opaque));
			}
		}
		nfsd_debug("get_attrstat: stat ok, path=<%s>\n", pathp);
		nfsd_debug("f %d t %d m 0%o u %d g %d sz %d bs %d b %d i %x ",
			   fattrp->fsid,
			   fattrp->type,
			   fattrp->mode,
			   fattrp->uid,
			   fattrp->gid,
			   fattrp->size,
			   fattrp->blocksize,
			   fattrp->blocks,
			   fattrp->fileid);
		nfsd_debug("atime %x mtime %x\n",
			   fattrp->atime,
			   fattrp->mtime);

		return (OK);
	}
	nfsd_debugno();
	nfsd_debug("get_attrstat: stat failed, path=<%s>\n", pathp);

	if (fd)
		close(fd);

	return (ERROR);
}

attrstat       *
nfsproc_getattr_2(nfs_fh * fhp, struct svc_req * reqp)
{
	int             fd;
	struct fattr   *fattrp;
	static attrstat attrstat, *attrstatp;
	char           *pathp;

	nfsd_debug("getattr\n");

	attrstatp = &attrstat;
	bzero(attrstatp, sizeof(*attrstatp));
	fattrp = &attrstatp->attrstat_u.attributes;

	if ((pathp = fh2path((vxfh_t *) fhp)) == NULL) {
		attrstat.status = NFSERR_NOENT;
		return (&attrstat);
	}
	if (get_attrstat(pathp, fattrp, (vxfh_t *) fhp) == OK) {
		attrstat.status = NFS_OK;
		return (&attrstat);
	}
	attrstat.status = errnoGet();

	return (&attrstat);
}

attrstat       *
nfsproc_setattr_2(sattrargs * sattrargp, struct svc_req * reqp)
{
	static attrstat attrstat;
	struct stat     filestat;
	vxfh_t         *vxfhp;
	char           *pathp;
	mysattr_t      *msp;
	struct sattr   *sp;

	nfsd_debug("setattr: mode %x uid %x gid %x sz %x atime %x mtime %x\n",
		   sattrargp->attributes.mode,
		   sattrargp->attributes.uid,
		   sattrargp->attributes.gid,
		   sattrargp->attributes.size,
		   sattrargp->attributes.atime,
		   sattrargp->attributes.mtime
		);

	bzero(&attrstat, sizeof(attrstat));

	vxfhp = (vxfh_t *) (&sattrargp->file);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		attrstat.status = NFSERR_NOENT;
		return (&attrstat);
	}
	if (vxfhp->type == dosFsDrvNum) {
		msp = (mysattr_t *) & (vxfhp->opaque[4]);
		sp = &sattrargp->attributes;
		msp->mode = sp->mode;
		msp->uid = sp->uid;
		msp->gid = sp->gid;
		msp->size = sp->size;
		msp->atime = sp->atime.seconds;
		msp->mtime = sp->mtime.seconds;
	}
	attrstat.status = NFS_OK;
	return (&attrstat);
}

void           *
nfsproc_root_2()
{				/* obsolete */
	static char     dummy = (char) 0;

	nfsd_debug("root\n");

	return ((void *) &dummy);
}

diropres       *
nfsproc_lookup_2(diropargs * diropargp, struct svc_req * reqp)
{
	static diropres diropres;
	char            fullpath[PATH_MAX + 1];
	char           *pathp;
	vxfh_t         *vxfhp;

	nfsd_debug("lookup\n");

	bzero(&diropres, sizeof(diropres));

	vxfhp = (vxfh_t *) & (diropargp->dir);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		diropres.status = NFSERR_NOENT;
		return (&diropres);
	}
	vxfhp = (vxfh_t *) & diropres.diropres_u.diropres.file;

	if (pathCat(pathp, diropargp->name, fullpath) == OK &&
	    newfh(vxfhp, fullpath) == OK &&
	    get_attrstat(fullpath,
			 &diropres.diropres_u.diropres.attributes,
			 vxfhp) == OK) {
		diropres.status = NFS_OK;
		return (&diropres);
	}
	diropres.status = NFSERR_NOENT;
	return (&diropres);
}

readlinkres    *
nfsproc_readlink_2(nfs_fh * fhp, struct svc_req * reqp)
{
	static readlinkres readlinkres;

	nfsd_debug("linkres\n");

	bzero(&readlinkres, sizeof(readlinkres));
	readlinkres.status = NFS_OK;

	return (&readlinkres);
}

readres        *
nfsproc_read_2(readargs * readargp, struct svc_req * reqp)
{
	static readres  readres;
	int             fd;
	char           *pathp;

	nfsd_debug("read\n");

	bzero(&readres, sizeof(readres));

	if ((pathp = fh2path((vxfh_t *) & (readargp->file))) == NULL) {
		readres.status = NFSERR_NOENT;
		return (&readres);
	}
	if ((fd = open((char *) pathp, O_RDONLY, 0)) < 0) {
		readres.status = NFSERR_NXIO;
		return (&readres);
	}
	if (lseek(fd, readargp->offset, SEEK_SET) < 0 ||
	    (readres.readres_u.reply.data.data_len =
	     read(fd, nfsd_buffer, readargp->count)) < 0 ||
	    get_attrstat(pathp, &readres.readres_u.reply.attributes,
			 (vxfh_t *) & (readargp->file)) != OK) {
		readres.status = NFSERR_IO;
		close(fd);

		return (&readres);
	}
	readres.status = NFS_OK;
	readres.readres_u.reply.data.data_val = nfsd_buffer;

	close(fd);

	return (&readres);
}

void           *
nfsproc_writecache_2()
{				/* future -- nfs v.3 */
	static char     dummy = (char) 0;

	nfsd_debug("writecache\n");

	return ((void *) &dummy);
}

attrstat       *
nfsproc_write_2(writeargs * writeargp, struct svc_req * reqp)
{
	static attrstat attrstat;
	int             fd;
	char           *pathp;
	vxfh_t         *vxfhp;

	nfsd_debug("write\n");

	bzero(&attrstat, sizeof(attrstat));

	vxfhp = (vxfh_t *) & (writeargp->file);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		attrstat.status = NFSERR_NOENT;
		return (&attrstat);
	}
	if ((fd = open((char *) pathp, O_WRONLY, 0)) < 0) {
		attrstat.status = NFSERR_NXIO;
		nfsd_debugno();
		return (&attrstat);
	}
	nfsd_debug("writearg begoff %d off %d totlen %d len %d\n",
		   writeargp->beginoffset,	/* unused */
		   writeargp->offset,
		   writeargp->totalcount,	/* unused */
		   writeargp->data.data_len);

	if (lseek(fd, writeargp->offset, SEEK_SET) < 0 ||
	    write(fd, writeargp->data.data_val,
		  writeargp->data.data_len) < 0) {
		nfsd_debugno();
		attrstat.status = NFSERR_IO;
		close(fd);

		return (&attrstat);
	}
	close(fd);

	if (get_attrstat((char *) pathp,
			 &(attrstat.attrstat_u.attributes), vxfhp) != OK) {
		attrstat.status = errnoGet();
		return (&attrstat);
	}
	attrstat.status = NFS_OK;
	return (&attrstat);
}

diropres       *
nfsproc_create_2(createargs * createargp, struct svc_req * reqp)
{
	static diropres diropres;
	struct stat     stat_holder;
	char           *pathp;
	char            fullpath[PATH_MAX];
	vxfh_t         *vxfhp;
	int             fd;

	nfsd_debug("create\n");

	bzero(&diropres, sizeof(diropres));

	vxfhp = (vxfh_t *) & (createargp->where.dir);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		diropres.status = NFSERR_NOENT;
		return (&diropres);
	}
	if (pathCat(pathp, createargp->where.name, fullpath) != OK) {
		diropres.status = errnoGet();
		return (&diropres);
	}
	if (stat(fullpath, &stat_holder) == OK) {
		diropres.status = NFSERR_EXIST;
		return (&diropres);
	}
	nfsd_debug("creat: mode %x uid %x gid %x sz %x atime %x mtime %x\n",
		   createargp->attributes.mode,
		   createargp->attributes.uid,
		   createargp->attributes.gid,
		   createargp->attributes.size,
		   createargp->attributes.atime,
		   createargp->attributes.mtime
		);

	if ((fd = open(fullpath, O_RDWR | O_CREAT | O_TRUNC,
		       createargp->attributes.mode)) < 0) {
		diropres.status = errnoGet();
		return (&diropres);
	}
	if (vxfhp->type == rt11FsDrvNum) {
		lseek(fd, rt11_create_size, SEEK_SET);
		write(fd, "$", 1);
	}
	close(fd);

	vxfhp = (vxfh_t *) & diropres.diropres_u.diropres.file;

	if (newfh(vxfhp, fullpath) == OK &&
	    get_attrstat(fullpath,
			 &diropres.diropres_u.diropres.attributes,
			 vxfhp) == OK) {
		diropres.status = NFS_OK;
		return (&diropres);
	}
	diropres.status = errnoGet();
	return (&diropres);
}

nfsstat        *
nfsproc_remove_2(diropargs * diropargp, struct svc_req * reqp)
{
	static nfsstat  nfsstat;
	vxfh_t         *vxfhp;
	char           *pathp;
	char            fullpath[PATH_MAX + 1];

	nfsd_debug("remove\n");

	bzero(&nfsstat, sizeof(nfsstat));

	vxfhp = (vxfh_t *) & (diropargp->dir);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		nfsstat = NFSERR_NOENT;
		return (&nfsstat);
	}
	if (pathCat(pathp, diropargp->name, fullpath) == ERROR) {
		nfsstat = NFSERR_IO;
		return (&nfsstat);
	}
	nfsd_debug("remove: path <%s>\n", fullpath);

	if (remove(fullpath) == ERROR) {
		nfsstat = NFSERR_NOENT;
		nfsd_debugno();
		return (&nfsstat);
	}
	if (symRemove(nfsd_pathtab, fullpath,
		      (SYM_TYPE) NFSD_MSDOS_TYPE) != OK) {
		nfsstat = NFSERR_IO;	/* XXX bogus */
		nfsd_debugno();
		return (&nfsstat);
	}
	nfsstat = NFS_OK;

	return (&nfsstat);
}

nfsstat        *
nfsproc_rename_2(renameargs * renameargp, struct svc_req * reqp)
{
	static nfsstat  nfsstat;
	char           *pathp;
	char            from[PATH_MAX];
	char            to[PATH_MAX];
	vxfh_t         *vxfhp;

	nfsd_debug("rename\n");

	bzero(&nfsstat, sizeof(nfsstat));

	vxfhp = (vxfh_t *) & (renameargp->from);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		nfsstat = NFSERR_NOENT;
		return (&nfsstat);
	}
	if (pathCat(pathp, renameargp->from.name, from) == ERROR) {
		nfsstat = NFSERR_IO;
		return (&nfsstat);
	}
	vxfhp = (vxfh_t *) & (renameargp->to);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		nfsstat = NFSERR_NOENT;
		return (&nfsstat);
	}
	if (pathCat(pathp, renameargp->to.name, to) == ERROR) {
		nfsstat = NFSERR_IO;
		return (&nfsstat);
	}
	if (rename(from, to) == ERROR) {
		nfsstat = errnoGet();
		return (&nfsstat);
	}
	nfsstat = NFS_OK;

	return (&nfsstat);
}

nfsstat        *
nfsproc_link_2(linkargs * linkargp, struct svc_req * reqp)
{
	static nfsstat  nfsstat;

	nfsd_debug("link\n");

	bzero(&nfsstat, sizeof(nfsstat));

	return (&nfsstat);
}

nfsstat        *
nfsproc_symlink_2(symlinkargs * symlinkargp, struct svc_req * reqp)
{
	static nfsstat  nfsstat;

	nfsd_debug("symlink\n");

	bzero(&nfsstat, sizeof(nfsstat));

	return (&nfsstat);
}

diropres       *
nfsproc_mkdir_2(createargs * createargp, struct svc_req * reqp)
{
	static diropres diropres;
	vxfh_t         *vxfhp;
	char           *pathp;
	struct stat     stat_holder;
	char            fullpath[PATH_MAX];

	nfsd_debug("mkdir\n");

	bzero(&diropres, sizeof(diropres));

	vxfhp = (vxfh_t *) & (createargp->where.dir);

	if (vxfhp->type != dosFsDrvNum) {
		diropres.status = NFSERR_IO;	/* XXX bogus */
		return (&diropres);
	}
	if ((pathp = fh2path(vxfhp)) == NULL) {
		diropres.status = NFSERR_NOENT;
		return (&diropres);
	}
	if (pathCat(pathp, createargp->where.name, fullpath) != OK) {
		diropres.status = errnoGet();
		return (&diropres);
	}
	if (stat(fullpath, &stat_holder) == OK) {
		diropres.status = NFSERR_EXIST;
		return (&diropres);
	}
	nfsd_debug("mkdir: attrib.mode 0%o\n", createargp->attributes.mode);

	if (mkdir(fullpath) == ERROR) {
		diropres.status = errnoGet();
		return (&diropres);
	}
	if (newfh(vxfhp, fullpath) == OK &&
	    get_attrstat(fullpath,
			 &diropres.diropres_u.diropres.attributes,
			 vxfhp) == OK) {
		diropres.status = NFS_OK;
		return (&diropres);
	}
	diropres.status = errnoGet();
	return (&diropres);
}

nfsstat        *
nfsproc_rmdir_2(diropargs * diropargp, struct svc_req * reqp)
{
	static nfsstat  nfsstat;
	vxfh_t         *vxfhp;
	char           *pathp;
	char            fullpath[PATH_MAX + 1];

	nfsd_debug("rmdir\n");

	bzero(&nfsstat, sizeof(nfsstat));

	vxfhp = (vxfh_t *) & (diropargp->dir);

	if (vxfhp->type != dosFsDrvNum) {
		nfsstat = NFSERR_IO;	/* XXX bogus */
		return (&nfsstat);
	}
	if ((pathp = fh2path(vxfhp)) == NULL) {
		nfsstat = NFSERR_NOENT;
		return (&nfsstat);
	}
	if (pathCat(pathp, diropargp->name, fullpath) == ERROR) {
		nfsstat = NFSERR_IO;
		return (&nfsstat);
	}
	nfsd_debug("rmdir: path <%s>\n", fullpath);

	if (rmdir(fullpath) == ERROR) {
		nfsstat = NFSERR_NOENT;
		nfsd_debugno();
		return (&nfsstat);
	}
	nfsstat = NFS_OK;

	if (symRemove(nfsd_pathtab, fullpath,
		      (SYM_TYPE) NFSD_MSDOS_TYPE) != OK) {
		nfsstat = NFSERR_IO;	/* XXX bogus */
		nfsd_debugno();
		return (&nfsstat);
	}
	return (&nfsstat);
}

struct entry    nfsd_direntries[128];	/* XXX yucko! */

readdirres     *
nfsproc_readdir_2(readdirargs * readdirargp, struct svc_req * reqp)
{
	static readdirres readdirres;
	DIR            *dirp;
	struct dirent  *direntp;
	char           *pathp;
	vxfh_t         *vxfhp;
	int             fd;
	REQ_DIR_ENTRY   rde;
	struct dirlist *repp;
	entry         **enp;
	int             ix;
	int             entnum;

	nfsd_debug("readdir\n");

	bzero(&readdirres, sizeof(readdirres));

	vxfhp = (vxfh_t *) & (readdirargp->dir);

	if ((pathp = fh2path(vxfhp)) == NULL) {
		readdirres.status = errnoGet();
		return (&readdirres);
	}
	fd = open(pathp, O_RDONLY, 0);

	if (fd < 0) {
		readdirres.status = errnoGet();
		return (&readdirres);
	}
	repp = &readdirres.readdirres_u.reply;
	enp = &repp->entries;

	readdirargp->count = min(readdirargp->count, NFS_MAXDATA);

	nfsd_debug("readdir: path <%s> cookie %d  count %d\n",
		   pathp,
		   *(int *) (&(readdirargp->cookie)),
		   readdirargp->count);

	if (vxfhp->type == rt11FsDrvNum) {
		for (rde.entryNum = *(int *) (&(readdirargp->cookie)), ix = 0;
		     ioctl(fd, FIODIRENTRY, (int) &rde) == OK &&
		     ix < sizeof(nfsd_buffer) &&
		     ix < readdirargp->count;
		     rde.entryNum++) {
			*enp = &nfsd_direntries[rde.entryNum];
			(*enp)->fileid = rde.entryNum + 1;
			(*enp)->name = &nfsd_buffer[ix];
			strcpy((*enp)->name, rde.name);
			ix += strlen(rde.name) + 1;
			*(int *) (&((*enp)->cookie)) = rde.entryNum + 1;
			enp = &((*enp)->nextentry);
		}

		*enp = NULL;
		repp->eof = TRUE;	/* XXX */
		close(fd);

		nfsd_debug("readdir: entryNum %d ix %d\n", rde.entryNum, ix);

		readdirres.status = NFS_OK;
	} else if (vxfhp->type == dosFsDrvNum) {
		close(fd);

		if ((dirp = opendir(pathp)) == NULL) {
			nfsd_debug("readdir: cant opendir <%s>\n", pathp);
			readdirres.status = errnoGet();
			return (&readdirres);
		}
		for (entnum = *(int *) (&(readdirargp->cookie)), ix = 0;
		     (direntp = readdir(dirp)) != NULL &&
		     ix < sizeof(nfsd_buffer) &&
		     ix < readdirargp->count;
		     entnum++) {
			*enp = &nfsd_direntries[entnum];
			(*enp)->fileid = entnum + 1;
			(*enp)->name = &nfsd_buffer[ix];
			strcpy((*enp)->name, direntp->d_name);
			ix += strlen(direntp->d_name) + 1;
			*(int *) (&((*enp)->cookie)) = entnum + 1;
			enp = &((*enp)->nextentry);
		}

		*enp = NULL;
		repp->eof = TRUE;	/* XXX */
		closedir(dirp);
	}
	return (&readdirres);
}

statfsres      *
nfsproc_statfs_2(nfs_fh * fhp, struct svc_req * reqp)
{
	static statfsres statfsres;
	vxfh_t         *vxfhp;
	char           *pathp, *dummy;
	DEV_HDR        *devhdrp;
	RT_VOL_DESC    *rt11devp;
	DOS_VOL_DESC   *dosdevp;
	BLK_DEV        *blkp;

	nfsd_debug("statfs\n");

	bzero(&statfsres, sizeof(statfsres));

	vxfhp = (vxfh_t *) fhp;

	if ((pathp = fh2path(vxfhp)) == NULL) {
		statfsres.status = errnoGet();
		nfsd_debugno();
		return (&statfsres);
	}
	if ((devhdrp = iosDevFind(pathp, &dummy)) == NULL) {
		statfsres.status = errnoGet();
		nfsd_debugno();
		return (&statfsres);
	}
	if (vxfhp->type == rt11FsDrvNum) {
		rt11devp = (RT_VOL_DESC *) devhdrp;
		statfsres.statfsres_u.reply.tsize = nfsd_optimal_tsize;
		statfsres.statfsres_u.reply.bsize = RT_BYTES_PER_BLOCK;
		statfsres.statfsres_u.reply.blocks = rt11devp->vd_nblocks;

		/*
		 * XXX - bogus  vlaues -- make it look like all blocks are
		 * available since there's no easy way to tell
		 */
		statfsres.statfsres_u.reply.bfree = rt11devp->vd_nblocks;
		statfsres.statfsres_u.reply.bavail = rt11devp->vd_nblocks;
	} else if (vxfhp->type == dosFsDrvNum) {
		dosdevp = (DOS_VOL_DESC *) devhdrp;
		blkp = dosdevp->dosvd_pBlkDev;
		statfsres.statfsres_u.reply.tsize = nfsd_optimal_tsize;
		statfsres.statfsres_u.reply.bsize = blkp->bd_bytesPerBlk;
		statfsres.statfsres_u.reply.blocks = blkp->bd_nBlocks;

		/*
		 * XXX - bogus  vlaues -- make it look like all blocks are
		 * available since there's no easy way to tell
		 */
		statfsres.statfsres_u.reply.bfree = blkp->bd_nBlocks;
		statfsres.statfsres_u.reply.bavail = blkp->bd_nBlocks;
		/*
		 * save this info for later
		 */
		msdog_bytes_per_block = blkp->bd_nBlocks;
	}
	statfsres.status = NFS_OK;

	return (&statfsres);
}


void
mountd()
{
	rpcTaskInit();
	mountd_main(RPC_ANYSOCK);
}

void           *
mountproc_null_1()
{
	static char     dummy = (char) 0;

	nfsd_debug("mount null\n");
	return ((void *) &dummy);
}

fhstatus       *
mountproc_mnt_1(dirpath * dirpathp, struct svc_req * reqp)
{
	static fhstatus fhstatus;
	DEV_HDR        *dev_hdrp;
	char           *dummy;
	vxfh_t         *vxfhp;

	nfsd_debug("mount mnt\n");
	bzero(&fhstatus, sizeof(fhstatus));

	/*
	 * we only export filesystem device at the root. currently no config
	 * file is used. all rt11 and msdos filesystem devices are exported.
	 * no security & option support either.
	 */
	if ((dev_hdrp = iosDevFind(*dirpathp, &dummy)) == NULL) {
		fhstatus.fhs_status = errnoGet();
		return (&fhstatus);
	}
	if (dev_hdrp->drvNum == rt11FsDrvNum) {
		fhstatus.fhs_status = NFS_OK;
		vxfhp = (vxfh_t *) & (fhstatus.fhstatus_u.fhs_fhandle);
		vxfhp->type = rt11FsDrvNum;
		strcpy(vxfhp->opaque, (char *) (*dirpathp));

		if (nfsd_debug_on)
			d(vxfhp, sizeof(*vxfhp));

		return (&fhstatus);
	}
	if (dev_hdrp->drvNum == dosFsDrvNum) {
		vxfhp = (vxfh_t *) & (fhstatus.fhstatus_u.fhs_fhandle);

		if (newfh(vxfhp, *dirpathp) == ERROR) {
			fhstatus.fhs_status = NFSERR_NODEV;
			return (&fhstatus);
		}
		fhstatus.fhs_status = NFS_OK;
		return (&fhstatus);
	}
	fhstatus.fhs_status = NFSERR_NODEV;
	return (&fhstatus);
}

mountlist      *
mountproc_dump_1()
{
	static mountlist mountlist;

	nfsd_debug("mount dump\n");
	bzero(&mountlist, sizeof(mountlist));

	return (&mountlist);
}

void           *
mountproc_umnt_1()
{
	static char     dummy = (char) 0;

	nfsd_debug("mount umnt\n");
	return ((void *) &dummy);
}

void           *
mountproc_umntall_1()
{
	static char     dummy = (char) 0;

	nfsd_debug("mount umnt all\n");
	return ((void *) &dummy);
}

exports        *
mountproc_export_1()
{
	static exports  exval;
	static struct exportnode exnode[MAX_EXPORTS];
	int             ix;

	nfsd_debug("mount export\n");
	bzero(&exnode[0], sizeof(exnode));

	for (exval = &exnode[0], ix = 0; nfsd_exportfs[ix]; ix++, exval++) {
		exval->ex_dir = nfsd_exportfs[ix];
		exval->ex_next = exval + 1;
	}

	exval--;
	exval->ex_next = 0;

	exval = &exnode[0];
	return (&exval);
}

exports        *
mountproc_exportall_1()
{
	return (mountproc_export_1());
}

void
nfsd_debug(char *fmt,...)
{
	va_list         vap;

	if (!nfsd_debug_on)
		return;

	va_start(vap, fmt);
	vfprintf(stderr, fmt, vap);
	va_end(vap);
}

void
nfsd_debugno()
{
	if (!nfsd_debug_on)
		return;

	printErrno(errnoGet());
}
