#if defined(SQLITE_OS_OTHER)

//#include <errno.h>
//#include <stdio.h>
//#include <string.h>
#include <sys/stat.h>
#include <time.h>
//#include <unistd.h>
//#include <assert.h>
#include <ff_fs.h>
#include "sqlite3.h"
//#include <kutil.h>

#define TW_SQLITE_LOG(fmt, args ...)			printk(				"## %5d, %-30s ##, " fmt, __LINE__, __func__, ## args)
#define TW_SQLITE_LOG_RED(fmt, args ...)		printk_co(RED,		"## %5d, %-30s ##, " fmt, __LINE__, __func__, ## args)
#define TW_SQLITE_LOG_GREEN(fmt, args ...)		printk_co(GREEN,	"## %5d, %-30s ##, " fmt, __LINE__, __func__, ## args)
#define TW_SQLITE_UNDEF(fmt, args ...)

#define TW_SQLITE_DEBUG
#ifdef TW_SQLITE_DEBUG
#define _LOG			TW_SQLITE_LOG
#else
#define _LOG			TW_SQLITE_UNDEF
#endif
#define _LOG_WARN		TW_SQLITE_LOG_GREEN
#define _LOG_ERROR		TW_SQLITE_LOG_RED

#define MAX_PATHNAME 512


// declare static functions for sqlite3_io_methods structure
static int itron_close(sqlite3_file*);
static int itron_read(sqlite3_file*, void*, int iAmt, sqlite_int64 iOfst);
static int itron_write(sqlite3_file*, const void*, int iAmt, sqlite_int64 iOfst);
static int itron_truncate(sqlite3_file*, sqlite_int64 size);
static int itron_sync(sqlite3_file*, int flags);
static int itron_file_size(sqlite3_file*, sqlite_int64* pSize);
static int itron_lock(sqlite3_file*, int);
static int itron_unlock(sqlite3_file*, int);
static int itron_check_reserved_lock(sqlite3_file*, int* pResOut);
static int itron_file_control(sqlite3_file*, int op, void* pArg);
static int itron_sector_size(sqlite3_file*);
static int itron_device_characteristics(sqlite3_file*);


// declare static functions for sqlite3_vfs structure.
static int itron_open(sqlite3_vfs*, const char* zName, sqlite3_file*, int flags, int* pOutFlags);
static int itron_delete(sqlite3_vfs*, const char* zName, int syncDir);
static int itron_access(sqlite3_vfs*, const char* zName, int flags, int* pResOut);
static int itron_full_pathname(sqlite3_vfs*, const char* zName, int nOut, char* zOut);
static void* itron_dl_open(sqlite3_vfs*, const char* zFilename);
static void itron_dl_error(sqlite3_vfs*, int nByte, char* zErrMsg);
static void (*itron_dl_sym(sqlite3_vfs*, void*, const char* zSymbol))(void);
static void itron_dl_close(sqlite3_vfs*, void*);
static int itron_randomness(sqlite3_vfs*, int nByte, char* zOut);
static int itron_sleep(sqlite3_vfs*, int microseconds);
// comment-out because of needless
//static int itron_current_time(sqlite3_vfs*, double*);
//static int itron_get_last_error(sqlite3_vfs*, int, char *);
static int itron_current_time_int64(sqlite3_vfs*, sqlite3_int64*);
static int utf2uni(const char *utfstr, u16 *unistr, int unistr_len);


// declare extern functions
extern void * tw_MemAlloc (int size);
extern void tw_MemFree (void *mem);
extern int ascii2uni(const char *ascstr, u16 *unistr,  int len);


typedef unsigned char u8;
typedef unsigned short u16;

typedef struct itron_file_tag {
    sqlite3_file sfile;
    void* fp;
} itron_file_t;

static const sqlite3_io_methods s_itron_ios = {
    1,
    itron_close,
    itron_read,
    itron_write,
    itron_truncate,
    itron_sync,
    itron_file_size,
    itron_lock,
    itron_unlock,
    itron_check_reserved_lock,
    itron_file_control,
    itron_sector_size,
    itron_device_characteristics,
};

