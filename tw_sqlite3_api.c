/**
 * @file system/src/app3/tw_utils/tw_sqlite3_api.c
 *
 * @brief Implementation of tw_sqlite3_api
 *
 * @author 2015/02/25 - [] created file
 */


#include <app3/apps/apps.h>
#include <tw_log.h>
#include <app3/tw_utils/tw_utils.h>
#include <peripheral/rtc.h>

#include "tw_sqlite3_api.h"

#define TW_SQLITE3_API_DEBUG
#ifdef TW_SQLITE3_API_DEBUG
#define _LOG			TW_FLOW_LOG
#else
#define _LOG			TW_FLOW_LOG_UNDEF
#endif
#define _LOG_WARN		TW_FLOW_LOG_GREEN
#define _LOG_ERROR		TW_FLOW_LOG_RED


#define NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE \
	    "etc01 != '3' AND etc01 !='4' AND etc01 != '5' AND etc01 != '6'  AND etc01 != '7'  AND etc01 != '8'  AND etc01 != '9'"

unsigned char g_header_version[] = {0x01, 0x00, 0x00, 0x00};


////////////////////////////////////////////////////////////////////////////////
// extern variables
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// extern functions
////////////////////////////////////////////////////////////////////////////////
extern int twSqlite3Initialize(void);


////////////////////////////////////////////////////////////////////////////////
// static variables
////////////////////////////////////////////////////////////////////////////////
static obd_vehicle_t g_obd_vehicle;
static mbp_file_t g_mbp_file;
static frm_file_t g_frm_file;
static bcm_file_t g_bcm_file;
static sqlite3_list_manager_t g_sqlite3_list_manager[SQLITE3_ASYNC_MAX];
static int g_sqlite3_async_type;
static stats_search_t g_stats_search;
static stats_data_t g_stats_data;
static int g_stats_count;



////////////////////////////////////////////////////////////////////////////////
// static functions
////////////////////////////////////////////////////////////////////////////////
static sqlite3_list_item_t * twSqlite3ListCreateItem(u16 * unicode_buf, u8 * asc_buf);
static s32 twSqlite3ListAddItem(sqlite3_list_manager_t * manager, sqlite3_list_item_t * item);
static int twSqlite3Cb(void *NotUsed, int argc, char **argv, char **azColName);
static int twSqlite3CbStats(void *NotUsed, int argc, char **argv, char **azColName);
static int twSqlite3CbCarinfo(void *NotUsed, int argc, char **argv, char **azColName);
static int twSqlite3CbDecision(void *NotUsed, int argc, char **argv, char **azColName);

static int twSqlite3ReadBlob(
   sqlite3 *db,               /* Database containing blobs table */
   const char *zSql,          /* Sql statement */
   const char *zKey,          /* Null-terminated key to retrieve blob for */
   unsigned char **pzBlob,    /* Set *pzBlob to point to the retrieved blob */
   int *pnBlob                /* Set *pnBlob to the size of the retrieved blob */
 );