static sqlite3_vfs s_itron_vfs = {
    2,
    sizeof(itron_file_t),
    256,
    NULL,
    "itron_fs",
    NULL,
    itron_open,
    itron_delete,
    itron_access,
    itron_full_pathname,
    NULL, //itron_dl_open,
    NULL, //itron_dl_error,
    NULL, //itron_dl_sym,
    NULL, //itron_dl_close,
    itron_randomness,
    itron_sleep,
    NULL,
    NULL,
    itron_current_time_int64,
};


int sqlite3_os_init(void)
{	
	_LOG("");
    sqlite3_vfs_register(&s_itron_vfs, 1);
    
    return SQLITE_OK;
}

int sqlite3_os_end(void)
{
	_LOG("");
    return SQLITE_OK;
}


static int itron_close(sqlite3_file* pfile)
{
    itron_file_t* p = (itron_file_t*)pfile;

	_LOG("p->fp : %p", p->fp);
    
    if (ff_fclose(p->fp) != 0)
        return SQLITE_IOERR_CLOSE;
    else
        return SQLITE_OK;
}

static int itron_read(sqlite3_file* pfile, void* buf, int iAmt, sqlite_int64 iOfst)
{
    itron_file_t* p = (itron_file_t*)pfile;
    int ret;	

	//_LOG("p->fp : %p, buf : %p, iAmt : %d, iOfst : %lld", p->fp, buf, iAmt, iOfst);	
   
    if (ff_fseek(p->fp, iOfst, SEEK_SET) != 0) {
		_LOG("SQLITE_IOERR_SEEK");
        return SQLITE_IOERR_SEEK;
    }

	ret = ff_fread(buf, 1, iAmt, p->fp);
	//_LOG("readByte : %d", ret);
	
    if (ret < 0) {
        return SQLITE_IOERR_READ;
    } else if (ret == iAmt) {
    	return SQLITE_OK;        
    } else {
        memset(buf, 0, iAmt);
        return SQLITE_IOERR_SHORT_READ;
    }	
    
}

static int itron_write(sqlite3_file* pfile, const void* buf, int iAmt, sqlite_int64 iOfst)
{
    itron_file_t* p = (itron_file_t*)pfile;
	int ret;	

	//_LOG("p->fp : %p, buf : %p, iAmt : %d, iOfst : %lld", p->fp, buf, iAmt, iOfst);
   
    if (ff_fseek(p->fp, iOfst, SEEK_SET) != 0) {
		_LOG_ERROR("ff_fseek error");
        return SQLITE_IOERR_WRITE;	
    }

	ret = ff_fwrite(buf, 1, iAmt, p->fp);
	//_LOG("writeByte : %d", ret);
    if (ret < 1) {		
        return SQLITE_IOERR_WRITE;
    }
	
    return SQLITE_OK;
}

static int itron_truncate(sqlite3_file* pfile, sqlite_int64 size)
{
    // TODO 
	_LOG("");

    return SQLITE_OK;
}

static int itron_sync(sqlite3_file* pfile, int flags)
{
    itron_file_t* p = (itron_file_t*)pfile;
   
    if (fflush(p->fp) != 0)
        return SQLITE_IOERR_FSYNC;

	_LOG("fflush p->fp : %p", p->fp);	
    return SQLITE_OK;
}

static int itron_file_size(sqlite3_file* pfile, sqlite_int64* pSize)
{
    itron_file_t* p = (itron_file_t*)pfile;
    int ret;	
   
    if (ff_fseek(p->fp, 0, SEEK_END) != 0)
        return SQLITE_IOERR_FSTAT;

    ret = ff_ftell(p->fp);
    if (ret == -1)
        return SQLITE_IOERR_FSTAT;

    *pSize = ret;

	//_LOG("ff_ftell : %d", *pSize);
    return SQLITE_OK;
}

static int itron_lock(sqlite3_file* pfile, int lock)
{
    /** do nothing **/
    return SQLITE_OK;
}

static int itron_unlock(sqlite3_file* pfile, int lock)
{
    /** do nothing **/
    return SQLITE_OK;
}

static int itron_check_reserved_lock(sqlite3_file* pfile, int* pResOut)
{
	_LOG("");
	
    *pResOut = 0;
    return SQLITE_OK;
}

static int itron_file_control(sqlite3_file* pfile, int op, void* pArg)
{

	_LOG("");

    // TODO: do nothing?
    return SQLITE_OK;
}

static int itron_sector_size(sqlite3_file* pfile)
{

	_LOG("");

    // FIXME: maybe
    return 512;
}

static int itron_device_characteristics(sqlite3_file* pfile)
{

	_LOG("");

    // FIXME: maybe
    return 0;
}

static int itron_open(sqlite3_vfs* vfs, const char* zName, sqlite3_file* pfile, int flags, int* pOutFlags)
{
    itron_file_t* p = (itron_file_t*)pfile;	
	u16 uniName[(MAX_PATHNAME + 1)] = {0,};
	u16 uniMode[10] = {0,};
    memset(p, 0, sizeof(itron_file_t));

	_LOG("fileName = %s",zName);	
	
	ascii2uni(zName, uniName, strlen(zName));

	/*
    uniName = tw_MemAlloc(uniLength);
	if (utf2uni(zName, uniName, uniLength) < 0) {
		tw_MemFree(uniName);
		_LOG("SQLITE_ERROR");
		return SQLITE_ERROR;
	}*/

	// FIXME: more more flags study
    if (flags & SQLITE_OPEN_READONLY) {
		_LOG("read only mode");
		ascii2uni("r", uniMode, strlen("r"));	
    }
	if (flags & SQLITE_OPEN_READWRITE) {
		_LOG("read write mode");
		ascii2uni("r+", uniMode, strlen("r+"));		
	}
	if (flags & SQLITE_OPEN_CREATE) {
		_LOG("create mode");
	}	

   	p->fp = ff_fopen(uniName, uniMode);	
	_LOG("open p->fp = %p", p->fp);
	
    if (p->fp == NULL && (flags & SQLITE_OPEN_CREATE)) {
		ascii2uni("w+", uniMode, strlen("w+"));
        p->fp = ff_fopen(uniName, uniMode);
		_LOG("create p->fp = %p", p->fp);
    }

    if (p->fp == NULL) {
		_LOG("SQLITE_CANTOPEN");
        return SQLITE_CANTOPEN;
    }

    p->sfile.pMethods = &s_itron_ios;

    return SQLITE_OK;
}

static int itron_delete(sqlite3_vfs* vfs, const char* zName, int syncDir)
{
	u16 uniName[(MAX_PATHNAME + 1)] = {0,};	   
		
	ascii2uni(zName, uniName, strlen(zName));	
    ff_remove(uniName);
	_LOG("ff_remove : %s", zName);

    return SQLITE_OK;
}

static int itron_access(sqlite3_vfs* vfs, const char* zName, int flags, int* pResOut)
{    
    struct stat statbuf;	

    *pResOut = ff_fstat(zName, &statbuf) == 0;
	//_LOG("ff_fstat : %d", *pResOut);

    return SQLITE_OK;
}

static int itron_full_pathname(sqlite3_vfs* vfs, const char* zName, int nOut, char* zOut)
{	
	zOut[nOut-1] = '\0';
  	sqlite3_snprintf(nOut, zOut, "%s", zName);

	_LOG("zOut : %s", zOut);

    return SQLITE_OK;
}

static int itron_randomness(sqlite3_vfs* vfs, int nByte, char* zOut)
{
    // TODO

	_LOG("");

    return SQLITE_OK;
}

static void* itron_dl_open(sqlite3_vfs* vfs, const char* zPath)
{
    // TODO
    return NULL;
}

static void itron_dl_error(sqlite3_vfs* vfs, int nByte, char* zErrMsg)
{
    // TODO
    return;
}

static void (*itron_dl_sym(sqlite3_vfs* vfs, void* what, const char* zSymbol))(void)
{
    // TODO
    return 0;
}

static void itron_dl_close(sqlite3_vfs* vfs, void* pHandle)
{
    // TODO
    return;
}

static int itron_sleep(sqlite3_vfs* vfs, int nMicro)
{

	_LOG("");

    return 0;
}