static void twSqlite3FreeBlob(unsigned char *zBlob);
static void twSqlite3GetEncryptComm(int ecu_index);
static void twSqlite3GetEncryptSpec(void);
static void twSqlite3GetFRMapC(void);
static void twSqlite3GetBCM(void);
static void twSqlite3SetECU(const char * str);
static void twSqlite3SetTCU(const char * str);
static void twSqlite3SetFRMapC(const char * str);
static void twSqlite3MakeMbpFileHeader(FF_FILE *fp);
static void twSqlite3MakeMbpFileData(FF_FILE *fp, int ecu_index);
static void twSqlite3MakeFrmFileHeader(FF_FILE *fp);
static void twSqlite3MakeFrmFileData(FF_FILE *fp);
static void twSqlite3MakeBcmFileHeader(FF_FILE *fp);
static void twSqlite3MakeBcmFileData(FF_FILE *fp);
static void twSqlite3MakeMbpFile(void);
static void twSqlite3MakeFrmFile(void);
static void twSqlite3MakeBcmFile(void);
static void twSqlite3SetVehicleSpec(const char * colName, const char * colVal);
static void twSqlite3CreateTripTable(void);
static void twSqlite3SelectMaker(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectType01(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectReleased(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectFuel(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectDisplacement(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectFE(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectCapacity(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectLengthW(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectModel(int (*callback)(void *, int, char**, char**));
static void twSqlite3SelectDecision(int (*callback)(void *, int, char**, char**));
static void twSqlite3ResetStats(void);
static void twSqlite3SelectMonthStats(struct tm * search_tm);




//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ListCreateItem()
// Description    :
//---------------------------------------------------------------------------------
static sqlite3_list_item_t * 
twSqlite3ListCreateItem(u16 * unicode_buf, u8 * asc_buf)
{	
	sqlite3_list_item_t * new_item = NULL;	
	new_item = (sqlite3_list_item_t *)tw_MemAlloc(sizeof(sqlite3_list_item_t));
	memset((void *)new_item, 0x00, sizeof(sqlite3_list_item_t));
	if(unicode_buf != NULL) {			
		memcpy(new_item->sqlite3_uni, unicode_buf, uniStrLen(unicode_buf) * sizeof(u16));		
	}	

	if(asc_buf != NULL) {			
		memcpy(new_item->sqlite3_asc, asc_buf, strlen((char *)asc_buf));		
	}
	
	return new_item;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ListAddItem()
// Description    :
//---------------------------------------------------------------------------------
static s32
twSqlite3ListAddItem(sqlite3_list_manager_t * manager, sqlite3_list_item_t * item)
{
	sqlite3_list_node_t	* new_node;

	if(manager == NULL) {
		_LOG_ERROR("manager is NULL");
		return -1;
	}
	
	if (manager->head.next == &manager->tail) {
		// there is no item in this manager
		new_node = (sqlite3_list_node_t *)tw_MemAlloc(sizeof(sqlite3_list_node_t));
		if (new_node == NULL) {
			_LOG_ERROR("can not allocate memory, tw_MemAlloc(%d)", sizeof(sqlite3_list_node_t));
			return -1;
		}
		new_node->node_item = item;
		new_node->next = &manager->tail;		
		manager->head.next = new_node;

		manager->node_numbers++;
		//_LOG_WARN("first inserted, sqlite3_asc: %s", item->sqlite3_asc);
	} else {
		// exist items at least 1 in this manager
		sqlite3_list_node_t * curr, * new_node;
		curr = manager->head.next;
		// go to tail
		while (curr->next != &manager->tail) {
			//TL_LOG_GREEN("existed, str_id : %d", curr->node_item->str_id);
			curr = curr->next;
		}

		// add to tail
		new_node = (sqlite3_list_node_t *)tw_MemAlloc(sizeof(sqlite3_list_node_t));
		if (new_node == NULL) {
			_LOG_ERROR("can not allocate memory, tw_MemAlloc(%d)", sizeof(sqlite3_list_node_t));
			return -1;
		}
		new_node->node_item = item;
		new_node->next = &manager->tail;
		curr->next = new_node;	

		manager->node_numbers++;
		//_LOG_WARN("another inserted, sqlite3_asc : %s", item->sqlite3_asc);
	}

	return 0;

}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3Cb()
// Description    :
//---------------------------------------------------------------------------------
static int 
twSqlite3Cb(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;
	for(i = 0 ; i < argc ; i++) {
		_LOG_WARN("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
	}
	
	return 0;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3CbStats()
// Description    :
//---------------------------------------------------------------------------------
static int 
twSqlite3CbStats(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i, seconds;	
	int tripCount = 0, tripDistance = 0, tripRuntime = 0, tripGasBill = 0;
	float tripGasMileage = 0, tripGasConsump = 0;
	struct tm *stats_tm = NULL, *min_tm = NULL, *max_tm = NULL;
	
	for(i = 0 ; i < argc ; i++) {
		//_LOG_WARN("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
		if(strcmp(azColName[i], "SECONDS") == 0) {
			seconds = atoi(argv[i]);
			stats_tm = tm_localtime((time_t *)&seconds);	
			//_LOG("============== stats day ==============");
			_LOG("%d.%02d.%02d %02d:%02d:%02d", 
				stats_tm->tm_year,
				stats_tm->tm_mon+1,
				stats_tm->tm_mday,
				stats_tm->tm_hour,
				stats_tm->tm_min,
				stats_tm->tm_sec);	
			
			tripCount = 1;
		} else if(strcmp(azColName[i], "DISTANCE") == 0) {
			tripDistance = atoi(argv[i]);			
		} else if(strcmp(azColName[i], "GAS_MILEAGE") == 0) {
			tripGasMileage = atof(argv[i]);
		} else if(strcmp(azColName[i], "GAS_CONSUMP") == 0) {
			tripGasConsump = atof(argv[i]);		
		} else if(strcmp(azColName[i], "RUN_TIME") == 0) {
			tripRuntime = atoi(argv[i]);
		} else if(strcmp(azColName[i], "GAS_BILL") == 0) {
			tripGasBill = atoi(argv[i]);
		} else if(strcmp(azColName[i], "MIN(SECONDS)") == 0) {
			g_stats_search.min_seconds = atoi(argv[i]);			
			min_tm = tm_localtime((time_t *)&g_stats_search.min_seconds);
			min_tm->tm_hour = 0;
			min_tm->tm_min = 0;
			min_tm->tm_sec = 0;
			g_stats_search.min_seconds = tm_mktime(min_tm);
			memcpy(&g_stats_search.min_tm, min_tm, sizeof(struct tm));
			_LOG("minDaySeconds : %d", g_stats_search.min_seconds);
			_LOG("============== min day ==============");
			_LOG("%d.%d.%d %d:%d:%d", 
				min_tm->tm_year,
				min_tm->tm_mon+1,
				min_tm->tm_mday,
				min_tm->tm_hour,
				min_tm->tm_min,
				min_tm->tm_sec);	
		} else if(strcmp(azColName[i], "MAX(SECONDS)") == 0) {
			g_stats_search.max_seconds = atoi(argv[i]);			
			max_tm = tm_localtime((time_t *)&g_stats_search.max_seconds);
			max_tm->tm_hour = 0;
			max_tm->tm_min = 0;
			max_tm->tm_sec = 0;
			g_stats_search.max_seconds = tm_mktime(max_tm);
			memcpy(&g_stats_search.max_tm, max_tm, sizeof(struct tm));
			_LOG("maxDaySeconds : %d", g_stats_search.max_seconds);
			_LOG("============== max day ==============");
			_LOG("%d.%d.%d %d:%d:%d", 
				max_tm->tm_year,
				max_tm->tm_mon+1,
				max_tm->tm_mday,
				max_tm->tm_hour,
				max_tm->tm_min,
				max_tm->tm_sec);
		} else if(strcmp(azColName[i], "MAX(ID)") == 0) {
			g_stats_count = atoi(argv[i]);
			_LOG("max id : %d", g_stats_count);
		}
		
	}	

	if (tripCount > 0) {
		if (tripDistance > MIN_STATS_METER) {
			g_stats_data.trip_count += tripCount;
			g_stats_data.distance += tripDistance;
			//g_stats_data.gas_mileage += (tripGasMileage * tripRuntime);	
			g_stats_data.gas_consump += tripGasConsump;
			g_stats_data.runtime += tripRuntime;
			g_stats_data.gas_bill += tripGasBill;
			
			_LOG("[include] seconds : %d, tripDistance : %d, tripGasConsump : %f, tripRuntime : %d, tripGasBill : %d",
			seconds,
			tripDistance,
			tripGasConsump,
			tripRuntime,
			tripGasBill);
		} else {
			_LOG("[exclude] seconds : %d, tripDistance : %d, tripGasConsump : %f, tripRuntime : %d, tripGasBill : %d",
			seconds,
			tripDistance,
			tripGasConsump,
			tripRuntime,
			tripGasBill);
		}						

		/*_LOG("g_stats_data.trip_count : %d", g_stats_data.trip_count);
		_LOG("g_stats_data.distance : %d", g_stats_data.distance);
		_LOG("g_stats_data.gas_mileage : %f", g_stats_data.gas_mileage);
		_LOG("g_stats_data.runtime : %d", g_stats_data.runtime);
		_LOG("g_stats_data.gas_bill : %d", g_stats_data.gas_bill);*/	
	}	
	
	return 0;
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3CbCarinfo()
// Description    :
//---------------------------------------------------------------------------------
static int 
twSqlite3CbCarinfo(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;
	u16 unicode_buf[SQLITE_QUERY_RESULT_BUF] = {0,};
	sqlite3_list_item_t * item = NULL;	
	sqlite3_list_manager_t * manager = NULL;
	
	for(i = 0 ; i < argc ; i++) {
		_LOG_WARN("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
		if (argv[i] == NULL)
			return 0;

		utf8ToUni((const char *)argv[i], unicode_buf, sizeof(unicode_buf));			
		item = twSqlite3ListCreateItem(unicode_buf, (u8*)argv[i]);
		manager = twSqlite3ListGetManager(g_sqlite3_async_type);
		twSqlite3ListAddItem(manager, item);		
	}
	
	return 0;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3CbDecision()
// Description    :
//---------------------------------------------------------------------------------
static int 
twSqlite3CbDecision(void *NotUsed, int argc, char **argv, char **azColName)
{
	int i;	
	
	for(i = 0 ; i < argc ; i++) {
		_LOG_WARN("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
		if(argv[i] != NULL) {
			twSqlite3SetVehicleSpec(azColName[i], argv[i]);
		}		
	}	
	
	return 0;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ReadBlob()
// Description    : Read a blob from database db. Return an SQLite error code.
//---------------------------------------------------------------------------------
static int 
twSqlite3ReadBlob(
   sqlite3 *db,               /* Database containing blobs table */
   const char *zSql,          /* Sql statement */
   const char *zKey,          /* Null-terminated key to retrieve blob for */
   unsigned char **pzBlob,    /* Set *pzBlob to point to the retrieved blob */
   int *pnBlob                /* Set *pnBlob to the size of the retrieved blob */
 ){   
   sqlite3_stmt *pStmt;
   int rc;

   /* In case there is no table entry for key zKey or an error occurs, 
   ** set *pzBlob and *pnBlob to 0 now.
   */
   *pzBlob = 0;
   *pnBlob = 0;

   do {
     /* Compile the SELECT statement into a virtual machine. */
     rc = sqlite3_prepare(db, zSql, -1, &pStmt, 0);
     if( rc!=SQLITE_OK ){
       return rc;
     }

     /* Bind the key to the SQL variable. */
     sqlite3_bind_text(pStmt, 1, zKey, -1, SQLITE_STATIC);          

     /* Run the virtual machine. We can tell by the SQL statement that
     ** at most 1 row will be returned. So call sqlite3_step() once
     ** only. Normally, we would keep calling sqlite3_step until it
     ** returned something other than SQLITE_ROW.
     */
     rc = sqlite3_step(pStmt);
     if( rc==SQLITE_ROW ){
       /* The pointer returned by sqlite3_column_blob() points to memory
       ** that is owned by the statement handle (pStmt). It is only good
       ** until the next call to an sqlite3_XXX() function (e.g. the 
       ** sqlite3_finalize() below) that involves the statement handle. 
       ** So we need to make a copy of the blob into memory obtained from 
       ** malloc() to return to the caller.
       */
       *pnBlob = sqlite3_column_bytes(pStmt, 0);	   
	   *pzBlob = (unsigned char *)tw_MemAlloc(*pnBlob);
       memcpy(*pzBlob, sqlite3_column_blob(pStmt, 0), *pnBlob);	   
     }

     /* Finalize the statement (this releases resources allocated by 
     ** sqlite3_prepare() ).
     */
     rc = sqlite3_finalize(pStmt);	 

     /* If sqlite3_finalize() returned SQLITE_SCHEMA, then try to execute
     ** the statement all over again.
     */
   } while( rc==SQLITE_SCHEMA );

   return rc;
 }

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3FreeBlob()
// Description    : Free a blob read by twSqlite3ReadBlob().
//---------------------------------------------------------------------------------
static void 
twSqlite3FreeBlob(unsigned char *zBlob){
   tw_MemFree(zBlob);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetEncryptComm()
// Description    :
//---------------------------------------------------------------------------------
static void
twSqlite3GetEncryptComm(int ecu_index) 
{	
	sqlite3 *db;
	int i, rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	int nBlob = 0;
	unsigned char *zBlob = NULL;
		
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT EncryptionData FROM ECU WHERE ECU = ?");					
	_LOG_WARN("sql : %s", sql);
	
	if( SQLITE_OK != twSqlite3ReadBlob(
						db, 
						sql,
						g_obd_vehicle.ecu[ecu_index],
						&zBlob, 
						&nBlob) ) {
		_LOG_ERROR("read blob error : %s\n", sqlite3_errmsg(db));
		goto db_close;		
	}
	
	g_mbp_file.data.encrypt_vehicle_comm = zBlob;
	g_mbp_file.data.encrypt_comm_size = nBlob;
	_LOG_WARN("encrypt_comm_size : %d", nBlob);
	for(i = 0 ; i < nBlob ; i++) {
		//_LOG_WARN("vehicle_spec[%d] : 0x%X", i, g_mbp_file.data.encrypt_vehicle_comm[i]);
	}	
	_LOG_WARN("Select done successfully\n");
db_close:
	sqlite3_close(db);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetEncryptSpec()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3GetEncryptSpec(void) 
{	
	sqlite3 *db;
	int i, rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	char code_buf[MAX_CODE_LEN+1] = {0,};
	int nBlob = 0;
	unsigned char *zBlob = NULL;
		
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT EncryptionData FROM Vehicle01 WHERE Code = ?");					
	_LOG_WARN("sql : %s", sql);
	memcpy(code_buf, g_mbp_file.data.vehicle_spec.code, sizeof(g_mbp_file.data.vehicle_spec.code));
	_LOG_WARN("code_buf : %s", code_buf);
	
	if( SQLITE_OK != twSqlite3ReadBlob(
						db, 
						sql,
						code_buf,
						&zBlob, 
						&nBlob) ) {
		_LOG_ERROR("read blob error : %s\n", sqlite3_errmsg(db));
		goto db_close;		
	}
	
	g_mbp_file.data.encrypt_vehicle_spec = zBlob;
	g_mbp_file.data.encrypt_spec_size = nBlob;
	_LOG_WARN("encrypt_spec_size : %d", nBlob);
	for(i = 0 ; i < nBlob ; i++) {
		//_LOG_WARN("vehicle_spec[%d] : 0x%X", i, g_mbp_file.data.encrypt_vehicle_spec[i]);
	}	
	_LOG_WARN("Select done successfully\n");

db_close:
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetFRMapC()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3GetFRMapC(void) 
{	
	sqlite3 *db;
	int i, rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	int nBlob = 0;
	unsigned char *zBlob = NULL;
		
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT EncryptionData FROM FRMap WHERE FRMap = ?");					
	_LOG_WARN("sql : %s", sql);
	
	if( SQLITE_OK != twSqlite3ReadBlob(
						db, 
						sql,
						g_obd_vehicle.frmap_c, 
						&zBlob, 
						&nBlob) ) {
		_LOG_ERROR("read blob error : %s\n", sqlite3_errmsg(db));
		goto db_close;		
	}
	
	g_frm_file.data.encrypt = zBlob;
	g_frm_file.data.encrypt_size = nBlob;
	_LOG_WARN("FRMap encrypt_size : %d", nBlob);
	for(i = 0 ; i < nBlob ; i++) {
		//_LOG_WARN("FRMap[%d] : 0x%X", i, g_frm_file.data.encrypt[i]);
	}	
	_LOG_WARN("Select done successfully\n");

db_close:
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetBCM()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3GetBCM(void) 
{	
	sqlite3 *db;
	int i, rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	int nBlob = 0;
	unsigned char *zBlob = NULL;
		
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT EncryptionData FROM BCM WHERE BCM = ?");					
	_LOG_WARN("sql : %s", sql);
	
	if( SQLITE_OK != twSqlite3ReadBlob(
						db, 
						sql,
						g_obd_vehicle.tcu, 						
						&zBlob, 
						&nBlob) ) {
		_LOG_ERROR("read blob error : %s\n", sqlite3_errmsg(db));
		goto db_close;		
	}
	
	g_bcm_file.data.encrypt = zBlob;
	g_bcm_file.data.encrypt_size = nBlob;
	_LOG_WARN("BCM encrypt_size : %d", nBlob);
	for(i = 0 ; i < nBlob ; i++) {
		//_LOG_WARN("BCM[%d] : 0x%X", i, g_bcm_file.data.encrypt[i]);
	}	
	_LOG_WARN("Select done successfully\n");

db_close:
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SetECU()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SetECU(const char * str)
{	
	int index = 0;
	char * token = NULL;
	const char * delimiter = ",";
	
	token = strtok(str, delimiter);

	while(token != NULL) {
		memset(g_obd_vehicle.ecu[index], 0, sizeof(g_obd_vehicle.ecu[index]));
		memcpy(g_obd_vehicle.ecu[index], token, sizeof(g_obd_vehicle.ecu[index]));
		_LOG_WARN("ecu[%d] : %s", index, g_obd_vehicle.ecu[index]);
		
		g_obd_vehicle.num_ecu = index + 1;
		_LOG_WARN("g_obd_vehicle.num_ecu : %d\n", g_obd_vehicle.num_ecu);	
		
		token = strtok(NULL, delimiter);	

		index++;
	}		
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SetTCU()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SetTCU(const char * str)
{	
	memset(g_obd_vehicle.tcu, 0, sizeof(g_obd_vehicle.tcu));
	memcpy(g_obd_vehicle.tcu, str, sizeof(g_obd_vehicle.tcu));	
	_LOG_WARN("g_obd_vehicle.tcu : %s\n", g_obd_vehicle.tcu);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SetFRMapC()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SetFRMapC(const char * str)
{	
	memset(g_obd_vehicle.frmap_c, 0, sizeof(g_obd_vehicle.frmap_c));
	memcpy(g_obd_vehicle.frmap_c, str, sizeof(g_obd_vehicle.frmap_c));
	_LOG_WARN("g_obd_vehicle.frmap_c : %s\n", g_obd_vehicle.frmap_c);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeMbpFileHeader()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeMbpFileHeader(FF_FILE *fp)
{	
	u32 byte_write, i;
	mbp_file_header_t header;	
	unsigned char * checksum_byte;
	unsigned short data_sum = 0;
	unsigned short header_sum = 0;

	//version	
	memcpy(header.version, g_header_version, sizeof(header.version));
	
	// length
	header.length = sizeof(g_mbp_file.data.vehicle_spec) + g_mbp_file.data.encrypt_spec_size + g_mbp_file.data.encrypt_comm_size;	
	
	//datachecksum	
	checksum_byte = (unsigned char *)&g_mbp_file.data.vehicle_spec;
	for(i = 0 ; i < sizeof(g_mbp_file.data.vehicle_spec) ; i++) {
		data_sum += checksum_byte[i];		
	}
	checksum_byte = g_mbp_file.data.encrypt_vehicle_spec;
	for(i = 0 ; i < g_mbp_file.data.encrypt_spec_size ; i++) {
		data_sum += checksum_byte[i];		
	}
	checksum_byte = g_mbp_file.data.encrypt_vehicle_comm;
	for(i = 0 ; i < g_mbp_file.data.encrypt_comm_size ; i++) {
		data_sum += checksum_byte[i];		
	}
	header.datachecksum = data_sum;
	_LOG_WARN("header.datachecksum : 0x%X", header.datachecksum);

	// type
	header.type = MBP_TYPE;

	// headerchecksum
	checksum_byte = header.version;
	for(i = 0 ; i < sizeof(header.version) ; i++) {
		header_sum += checksum_byte[i];
	}
	checksum_byte = (unsigned char *)&header.length;
	for(i = 0 ; i < sizeof(header.length) ; i++) {
		header_sum += checksum_byte[i];
	}
	checksum_byte = (unsigned char *)&header.datachecksum;
	for(i = 0 ; i < sizeof(header.datachecksum) ; i++) {
		header_sum += checksum_byte[i];
	}
	header_sum += header.type;
	
	header.headerchecksum = header_sum;
	_LOG_WARN("header.headerchecksum : 0x%X", header.headerchecksum);
	
	byte_write = ff_fwrite(&header, 1, sizeof(header), fp);
	_LOG_WARN("header byte_write : %d", byte_write);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeMbpFileData()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeMbpFileData(FF_FILE *fp, int ecu_index)
{	
	u32 byte_write;	

	g_mbp_file.data.vehicle_spec.selected_ecu = ecu_index;
	
	byte_write = ff_fwrite(&g_mbp_file.data.vehicle_spec, 1, sizeof(g_mbp_file.data.vehicle_spec), fp);
	_LOG_WARN("vehicle_spec byte_write : %d", byte_write);

	byte_write = ff_fwrite(g_mbp_file.data.encrypt_vehicle_spec, 1, g_mbp_file.data.encrypt_spec_size, fp);
	_LOG_WARN("encrypt_vehicle_spec byte_write : %d", byte_write);
	
	byte_write = ff_fwrite(g_mbp_file.data.encrypt_vehicle_comm, 1, g_mbp_file.data.encrypt_comm_size, fp);
	_LOG_WARN("encrypt_vehicle_comm byte_write : %d", byte_write);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeFrmFileHeader()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeFrmFileHeader(FF_FILE *fp)
{	
	u32 byte_write, i;
	frm_file_header_t header;	
	unsigned char * checksum_byte;
	unsigned short data_sum = 0;
	unsigned short header_sum = 0;

	//version	
	memcpy(header.version, g_header_version, sizeof(header.version));
	
	// length
	header.length = g_frm_file.data.encrypt_size;	
	
	//datachecksum	
	checksum_byte = g_frm_file.data.encrypt;
	for(i = 0 ; i < g_frm_file.data.encrypt_size ; i++) {
		data_sum += checksum_byte[i];
	}
	header.datachecksum = data_sum;
	_LOG_WARN("header.datachecksum : 0x%X", header.datachecksum);

	// type
	header.type = FRM_TYPE;

	// headerchecksum
	checksum_byte = header.version;
	for(i = 0 ; i < sizeof(header.version) ; i++) {
		header_sum += checksum_byte[i];
	}
	checksum_byte = (unsigned char *)&header.length;
	for(i = 0 ; i < sizeof(header.length) ; i++) {
		header_sum += checksum_byte[i];
	}
	checksum_byte = (unsigned char *)&header.datachecksum;
	for(i = 0 ; i < sizeof(header.datachecksum) ; i++) {
		header_sum += checksum_byte[i];
	}
	header_sum += header.type;
	
	header.headerchecksum = header_sum;
	_LOG_WARN("header.headerchecksum : 0x%X", header.headerchecksum);
	
	byte_write = ff_fwrite(&header, 1, sizeof(header), fp);
	_LOG_WARN("header byte_write : %d", byte_write);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeFrmFileData()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeFrmFileData(FF_FILE *fp)
{	
	u32 byte_write;		
	
	byte_write = ff_fwrite(g_frm_file.data.encrypt, 1, g_frm_file.data.encrypt_size, fp);
	_LOG_WARN("encrypt_size : %d", byte_write);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeBcmFileHeader()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeBcmFileHeader(FF_FILE *fp)
{	
	u32 byte_write, i;
	frm_file_header_t header;	
	unsigned char * checksum_byte;
	unsigned short data_sum = 0;
	unsigned short header_sum = 0;

	//version	
	memcpy(header.version, g_header_version, sizeof(header.version));
	
	// length
	header.length = g_bcm_file.data.encrypt_size;	
	
	//datachecksum	
	checksum_byte = g_bcm_file.data.encrypt;
	for(i = 0 ; i < g_bcm_file.data.encrypt_size ; i++) {
		data_sum += checksum_byte[i];
	}
	header.datachecksum = data_sum;
	_LOG_WARN("header.datachecksum : 0x%X", header.datachecksum);

	// type
	header.type = BCM_TYPE;

	// headerchecksum
	checksum_byte = header.version;
	for(i = 0 ; i < sizeof(header.version) ; i++) {
		header_sum += checksum_byte[i];
	}
	checksum_byte = (unsigned char *)&header.length;
	for(i = 0 ; i < sizeof(header.length) ; i++) {
		header_sum += checksum_byte[i];
	}
	checksum_byte = (unsigned char *)&header.datachecksum;
	for(i = 0 ; i < sizeof(header.datachecksum) ; i++) {
		header_sum += checksum_byte[i];
	}
	header_sum += header.type;
	
	header.headerchecksum = header_sum;
	_LOG_WARN("header.headerchecksum : 0x%X", header.headerchecksum);
	
	byte_write = ff_fwrite(&header, 1, sizeof(header), fp);
	_LOG_WARN("header byte_write : %d", byte_write);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeBcmFileData()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeBcmFileData(FF_FILE *fp)
{	
	u32 byte_write;		
	
	byte_write = ff_fwrite(g_bcm_file.data.encrypt, 1, g_bcm_file.data.encrypt_size, fp);
	_LOG_WARN("encrypt_size : %d", byte_write);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeMbpFile()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeMbpFile(void)
{		
	int index;
	FF_FILE *fp;
	char asciiName[MAX_OBD_FILE_NAME];
	u16 uniName[MAX_OBD_FILE_NAME] = {0,};	

	twSqlite3GetEncryptSpec();
	
	for(index = 0 ; index < g_obd_vehicle.num_ecu ; index++) {
		sprintf(asciiName, "%sbp%03d.mbp", OBD_FILE_DIR, index+1);		
		_LOG_WARN("filename : %s\n", asciiName);
		ascii2uni(asciiName, uniName, strlen(asciiName));

		fp = ff_fopen(uniName, "w");
		if(fp == NULL) {
			_LOG_ERROR("Can't create the file %s\n", asciiName);
		} else {	
			twSqlite3GetEncryptComm(index);
			twSqlite3MakeMbpFileHeader(fp);			 
			twSqlite3MakeMbpFileData(fp, index);			
			ff_fclose(fp);	

			// free encrypt comm blob pointer	
			twSqlite3FreeBlob(g_mbp_file.data.encrypt_vehicle_comm);
		}	
	}	
	
	// free encrypt spec blob pointer
	twSqlite3FreeBlob(g_mbp_file.data.encrypt_vehicle_spec);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeFrmFile()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeFrmFile(void)
{	
	FF_FILE *fp;
	char asciiName[MAX_OBD_FILE_NAME];
	u16 uniName[MAX_OBD_FILE_NAME] = {0,};
	const char * fileName = NULL;

	fileName = g_obd_vehicle.frmap_c;	
	if(strlen(fileName) <= 0) {
		_LOG_ERROR("Don't make FRM file");
		return;
	}	
	
	twSqlite3GetFRMapC();
	
	//sprintf(asciiName, "%s%s.frm", OBD_FILE_DIR, fileName);
	sprintf(asciiName, "%s%s.frm", OBD_FILE_DIR, FIXED_TW_FILE_NAME);
	_LOG_WARN("filename : %s\n", asciiName);
	ascii2uni(asciiName, uniName, strlen(asciiName));

	fp = ff_fopen(uniName, "w");
	if(fp == NULL) {
		_LOG_ERROR("Can't create the file %s\n", asciiName);
	} else {		
		twSqlite3MakeFrmFileHeader(fp);			 
		twSqlite3MakeFrmFileData(fp);			
		ff_fclose(fp);			
	}
	
	// free FRMap encrypt blob pointer
	twSqlite3FreeBlob(g_frm_file.data.encrypt);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3MakeBcmFile()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3MakeBcmFile(void)
{		
	FF_FILE *fp;
	char asciiName[MAX_OBD_FILE_NAME];
	u16 uniName[MAX_OBD_FILE_NAME] = {0,};
	const char * fileName = NULL;

	fileName = g_obd_vehicle.tcu;	
	if(strlen(fileName) <= 0) {
		_LOG_ERROR("Don't make BCM file");
		return;
	}	
	
	twSqlite3GetBCM();

	//sprintf(asciiName, "%s%s.bcm", OBD_FILE_DIR, fileName);	
	sprintf(asciiName, "%s%s.bcm", OBD_FILE_DIR, FIXED_TW_FILE_NAME);	
	_LOG_WARN("filename : %s\n", asciiName);
	ascii2uni(asciiName, uniName, strlen(asciiName));

	fp = ff_fopen(uniName, "w");
	if(fp == NULL) {
		_LOG_ERROR("Can't create the file %s\n", asciiName);
	} else {		
		twSqlite3MakeBcmFileHeader(fp);			 
		twSqlite3MakeBcmFileData(fp);			
		ff_fclose(fp);			
	}
	
	// free BCM encrypt blob pointer
	twSqlite3FreeBlob(g_bcm_file.data.encrypt);
}


//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SetVehicleSpec()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SetVehicleSpec(const char * colName, const char * colVal)
{	
	if(strcmp(colName, "Code") == 0) {		
		memset(g_mbp_file.data.vehicle_spec.code, 0x0, sizeof(g_mbp_file.data.vehicle_spec.code));
		memcpy(g_mbp_file.data.vehicle_spec.code, colVal, sizeof(g_mbp_file.data.vehicle_spec.code));
	} else if(strcmp(colName, "Capacity") == 0) {	
		g_mbp_file.data.vehicle_spec.capacity = atoi(colVal);		
	} else if(strcmp(colName, "Cylinder") == 0) {		
		g_mbp_file.data.vehicle_spec.cylinder = atoi(colVal);
	} else if(strcmp(colName, "Displacement") == 0) {		
		g_mbp_file.data.vehicle_spec.displacement = atoi(colVal);		
	} else if(strcmp(colName, "WeightE") == 0) {		
		g_mbp_file.data.vehicle_spec.weight_e = atoi(colVal);		
	} else if(strcmp(colName, "LengthS") == 0) {	
		g_mbp_file.data.vehicle_spec.length_s = atoi(colVal);		
	} else if(strcmp(colName, "Tankage") == 0) {
		g_mbp_file.data.vehicle_spec.tankage = atof(colVal);		
	} else if(strcmp(colName, "FE") == 0) {	
		g_mbp_file.data.vehicle_spec.fe = atof(colVal);			
	} else if(strcmp(colName, "ECU") == 0) {			
		twSqlite3SetECU(colVal);		
	} else if(strcmp(colName, "TCU") == 0) {			
		twSqlite3SetTCU(colVal);
	} else if(strcmp(colName, "FRMapC") == 0) {			
		twSqlite3SetFRMapC(colVal);	
	}
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3CreateTripTable()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3CreateTripTable(void)
{
	sqlite3 *db;
   	char *zErrMsg = NULL, *sql = NULL;
   	int rc;
	
	/* Open database */	
	rc = sqlite3_open_v2(SQLITE_STATS_DB_PATH, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sql = "CREATE TABLE TRIP (\
	      ID INTEGER PRIMARY 	KEY     NOT NULL,\
	      SECONDS             	INT    	NOT NULL 	UNIQUE,\
	      DISTANCE      		INT   	NOT NULL,\
	      GAS_MILEAGE      		REAL    NOT NULL,\
	      GAS_CONSUMP      		REAL    NOT NULL,\
	      RUN_TIME      		INT   	NOT NULL,\
	      GAS_BILL         		INT    	NOT NULL)";
	_LOG_WARN("sql : %s", sql);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, twSqlite3Cb, 0, &zErrMsg);
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Table created successfully\n");
	}
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectMaker()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectMaker(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;	
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */	
	sprintf(sql, "SELECT DISTINCT Maker FROM Vehicle01 WHERE \
					(%s) ORDER BY Maker ASC", NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectType01()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectType01(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT DISTINCT Type01 FROM Vehicle01 WHERE \
					(Maker = '%s') \
					AND (%s) ORDER BY Type01 ASC", 
					twSqlite3GetUserCarinfo(STEP_MAKER),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectReleased()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectReleased(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT MIN(Released), MAX(Discontinued) FROM Vehicle01 WHERE \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s) ORDER BY Released DESC", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectFuel()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectFuel(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT DISTINCT Fuel FROM Vehicle01 WHERE  \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s BETWEEN Released AND Discontinued) \
					AND (%s) ORDER BY Fuel DESC", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					twSqlite3GetUserCarinfo(STEP_RELEASED),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectDisplacement()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectDisplacement(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT DISTINCT Displacement FROM Vehicle01 WHERE   \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s BETWEEN Released AND Discontinued) \
					AND (Fuel = '%s') \
					AND (%s) ORDER BY Displacement ASC", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					twSqlite3GetUserCarinfo(STEP_RELEASED),
					twSqlite3GetUserCarinfo(STEP_FUEL),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectFE()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectFE(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT DISTINCT FE FROM Vehicle01 WHERE \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s BETWEEN Released AND Discontinued) \
					AND (Fuel = '%s') \
					AND (Displacement = '%s') \
					AND (%s) ORDER BY FE ASC", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					twSqlite3GetUserCarinfo(STEP_RELEASED),
					twSqlite3GetUserCarinfo(STEP_FUEL),
					twSqlite3GetUserCarinfo(STEP_DISPLACEMENT),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectCapacity()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectCapacity(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT DISTINCT Capacity FROM Vehicle01 WHERE \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s BETWEEN Released AND Discontinued) \
					AND (Fuel = '%s') \
					AND (Displacement = '%s') \
					AND (fe = '%s') \
					AND (%s) ORDER BY Capacity ASC", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					twSqlite3GetUserCarinfo(STEP_RELEASED),
					twSqlite3GetUserCarinfo(STEP_FUEL),
					twSqlite3GetUserCarinfo(STEP_DISPLACEMENT),
					twSqlite3GetUserCarinfo(STEP_FE),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectLengthW()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectLengthW(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT DISTINCT LengthW FROM Vehicle01 WHERE \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s BETWEEN Released AND Discontinued) \
					AND (Fuel = '%s') \
					AND (Displacement = '%s') \
					AND (fe = '%s') \
					AND (Capacity = '%s') \
					AND (%s) ORDER BY LengthW ASC", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					twSqlite3GetUserCarinfo(STEP_RELEASED),
					twSqlite3GetUserCarinfo(STEP_FUEL),
					twSqlite3GetUserCarinfo(STEP_DISPLACEMENT),
					twSqlite3GetUserCarinfo(STEP_FE),
					twSqlite3GetUserCarinfo(STEP_CAPACITY),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectModel()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectModel(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT DISTINCT Model FROM Vehicle01 WHERE \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s BETWEEN Released AND Discontinued) \
					AND (Fuel = '%s') \
					AND (Displacement = '%s') \
					AND (fe = '%s') \
					AND (Capacity = '%s') \
					AND (LengthW = '%s') \
					AND (%s)", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					twSqlite3GetUserCarinfo(STEP_RELEASED),
					twSqlite3GetUserCarinfo(STEP_FUEL),
					twSqlite3GetUserCarinfo(STEP_DISPLACEMENT),
					twSqlite3GetUserCarinfo(STEP_FE),
					twSqlite3GetUserCarinfo(STEP_CAPACITY),
					twSqlite3GetUserCarinfo(STEP_LENGTH_W),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectDecision()
// Description    :
//---------------------------------------------------------------------------------
static void 
twSqlite3SelectDecision(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT * FROM Vehicle01 WHERE \
					(Maker = '%s') \
					AND (type01 ='%s') \
					AND (%s BETWEEN Released AND Discontinued) \
					AND (Fuel = '%s') \
					AND (Displacement = '%s') \
					AND (fe = '%s') \
					AND (Capacity = '%s') \
					AND (LengthW = '%s') \
					AND (Model = '%s') \
					AND (%s)", 
					twSqlite3GetUserCarinfo(STEP_MAKER), 
					twSqlite3GetUserCarinfo(STEP_TYPE01),
					twSqlite3GetUserCarinfo(STEP_RELEASED),
					twSqlite3GetUserCarinfo(STEP_FUEL),
					twSqlite3GetUserCarinfo(STEP_DISPLACEMENT),
					twSqlite3GetUserCarinfo(STEP_FE),
					twSqlite3GetUserCarinfo(STEP_CAPACITY),
					twSqlite3GetUserCarinfo(STEP_LENGTH_W),
					twSqlite3GetUserCarinfo(STEP_MODEL),
					NOT_SUPPORT_WITH_THINKWARE_WHERE_CLAUSE);
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ResetStats()
// Description    :
//---------------------------------------------------------------------------------
static void
twSqlite3ResetStats(void)
{
	memset(&g_stats_data, 0x0, sizeof(stats_data_t));
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectMonthStats()
// Description    :
//---------------------------------------------------------------------------------
static void
twSqlite3SelectMonthStats(struct tm * search_tm)
{
	struct tm end_tm;
	int startSeconds, endSeconds;
	
	startSeconds = tm_mktime(search_tm);	
	
	memcpy(&end_tm, search_tm, sizeof(struct tm));
	
	if (end_tm.tm_mon == 11) {
		end_tm.tm_year++;
		end_tm.tm_mon = 0;
	} else {
		end_tm.tm_mon++;
	}	

	_LOG("============== end range ==============");
	_LOG("tm_year : %d", end_tm.tm_year);
	_LOG("tm_mon : %d", end_tm.tm_mon+1);
	_LOG("tm_mday : %d", end_tm.tm_mday);
	_LOG("tm_hour : %d", end_tm.tm_hour);
	_LOG("tm_min : %d", end_tm.tm_min);
	_LOG("tm_sec : %d", end_tm.tm_sec);

	endSeconds = tm_mktime(&end_tm);
	_LOG("startSeconds : %d, endSeconds: %d",startSeconds, endSeconds);
	
	twSqlite3SelectStatsPerMonth(startSeconds, endSeconds, twSqlite3CbStats);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ListInit()
// Description    :
//---------------------------------------------------------------------------------
void
twSqlite3ListInit(int async_type)
{
	sqlite3_list_manager_t * manager = twSqlite3ListGetManager(async_type);
	if(manager == NULL) {
		_LOG_ERROR("manager is NULL, async_type : %d", async_type);
		return;
	}
	
	if (manager->node_numbers == 0) {
		memset((void *)manager, 0x00, sizeof(sqlite3_list_manager_t));	
		manager->head.next = &manager->tail;
	}		

	g_sqlite3_async_type = async_type;
	_LOG_WARN("g_sqlite3_async_type : %d", g_sqlite3_async_type);
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ListFree()
// Description    :
//---------------------------------------------------------------------------------
void
twSqlite3ListFree(int async_type)
{
	sqlite3_list_node_t * curr_node, * temp;	
	sqlite3_list_manager_t * manager;

	manager = twSqlite3ListGetManager(async_type);	
	if(manager == NULL) {
		_LOG_ERROR("manager is NULL, async_type : %d", async_type);
		return;
	}	

	curr_node = manager->head.next;	
	
	while (curr_node != &manager->tail) {
		manager->node_numbers--;
		
		// free item
		if(curr_node->node_item != NULL) {			
			tw_MemFree(curr_node->node_item);
		}
		
		temp = curr_node;
		curr_node = curr_node->next;
		
		// free node
		tw_MemFree(temp);				
	}	

	manager->head.next = &manager->tail;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ListFreeRange()
// Description    :
//---------------------------------------------------------------------------------
void
twSqlite3ListFreeRange(int start_async, int end_async)
{
	int async_type;
	
	for (async_type = start_async ; async_type <= end_async ; async_type++) {
		if (twSqlite3ListGetNodeNumbers(async_type) > 0) {
			twSqlite3ListFree(async_type);
		}
	}
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ListGetManager()
// Description    :
//---------------------------------------------------------------------------------
sqlite3_list_manager_t * 
twSqlite3ListGetManager(int async_type)
{
	return &g_sqlite3_list_manager[async_type];
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3ListGetNodeNumbers()
// Description    :
//---------------------------------------------------------------------------------
int  
twSqlite3ListGetNodeNumbers(int async_type)
{
	sqlite3_list_manager_t * manager;

	manager = twSqlite3ListGetManager(async_type);
	if(manager == NULL) {
		_LOG_ERROR("manager is NULL, async_type : %d", async_type);
		return -1;
	}
	
	return manager->node_numbers;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SaveTrip()
// Description    :
//---------------------------------------------------------------------------------
int 
twSqlite3SaveTrip(const tw_trip_t * trip)
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
	char sql[SQLITE_QUERY_BUF] = {0,};
   	int rc;	

	/* Create Trip Table */
	if (!is_file_exist(SQLITE_STATS_DB_PATH, 1)) {
		twSqlite3CreateTripTable();
	}
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_STATS_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {		
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));		
		return rc;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */	        
	sprintf(sql, "INSERT OR IGNORE INTO TRIP (SECONDS, DISTANCE, GAS_MILEAGE, GAS_CONSUMP, RUN_TIME, GAS_BILL) VALUES (%d, %d, %f, %f, %d, %d);\
	              UPDATE TRIP SET DISTANCE = %d, GAS_MILEAGE = %f, GAS_CONSUMP = %f, RUN_TIME = %d, GAS_BILL = %d WHERE SECONDS = %d",				 
	             trip->seconds, trip->distance, trip->gas_milege, trip->gas_consump, trip->run_time, trip->gas_bill,
	             trip->distance, trip->gas_milege, trip->gas_consump, trip->run_time, trip->gas_bill, trip->seconds);
	/*sprintf(sql, "INSERT OR REPLACE INTO TRIP (DATE_TIME, DISTANCE, GAS_BILL, GAS_MILEAGE) VALUES (%d, %d, %d, %f)",	              			 
	             trip.date, trip.distance, trip.gas_bill, trip.gas_milege);*/
	_LOG_WARN("sql : %s", sql);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, twSqlite3Cb, 0, &zErrMsg);
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Save done successfully\n");
	}	
	sqlite3_close(db);

	return rc;
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectAsync()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SelectAsync(int async_type)
{	
	twSqlite3ListInit(async_type);
	
	switch (async_type) {
	case SQLITE3_ASYNC_MAKER:	
		twSqlite3SelectMaker(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_TYPE01:
		twSqlite3SelectType01(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_RELEASED:	
		twSqlite3SelectReleased(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_FUEL:	
		twSqlite3SelectFuel(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_DISPLACEMENT:
		twSqlite3SelectDisplacement(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_FE:	
		twSqlite3SelectFE(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_CAPACITY:	
		twSqlite3SelectCapacity(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_LENGTH_W:	
		twSqlite3SelectLengthW(twSqlite3CbCarinfo);
		break;
	case SQLITE3_ASYNC_MODEL:	
		twSqlite3SelectModel(twSqlite3CbCarinfo);
		break;	
	case SQLITE3_ASYNC_DECISION:
		twSqlite3SelectDecision(twSqlite3CbDecision);
	
		twSqlite3MakeMbpFile();
		twSqlite3MakeFrmFile();
		twSqlite3MakeBcmFile();		
		break;
	case SQLITE3_ASYNC_STATS_MIN_MAX:
		twSqlite3ResetStats();
		twSqlite3SelectMinMax(twSqlite3CbStats);
		twSqlite3SelectStatsPerDay(g_stats_search.search_seconds, twSqlite3CbStats);
		break;	
	case SQLITE3_ASYNC_STATS_DAY:
		twSqlite3ResetStats();
		twSqlite3SelectStatsPerDay(g_stats_search.search_seconds, twSqlite3CbStats);
		break;	
	case SQLITE3_ASYNC_STATS_MONTH:		
		twSqlite3ResetStats();
		twSqlite3SelectMonthStats(&g_stats_search.search_tm);
		break;	

	}
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectDtc()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SelectDtc(int (*callback)(void *, int, char**, char**), obd_dtc_info_t *_dtc_info)
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc, i;	
	char sql[SQLITE_QUERY_BUF] = {0,};	
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}
	
	for (i = 0 ; i < _dtc_info->cnt ; i++) {		
		/* Create SQL statement */	
		sprintf(sql, "SELECT DISTINCT description,code FROM Diagnosis_COM WHERE code='%s'", _dtc_info->code[i]);
		_LOG_WARN("sql : %s", sql);

		/* Execute SQL statement */
		rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
		if( rc != SQLITE_OK ) {
			_LOG_ERROR("SQL error: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
		} else {
	  		_LOG_WARN("Select done successfully\n");
		}	
	}	
	sqlite3_close(db);	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectStatsPerDay()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SelectStatsPerDay(int seconds, int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
	char sql[SQLITE_QUERY_BUF] = {0,};
   	int rc;	
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_STATS_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */	        
	sprintf(sql, "SELECT * FROM TRIP WHERE \
					(SECONDS >= %d) AND (SECONDS < %d)", seconds, seconds + SECONDS_PER_DAY);
	_LOG_WARN("sql : %s", sql);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}	
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectStatsPerMonth()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SelectStatsPerMonth(int startSeconds, int endSeconds, int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
	char sql[SQLITE_QUERY_BUF] = {0,};
   	int rc;	
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_STATS_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */	        
	sprintf(sql, "SELECT * FROM TRIP WHERE \
					(SECONDS >= %d) AND (SECONDS < %d)", startSeconds, endSeconds);
	_LOG_WARN("sql : %s", sql);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}	
	sqlite3_close(db);
	
}


//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SelectMinMax()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SelectMinMax(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
	char sql[SQLITE_QUERY_BUF] = {0,};
   	int rc;	
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_STATS_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */	        
	sprintf(sql, "SELECT MIN(SECONDS), MAX(SECONDS), MAX(ID) FROM TRIP");					
	_LOG_WARN("sql : %s", sql);

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}	
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SetDayStatsSearch()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SetDayStatsSearch(int type)
{		
	struct tm curr_tm, *prev_tm, *next_tm;	

	if (type == TODAY) {
		rtc_get_time(&curr_tm);
		curr_tm.tm_hour = 0;
		curr_tm.tm_min = 0;
		curr_tm.tm_sec = 0;

		g_stats_search.search_seconds = tm_mktime(&curr_tm);
		_LOG("today seconds : %d", g_stats_search.search_seconds);
		_LOG("============== today ==============");
		_LOG("tm_year : %d", curr_tm.tm_year);
		_LOG("tm_mon : %d", curr_tm.tm_mon+1);
		_LOG("tm_mday : %d", curr_tm.tm_mday);
		_LOG("tm_hour : %d", curr_tm.tm_hour);
		_LOG("tm_min : %d", curr_tm.tm_min);
		_LOG("tm_sec : %d", curr_tm.tm_sec);
		_LOG("tm_wday : %d", curr_tm.tm_wday);

		memcpy(&g_stats_search.search_tm, &curr_tm, sizeof(struct tm));
		
	} else if (type == PREV_DAY) {
		g_stats_search.search_seconds -= SECONDS_PER_DAY;
		_LOG("prev day seconds : %d", g_stats_search.search_seconds);
		prev_tm = tm_localtime((time_t *)&g_stats_search.search_seconds);
		_LOG("============== prev day ==============");
		_LOG("tm_year : %d", prev_tm->tm_year);
		_LOG("tm_mon : %d", prev_tm->tm_mon+1);
		_LOG("tm_mday : %d", prev_tm->tm_mday);
		_LOG("tm_hour : %d", prev_tm->tm_hour);
		_LOG("tm_min : %d", prev_tm->tm_min);
		_LOG("tm_sec : %d", prev_tm->tm_sec);
		_LOG("tm_wday : %d", prev_tm->tm_wday);

		memcpy(&g_stats_search.search_tm, prev_tm, sizeof(struct tm));
		
	} else if (type == NEXT_DAY) {
		g_stats_search.search_seconds += SECONDS_PER_DAY;
		_LOG("next day seconds : %d", g_stats_search.search_seconds);
		next_tm = tm_localtime((time_t *)&g_stats_search.search_seconds);
		_LOG("============== next day ==============");
		_LOG("tm_year : %d", next_tm->tm_year);
		_LOG("tm_mon : %d", next_tm->tm_mon+1);
		_LOG("tm_mday : %d", next_tm->tm_mday);
		_LOG("tm_hour : %d", next_tm->tm_hour);
		_LOG("tm_min : %d", next_tm->tm_min);
		_LOG("tm_sec : %d", next_tm->tm_sec);	
		_LOG("tm_wday : %d", next_tm->tm_wday);

		memcpy(&g_stats_search.search_tm, next_tm, sizeof(struct tm));
	}	
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SetMonthStatsSearch()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SetMonthStatsSearch(int type)
{
	struct tm curr_tm;
		
	if (type == PREV_MONTH) {
		if (g_stats_search.search_tm.tm_mon == 0) {
			g_stats_search.search_tm.tm_year--;
			g_stats_search.search_tm.tm_mon = 11;
		} else {
			g_stats_search.search_tm.tm_mon--;
		}
	} else if (type == NEXT_MONTH) {
		if (g_stats_search.search_tm.tm_mon == 11) {
			g_stats_search.search_tm.tm_year++;
			g_stats_search.search_tm.tm_mon = 0;
		} else {
			g_stats_search.search_tm.tm_mon++;
		}
	} else if (type == CURRENT_MONTH) {
		rtc_get_time(&curr_tm);
		curr_tm.tm_mday = 1;
		curr_tm.tm_hour = 0;
		curr_tm.tm_min = 0;
		curr_tm.tm_sec = 0;	

		memcpy(&g_stats_search.search_tm, &curr_tm, sizeof(struct tm));
	}

	_LOG("============== search month ==============");
	_LOG("tm_year : %d", g_stats_search.search_tm.tm_year);
	_LOG("tm_mon : %d", g_stats_search.search_tm.tm_mon+1);
	_LOG("tm_mday : %d", g_stats_search.search_tm.tm_mday);
	_LOG("tm_hour : %d", g_stats_search.search_tm.tm_hour);
	_LOG("tm_min : %d", g_stats_search.search_tm.tm_min);
	_LOG("tm_sec : %d", g_stats_search.search_tm.tm_sec);		

}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetStatsData()
// Description    :
//---------------------------------------------------------------------------------
stats_data_t *
twSqlite3GetStatsData(void)
{
	return &g_stats_data;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetStatsSearch()
// Description    :
//---------------------------------------------------------------------------------
stats_search_t *
twSqlite3GetStatsSearch(void)
{
	return &g_stats_search;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetStatsCount()
// Description    :
//---------------------------------------------------------------------------------
int
twSqlite3GetStatsCount(void)
{
	_LOG("get stats count :%d", g_stats_count);
	return g_stats_count;
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3SetUserCarinfo()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3SetUserCarinfo(int step, const char * choice)
{	
	float prefFE;	

	memset(app_pref_user->carinfo[step], 0, sizeof(app_pref_user->carinfo[step]));
	memcpy(app_pref_user->carinfo[step], choice, strlen(choice));		
	//_LOG_WARN("step : %d, carinfo : %s", step, app_pref_user->carinfo[step]);

	if (step == STEP_FE) {
		prefFE = atof(choice);			
		update_obd_preference(OBD_PREF_FUEL_EFF, (int)(prefFE*10));
	}	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3GetUserCarinfo()
// Description    :
//---------------------------------------------------------------------------------
const char * 
twSqlite3GetUserCarinfo(int step)
{	
	//_LOG_WARN("step : %d, carinfo : %s", step, app_pref_user->carinfo[step]);
	return app_pref_user->carinfo[step]; 	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3AllClearUserCarinfo()
// Description    :
//---------------------------------------------------------------------------------
void
twSqlite3AllClearUserCarinfo(void)
{
	int step;

	for (step = STEP_MAKER ; step < STEP_MAX ; step++) {
		memset(app_pref_user->carinfo[step], 0, sizeof(app_pref_user->carinfo[step]));
	}	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3VehicleDbVer()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3VehicleDbVer(int (*callback)(void *, int, char**, char**))
{
	sqlite3 *db;
   	char *zErrMsg = NULL;
   	int rc;
	char sql[SQLITE_QUERY_BUF] = {0,};
	
	/* Open database */
	rc = sqlite3_open_v2(SQLITE_VEHICLE_DB_PATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sprintf(sql, "SELECT ver FROM Info");
	_LOG_WARN("sql : %s", sql);
				
	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, callback, 0, &zErrMsg);	
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Select done successfully\n");
	}
	sqlite3_close(db);	
}


//---------------------------------------------------------------------------------
// Function Name  : twSqlite3Update()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3Update(void)
{
	sqlite3 *db;
   	char *zErrMsg = NULL, *sql = NULL;
   	int rc;
	
	/* Open database */
	rc = sqlite3_open(SQLITE_VEHICLE_DB_PATH, &db);
	if( rc ){
	  _LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
	  return;
	}else{
	  _LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sql = "UPDATE COMPANY set SALARY = 25000 where ID=1; " \
         "SELECT * from COMPANY";

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, twSqlite3Cb, 0, &zErrMsg);
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Update done successfully\n");
	}
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3Delete()
// Description    :
//---------------------------------------------------------------------------------
void 
twSqlite3Delete(void)
{
	sqlite3 *db;
   	char *zErrMsg = NULL, *sql = NULL;
   	int rc;
	
	/* Open database */
	rc = sqlite3_open(SQLITE_VEHICLE_DB_PATH, &db);
	if( rc ) {
		_LOG_ERROR("Can't open database: %s\n", sqlite3_errmsg(db));
		return;
	} else {
		_LOG_WARN("Opened database successfully\n");
	}

	/* Create SQL statement */
	sql = "DELETE from COMPANY where ID=2; " \
         "SELECT * from COMPANY";

	/* Execute SQL statement */
	rc = sqlite3_exec(db, sql, twSqlite3Cb, 0, &zErrMsg);
	if( rc != SQLITE_OK ) {
		_LOG_ERROR("SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
	} else {
	  	_LOG_WARN("Delete done successfully\n");
	}
	sqlite3_close(db);
	
}

//---------------------------------------------------------------------------------
// Function Name  : twSqlite3Init()
// Description    :
//---------------------------------------------------------------------------------
int 
twSqlite3Init(void)
{
	return twSqlite3Initialize();
}