static int itron_current_time_int64(sqlite3_vfs* vfs, sqlite3_int64* pTime)
{
    static const sqlite3_int64 unixEpoch = 24405875*(sqlite3_int64)8640000;
    time_t t;


	_LOG("");

    time(&t);
    *pTime = ((sqlite3_int64)t)*1000 + unixEpoch;
 
    return SQLITE_OK;
}

static void *memAlloc (int size)
{
	int *pMem = (int*) tw_MemAlloc(size + 4); // 4 is int has a size value

	if (pMem == NULL)
		return NULL;

	*pMem = size;

	return (void*)(pMem + 1);
}
	
static void memFree (void *mem)
{
	if (mem != NULL)
		tw_MemFree((int*)mem - 1);
}

static void *memRealloc(void *pPrior, int nByte)
{
	void *pDest = NULL;
	int priorSize;
	
	if (pPrior != NULL && nByte == 0) {
		memFree(pPrior);
	} else if (pPrior == NULL && nByte != 0) {
		pDest = memAlloc(nByte);
	} else {
		pDest = memAlloc(nByte);
		if (pDest != NULL) {
			priorSize = memSize(pPrior);

			if (priorSize < nByte)
				nByte = priorSize;
			
			memcpy(pDest, pPrior, nByte);
			memFree(pPrior);
		}
	}

	return pDest; 
}

static int memSize(void *pPrior)
{ 
	if (pPrior != NULL)
		return *((int*)pPrior - 1);

	return 0;
}

static int memRoundup(int n)
{ 
	return n; 
}
static int memInit(void *NotUsed)
{ 

	_LOG("");

	return SQLITE_OK; 
}

static void memShutdown(void *NotUsed)
{ 

	_LOG("");

	return ; 
}

static void twMemSetDefault(void)
{
  	static const sqlite3_mem_methods twMemMethods = {
		memAlloc,
		memFree,
		memRealloc,
		memSize,
		memRoundup,
		memInit,
		memShutdown,
		NULL
  	};

	_LOG("");		
	sqlite3_config(SQLITE_CONFIG_MALLOC, &twMemMethods);	
}


int twSqlite3Initialize(void)
{

	_LOG("");

	twMemSetDefault();	

	return sqlite3_initialize();
}

/**
 * Convert UTF8 encoded string to unicode string.
 
 */
static int utf2uni(const char *utfstr, u16 *unistr, int unistr_len)
{
	const u8* z = (u8*) utfstr;
	u16 *t = unistr;
	int remain = unistr_len - 1;  // 1 for terminated-null char

	while (*z && remain > 0) { // null check
		if (*z < 0x80) { // 1 byte
			*t++ = (u16) *z;
			z++;
		} else if ((*z & 0xE0) ==0xC0) { // 2 bytes
			if ((*(z+1) & 0xC0) == 0x80) {
				*t++ = ((((u16)(*z & 0x1F))<<6)|(*(z + 1)&0x3F));
				z+=2;
			} else {
				// bad chacter case
				return -2;
			}
		} else if ((*z&0xF0)==0xE0) {	// 3 bytes
			if ((*(z+1)&0xC0)==0x80 && (*(z+2)&0xC0)==0x80)
			{
				*t++ = ((((u16)(*z&0x0F))<<12)|((*(z+1)&0x3F)<<6)|(*(z+2)&0x3F));
				z+=3;
			} else {
				// bad chacter case
				return -3;
			}
		}
		else if ((*z&0xF8)==0xF0) {		// 4 bytes
			if ((*(z+1)&0xC0)==0x80 && (*(z+2)&0xC0)==0x80 && (*(z+3)&0xC0)==0x80){
				*t++ = ((((u16)(*z&0x0F))<<18)|((*(z+1)&0x3F)<<12)||((*(z+2)&0x3F)<<6)|(*(z+3)&0x3F));
				z+=4;
			} else {
				// bad chacter case
				return -4;
			}
		} else {
			// bad chacter case
			return -5;
		}

		remain--;
	}

	// check wether unistr is too short
	if (*z)
		return -1;

	*t = 0;

	return (t - unistr) / sizeof(u16);
}


#endif // SQLITE_OS_OTHER

