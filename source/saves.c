#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <psp2/appmgr.h>

#include "saves.h"
#include "common.h"
#include "sfo.h"
#include "settings.h"
#include "utils.h"
#include "sqlite3.h"
#include "vitashell_user.h"

#define UTF8_CHAR_GROUP		"\xe2\x97\x86"
#define UTF8_CHAR_ITEM		"\xe2\x94\x97"
#define UTF8_CHAR_STAR		"\xE2\x98\x85"

#define CHAR_ICON_ZIP		"\x0C"
#define CHAR_ICON_COPY		"\x0B"
#define CHAR_ICON_SIGN		"\x06"
#define CHAR_ICON_USER		"\x07"
#define CHAR_ICON_LOCK		"\x08"
#define CHAR_ICON_WARN		"\x0F"

#define MAX_MOUNT_POINT_LENGTH 16

int sqlite_init();

char pfs_mount_point[MAX_MOUNT_POINT_LENGTH];
const int known_pfs_ids[] = { 0x6E, 0x12E, 0x12F, 0x3ED };

static sqlite3* open_sqlite_db(const char* db_path)
{
	sqlite3 *db;

	// initialize the SceSqlite rw_vfs
	if (sqlite_init() != SQLITE_OK)
	{
		LOG("Error sqlite init");
		return NULL;
	}

	LOG("Opening '%s'...", db_path);
	if (sqlite3_open_v2(db_path, &db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK)
	{
		LOG("Error db open: %s", sqlite3_errmsg(db));
		return NULL;
	}

	return db;
}

static int get_appdb_title(sqlite3* db, const char* titleid, char* name)
{
	sqlite3_stmt* res;

	if (!db)
		return 0;

	char* query = sqlite3_mprintf("SELECT titleId, title FROM tbl_appinfo_icon WHERE (titleId = %Q)", titleid);

	if (sqlite3_prepare_v2(db, query, -1, &res, NULL) != SQLITE_OK || sqlite3_step(res) != SQLITE_ROW)
	{
		LOG("Failed to fetch data: %s", sqlite3_errmsg(db));
		sqlite3_free(query);
		return 0;
	}

	strncpy(name, (const char*) sqlite3_column_text(res, 1), 0x80);
	sqlite3_free(query);

	return 1;
}

int vita_SaveUmount(const char* mount)
{
	if (pfs_mount_point[0] == 0)
		return 0;

	int umountErrorCode = sceAppMgrUmount(pfs_mount_point);	
	if (umountErrorCode < 0)
	{
		LOG("UMOUNT_ERROR (%X)", umountErrorCode);
		notification("Warning! Save couldn't be unmounted!");
		return 0;
	}
	pfs_mount_point[0] = 0;

	return (umountErrorCode == SUCCESS);
}

int vita_SaveMount(const save_entry_t *save, char* mount)
{
	char path[0x100];
	char klicensee[0x10];
	ShellMountIdArgs args;

	memset(klicensee, 0, sizeof(klicensee));
	snprintf(path, sizeof(path), "ux0:user/00/savedata/%s", save->dir_name);
	strcpy(mount, save->dir_name);

	args.process_titleid = "NP0APOLLO";
	args.path = path;
	args.desired_mount_point = NULL;
	args.klicensee = klicensee;
	args.mount_point = pfs_mount_point;

	for (int i = 0; i < countof(known_pfs_ids); i++)
	{
		args.id = known_pfs_ids[i];
		if (shellUserMountById(&args) < 0)
			continue;

		LOG("[%s] '%s' mounted (%s)", save->title_id, pfs_mount_point, path);
		return 1;
	}

	int mountErrorCode = sceAppMgrGameDataMount(path, 0, 0, pfs_mount_point);
	if (mountErrorCode < 0)
	{
		LOG("ERROR (%X): can't mount '%s/%s'", mountErrorCode, save->title_id, save->dir_name);
		return 0;
	}

	LOG("[%s] '/%s' mounted (%s)", save->title_id, pfs_mount_point, path);
	return 1;
}

int orbis_UpdateSaveParams(const char* mountPath, const char* title, const char* subtitle, const char* details)
{
	/*
	OrbisSaveDataParam saveParams;
	OrbisSaveDataMountPoint mount;

	memset(&saveParams, 0, sizeof(OrbisSaveDataParam));
	memset(&mount, 0, sizeof(OrbisSaveDataMountPoint));

	strncpy(mount.data, mountPath, sizeof(mount.data));
	strncpy(saveParams.title, title, ORBIS_SAVE_DATA_TITLE_MAXSIZE);
	strncpy(saveParams.subtitle, subtitle, ORBIS_SAVE_DATA_SUBTITLE_MAXSIZE);
	strncpy(saveParams.details, details, ORBIS_SAVE_DATA_DETAIL_MAXSIZE);
	saveParams.mtime = time(NULL);

	int32_t setParamResult = sceSaveDataSetParam(&mount, ORBIS_SAVE_DATA_PARAM_TYPE_ALL, &saveParams, sizeof(OrbisSaveDataParam));
	if (setParamResult < 0) {
		LOG("sceSaveDataSetParam error (%X)", setParamResult);
		return 0;
	}

	return (setParamResult == SUCCESS);
*/ return 0;
}

/*
 * Function:		endsWith()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Checks to see if a ends with b
 * Arguments:
 *	a:				String
 *	b:				Potential end
 * Return:			pointer if true, NULL if false
 */
static char* endsWith(const char * a, const char * b)
{
	int al = strlen(a), bl = strlen(b);
    
	if (al < bl)
		return NULL;

	a += (al - bl);
	while (*a)
		if (toupper(*a++) != toupper(*b++)) return NULL;

	return (char*) (a - bl);
}

/*
 * Function:		readFile()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		reads the contents of a file into a new buffer
 * Arguments:
 *	path:			Path to file
 * Return:			Pointer to the newly allocated buffer
 */
char * readTextFile(const char * path, long* size)
{
	FILE *f = fopen(path, "rb");

	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long fsize = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (fsize <= 0)
	{
		fclose(f);
		return NULL;
	}

	char * string = malloc(fsize + 1);
	fread(string, fsize, 1, f);
	fclose(f);

	string[fsize] = 0;
	if (size)
		*size = fsize;

	return string;
}

static code_entry_t* _createCmdCode(uint8_t type, const char* name, char code)
{
	code_entry_t* entry = (code_entry_t *)calloc(1, sizeof(code_entry_t));
	entry->type = type;
	entry->name = strdup(name);
	asprintf(&entry->codes, "%c", code);

	return entry;
}

static option_entry_t* _initOptions(int count)
{
	option_entry_t* options = (option_entry_t*)malloc(sizeof(option_entry_t));

	options->id = 0;
	options->sel = -1;
	options->size = count;
	options->line = NULL;
	options->value = malloc (sizeof(char *) * count);
	options->name = malloc (sizeof(char *) * count);

	return options;
}

static option_entry_t* _createOptions(int count, const char* name, char value)
{
	option_entry_t* options = _initOptions(count);

	asprintf(&options->name[0], "%s (%s)", name, UMA0_PATH);
	asprintf(&options->value[0], "%c%c", value, 0);
	asprintf(&options->name[1], "%s (%s)", name, IMC0_PATH);
	asprintf(&options->value[1], "%c%c", value, 1);

	return options;
}

static save_entry_t* _createSaveEntry(uint16_t flag, const char* name)
{
	save_entry_t* entry = (save_entry_t *)calloc(1, sizeof(save_entry_t));
	entry->flags = flag;
	entry->name = strdup(name);

	return entry;
}

static void _walk_dir_list(const char* startdir, const char* inputdir, const char* mask, list_t* list)
{
	char fullname[256];	
	struct dirent *dirp;
	int len = strlen(startdir);
	DIR *dp = opendir(inputdir);

	if (!dp) {
		LOG("Failed to open input directory: '%s'", inputdir);
		return;
	}

	while ((dirp = readdir(dp)) != NULL)
	{
		if ((strcmp(dirp->d_name, ".")  == 0) || (strcmp(dirp->d_name, "..") == 0) || (strcmp(dirp->d_name, "sce_sys") == 0) ||
			(strcmp(dirp->d_name, "ICON0.PNG") == 0) || (strcmp(dirp->d_name, "PARAM.SFO") == 0) || (strcmp(dirp->d_name,"PIC1.PNG") == 0) ||
			(strcmp(dirp->d_name, "ICON1.PMF") == 0) || (strcmp(dirp->d_name, "SND0.AT3") == 0))
			continue;

		snprintf(fullname, sizeof(fullname), "%s%s", inputdir, dirp->d_name);

		if (dirp->d_stat.st_mode & SCE_S_IFDIR)
		{
			strcat(fullname, "/");
			_walk_dir_list(startdir, fullname, mask, list);
		}
		else if (wildcard_match_icase(dirp->d_name, mask))
		{
			//LOG("Adding file '%s'", fullname+len);
			list_append(list, strdup(fullname+len));
		}
	}
	closedir(dp);
}

static option_entry_t* _getFileOptions(const char* save_path, const char* mask, uint8_t is_cmd)
{
	char *filename;
	list_t* file_list;
	list_node_t* node;
	int i = 0;
	option_entry_t* opt;

	if (dir_exists(save_path) != SUCCESS)
		return NULL;

	LOG("Loading filenames {%s} from '%s'...", mask, save_path);

	file_list = list_alloc();
	_walk_dir_list(save_path, save_path, mask, file_list);
	opt = _initOptions(list_count(file_list));

	for (node = list_head(file_list); (filename = list_get(node)); node = list_next(node))
	{
		LOG("Adding '%s' (%s)", filename, mask);
		opt->name[i] = filename;

		if (is_cmd)
			asprintf(&opt->value[i], "%c", is_cmd);
		else
			asprintf(&opt->value[i], "%s", mask);

		i++;
	}

	list_free(file_list);

	return opt;
}

static void _addBackupCommands(save_entry_t* item)
{
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Apply Changes & Resign", CMD_RESIGN_SAVE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Save Details", CMD_VIEW_DETAILS);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " File Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy save game", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(3, "Copy Save to Backup Storage", CMD_COPY_SAVE_USB);
	asprintf(&cmd->options->name[2], "Copy Save to User Storage (ux0:%s/)", (item->flags & SAVE_FLAG_PSP) ? "pspemu":"user");
	asprintf(&cmd->options->value[2], "%c", CMD_COPY_SAVE_HDD);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_ZIP " Export save game to Zip", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(3, "Export Zip to Backup Storage", CMD_EXPORT_ZIP_USB);
	asprintf(&cmd->options->name[2], "Export Zip to User Storage (ux0:data/)");
	asprintf(&cmd->options->value[2], "%c", CMD_EXPORT_ZIP_HDD);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Export decrypted save files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_DECRYPT_FILE);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Import decrypted save files", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _getFileOptions(item->path, "*", CMD_IMPORT_DATA_FILE);
	list_append(item->codes, cmd);
}

static option_entry_t* _getSaveTitleIDs(const char* title_id)
{
	int count = 1;
	option_entry_t* opt;
	char tmp[16];
	const char *ptr;
	const char *tid = NULL;//get_game_title_ids(title_id);

	if (!tid)
		tid = title_id;

	ptr = tid;
	while (*ptr)
		if (*ptr++ == '/') count++;

	LOG("Adding (%d) TitleIDs=%s", count, tid);

	opt = _initOptions(count);
	int i = 0;

	ptr = tid;
	while (*ptr++)
	{
		if ((*ptr == '/') || (*ptr == 0))
		{
			memset(tmp, 0, sizeof(tmp));
			strncpy(tmp, tid, ptr - tid);
			asprintf(&opt->name[i], "%s", tmp);
			asprintf(&opt->value[i], "%c", SFO_CHANGE_TITLE_ID);
			tid = ptr+1;
			i++;
		}
	}

	return opt;
}

static void _addSfoCommands(save_entry_t* save)
{
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Keystone Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Export Keystone", CMD_EXP_KEYSTONE);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Import Keystone", CMD_IMP_KEYSTONE);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Dump save Fingerprint", CMD_EXP_FINGERPRINT);
	list_append(save->codes, cmd);

	return;
/*
	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " SFO Patches " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_SFO, CHAR_ICON_USER " Change Account ID", SFO_CHANGE_ACCOUNT_ID);
	cmd->options_count = 1;
	cmd->options = _initOptions(2);
	cmd->options->name[0] = strdup("Remove ID/Offline");
	cmd->options->value[0] = calloc(1, SFO_ACCOUNT_ID_SIZE);
	cmd->options->name[1] = strdup("Fake Owner");
	cmd->options->value[1] = strdup("ffffffffffffffff");
	list_append(save->codes, cmd);

	cmd = _createCmdCode(PATCH_SFO, CHAR_ICON_USER " Remove Console ID", SFO_REMOVE_PSID);
	list_append(save->codes, cmd);

	if (save->flags & SAVE_FLAG_LOCKED)
	{
		cmd = _createCmdCode(PATCH_SFO, CHAR_ICON_LOCK " Remove copy protection", SFO_UNLOCK_COPY);
		list_append(save->codes, cmd);
	}

	cmd = _createCmdCode(PATCH_SFO, CHAR_ICON_USER " Change Region Title ID", SFO_CHANGE_TITLE_ID);
	cmd->options_count = 1;
	cmd->options = _getSaveTitleIDs(save->title_id);
	list_append(save->codes, cmd);
*/
}

static void add_psp_commands(save_entry_t* item)
{
	code_entry_t* cmd;

	cmd = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Game Key Backup " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Export binary Game Key", CMD_EXP_PSPKEY);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Dump Game Key fingerprint", CMD_DUMP_PSPKEY);
	list_append(item->codes, cmd);

	return;
}

option_entry_t* get_file_entries(const char* path, const char* mask)
{
	return _getFileOptions(path, mask, CMD_CODE_NULL);
}

/*
 * Function:		ReadLocalCodes()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Reads an entire NCL file into an array of code_entry
 * Arguments:
 *	path:			Path to ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
int ReadCodes(save_entry_t * save)
{
	code_entry_t * code;
	char filePath[256];
	char * buffer = NULL;
	char mount[32];

	save->codes = list_alloc();

	if (save->flags & SAVE_FLAG_PSV && save->flags & SAVE_FLAG_HDD)
		if (!vita_SaveMount(save, mount))
		{
			code = _createCmdCode(PATCH_NULL, CHAR_ICON_WARN " --- Error Mounting Save! Check Save Mount Patches --- " CHAR_ICON_WARN, CMD_CODE_NULL);
			list_append(save->codes, code);
			return list_count(save->codes);
		}

	_addBackupCommands(save);
	(save->flags & SAVE_FLAG_PSP) ? add_psp_commands(save) : _addSfoCommands(save);

	snprintf(filePath, sizeof(filePath), APOLLO_DATA_PATH "%s.savepatch", save->title_id);
	if (file_exists(filePath) != SUCCESS)
		goto skip_end;

	code = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Cheats " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);	
	list_append(save->codes, code);

	code = _createCmdCode(PATCH_COMMAND, CHAR_ICON_USER " View Raw Patch File", CMD_VIEW_RAW_PATCH);
	list_append(save->codes, code);

	LOG("Loading BSD codes '%s'...", filePath);
	buffer = readTextFile(filePath, NULL);
	load_patch_code_list(buffer, save->codes, &get_file_entries, save->path);
	free (buffer);

skip_end:
	if (save->flags & SAVE_FLAG_PSV && save->flags & SAVE_FLAG_HDD)
		vita_SaveUmount(mount);

	LOG("Loaded %ld codes", list_count(save->codes));

	return list_count(save->codes);
}

int ReadTrophies(save_entry_t * game)
{
	int trop_count = 0;
	code_entry_t * trophy;
	char query[256];
	sqlite3 *db;
	sqlite3_stmt *res;

	if ((db = open_sqlite_db(game->path)) == NULL)
		return 0;

	game->codes = list_alloc();
/*
	trophy = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Apply Changes & Resign Trophy Set", CMD_RESIGN_TROPHY);
	list_append(game->codes, trophy);

	trophy = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Backup Trophy Set to USB", CMD_CODE_NULL);
	trophy->file = strdup(game->path);
	trophy->options_count = 1;
	trophy->options = _createOptions(2, "Copy Trophy to USB", CMD_EXP_TROPHY_USB);
	list_append(game->codes, trophy);

	trophy = _createCmdCode(PATCH_COMMAND, CHAR_ICON_ZIP " Export Trophy Set to Zip", CMD_CODE_NULL);
	trophy->file = strdup(game->path);
	trophy->options_count = 1;
	trophy->options = _createOptions(2, "Save .Zip to USB", CMD_EXPORT_ZIP_USB);
	list_append(game->codes, trophy);
*/
	trophy = _createCmdCode(PATCH_NULL, "----- " UTF8_CHAR_STAR " Trophies " UTF8_CHAR_STAR " -----", CMD_CODE_NULL);
	list_append(game->codes, trophy);

	snprintf(query, sizeof(query), "SELECT title_id, npcommid, title, description, grade, unlocked, id FROM tbl_trophy_flag WHERE title_id = %d", game->blocks);

	if (sqlite3_prepare_v2(db, query, -1, &res, NULL) != SQLITE_OK)
	{
		LOG("Failed to fetch data: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		return 0;
	}

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		snprintf(query, sizeof(query), "   %s", sqlite3_column_text(res, 2));
		trophy = _createCmdCode(PATCH_NULL, query, CMD_CODE_NULL);

		asprintf(&trophy->codes, "%s\n", sqlite3_column_text(res, 3));

		switch (sqlite3_column_int(res, 4))
		{
		case 4:
			trophy->name[0] = CHAR_TRP_BRONZE;
			break;

		case 3:
			trophy->name[0] = CHAR_TRP_SILVER;
			break;

		case 2:
			trophy->name[0] = CHAR_TRP_GOLD;
			break;

		case 1:
			trophy->name[0] = CHAR_TRP_PLATINUM;
			break;

		default:
			break;
		}

		trop_count = sqlite3_column_int(res, 6);
		trophy->file = malloc(sizeof(trop_count));
		memcpy(trophy->file, &trop_count, sizeof(trop_count));

		if (!sqlite3_column_int(res, 5))
			trophy->name[1] = CHAR_TAG_LOCKED;

		// if trophy has been synced, we can't allow changes
		if (0)
			trophy->name[1] = CHAR_TRP_SYNC;
		else
			trophy->type = (sqlite3_column_int(res, 5) ? PATCH_TROP_LOCK : PATCH_TROP_UNLOCK);

		LOG("Trophy=%d [%d] '%s' (%s)", trop_count, trophy->type, trophy->name, trophy->codes);
		list_append(game->codes, trophy);
	}

	sqlite3_finalize(res);
	sqlite3_close(db);

	return list_count(game->codes);
}

/*
 * Function:		ReadOnlineSaves()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads an entire NCL file into an array of code_entry
 * Arguments:
 *	filename:		File name ncl
 *	_count_count:	Pointer to int (set to the number of codes within the ncl)
 * Return:			Returns an array of code_entry, null if failed to load
 */
int ReadOnlineSaves(save_entry_t * game)
{
	code_entry_t* item;
	char path[256];
	snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%s.txt", game->title_id);

	if (file_exists(path) == SUCCESS)
	{
		struct stat stats;
		stat(path, &stats);
		// re-download if file is +1 day old
		if ((stats.st_mtime + ONLINE_CACHE_TIMEOUT) < time(NULL))
			http_download(game->path, "saves.txt", path, 0);
	}
	else
	{
		if (!http_download(game->path, "saves.txt", path, 0))
			return -1;
	}

	long fsize;
	char *data = readTextFile(path, &fsize);
	
	char *ptr = data;
	char *end = data + fsize + 1;

	game->codes = list_alloc();

	while (ptr < end && *ptr)
	{
		const char* content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		if (content[12] == '=')
		{
			snprintf(path, sizeof(path), CHAR_ICON_ZIP " %s", content + 13);
			item = _createCmdCode(PATCH_COMMAND, path, CMD_CODE_NULL);
			asprintf(&item->file, "%.12s", content);

			item->options_count = 1;
			item->options = _createOptions(2, "Download to Backup Storage", CMD_DOWNLOAD_USB);
			list_append(game->codes, item);

			LOG("[%s%s] %s", game->path, item->file, item->name + 1);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	if (data) free(data);

	return (list_count(game->codes));
}

list_t * ReadBackupList(const char* userPath)
{
	char tmp[32];
	save_entry_t * item;
	code_entry_t * cmd;
	list_t *list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_ZIP, CHAR_ICON_ZIP " Extract Archives (RAR, Zip, 7z)");
	item->path = strdup("ux0:data/");
	item->type = FILE_TYPE_ZIP;
	list_append(list, item);

/*
	item = _createSaveEntry(SAVE_FLAG_PS4, CHAR_ICON_USER " Activate PS Vita Account");
	asprintf(&item->path, EXDATA_PATH_HDD, apollo_config.user_id);
	item->type = FILE_TYPE_ACT;
	list_append(list, item);

	item = _createSaveEntry(SAVE_FLAG_PS4, CHAR_ICON_LOCK " Show Parental Security Passcode");
	item->codes = list_alloc();
	cmd = _createCmdCode(PATCH_NULL, CHAR_ICON_LOCK " Security Passcode: ????????", CMD_CODE_NULL);
//	regMgr_GetParentalPasscode(tmp);
	strncpy(cmd->name + 21, tmp, 8);
	list_append(item->codes, cmd);
	list_append(list, item);

	for (int i = 0; i <= MAX_USB_DEVICES; i++)
	{
		snprintf(tmp, sizeof(tmp), USB_PATH, i);

		if (dir_exists(tmp) != SUCCESS)
			continue;

		snprintf(tmp, sizeof(tmp), CHAR_ICON_COPY " Import Licenses (USB %d)", i);
		item = _createSaveEntry(SAVE_FLAG_PS3, tmp);
		asprintf(&item->path, IMPORT_RAP_PATH_USB, i);
		item->type = FILE_TYPE_RAP;
		list_append(list, item);
	}
*/

	return list;
}

int ReadBackupCodes(save_entry_t * bup)
{
	code_entry_t * cmd;
	char tmp[256];

	switch(bup->type)
	{
	case FILE_TYPE_ZIP:
		break;

	case FILE_TYPE_ACT:
		bup->codes = list_alloc();

		LOG("Getting Users...");
		for (int i = 1; i <= 16; i++)
		{
			uint64_t account;
			char userName[0x20];

//			regMgr_GetUserName(i, userName);
			if (!userName[0])
				continue;

//			regMgr_GetAccountId(i, &account);
			snprintf(tmp, sizeof(tmp), "%c Activate Offline Account %s (%016lx)", account ? CHAR_TAG_LOCKED : CHAR_TAG_OWNER, userName, account);
			cmd = _createCmdCode(account ? PATCH_NULL : PATCH_COMMAND, tmp, CMD_CODE_NULL); //CMD_CREATE_ACT_DAT

			if (!account)
			{
				cmd->options_count = 1;
				cmd->options = calloc(1, sizeof(option_entry_t));
				cmd->options->sel = -1;
//				cmd->options->size = get_xml_owners(APOLLO_PATH OWNER_XML_FILE, CMD_CREATE_ACT_DAT, &cmd->options->name, &cmd->options->value);
				cmd->file = malloc(1);
				cmd->file[0] = i;
			}
			list_append(bup->codes, cmd);

			LOG("ID %d = '%s' (%lx)", i, userName, account);
		}

		return list_count(bup->codes);

	default:
		return 0;
	}

	bup->codes = list_alloc();

	LOG("Loading files from '%s'...", bup->path);

	DIR *d;
	struct dirent *dir;
	d = opendir(bup->path);

	if (d)
	{
		while ((dir = readdir(d)) != NULL)
		{
			if (!(dir->d_stat.st_mode & SCE_S_IFREG) ||
				(!endsWith(dir->d_name, ".RAR") && !endsWith(dir->d_name, ".ZIP") && !endsWith(dir->d_name, ".7Z")))
				continue;

			snprintf(tmp, sizeof(tmp), CHAR_ICON_ZIP " Extract %s", dir->d_name);
			cmd = _createCmdCode(PATCH_COMMAND, tmp, CMD_EXTRACT_ARCHIVE);
			asprintf(&cmd->file, "%s%s", bup->path, dir->d_name);

			LOG("[%s] name '%s'", cmd->file, cmd->name);
			list_append(bup->codes, cmd);
		}
		closedir(d);
	}

	if (!list_count(bup->codes))
	{
		list_free(bup->codes);
		bup->codes = NULL;
		return 0;
	}

	LOG("%ld items loaded", list_count(bup->codes));

	return list_count(bup->codes);
}

/*
 * Function:		UnloadGameList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Free entire array of game_entry
 * Arguments:
 *	list:			Array of game_entry to free
 *	count:			number of game entries
 * Return:			void
 */
void UnloadGameList(list_t * list)
{
	list_node_t *node, *nc;
	save_entry_t *item;
	code_entry_t *code;

	for (node = list_head(list); (item = list_get(node)); node = list_next(node))
	{
		if (item->name)
		{
			free(item->name);
			item->name = NULL;
		}

		if (item->path)
		{
			free(item->path);
			item->path = NULL;
		}

		if (item->dir_name)
		{
			free(item->dir_name);
			item->dir_name = NULL;
		}

		if (item->title_id)
		{
			free(item->title_id);
			item->title_id = NULL;
		}
		
		if (item->codes)
		{
			for (nc = list_head(item->codes); (code = list_get(nc)); nc = list_next(nc))
			{
				if (code->codes)
				{
					free (code->codes);
					code->codes = NULL;
				}
				if (code->name)
				{
					free (code->name);
					code->name = NULL;
				}
				if (code->options && code->options_count > 0)
				{
					for (int z = 0; z < code->options_count; z++)
					{
						if (code->options[z].line)
							free(code->options[z].line);
						if (code->options[z].name)
							free(code->options[z].name);
						if (code->options[z].value)
							free(code->options[z].value);
					}
					
					free (code->options);
				}
			}
			
			list_free(item->codes);
			item->codes = NULL;
		}
	}

	list_free(list);
	
	LOG("UnloadGameList() :: Successfully unloaded game list");
}

int sortCodeList_Compare(const void* a, const void* b)
{
	return strcasecmp(((code_entry_t*) a)->name, ((code_entry_t*) b)->name);
}

/*
 * Function:		qsortSaveList_Compare()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Compares two game_entry for QuickSort
 * Arguments:
 *	a:				First code
 *	b:				Second code
 * Return:			1 if greater, -1 if less, or 0 if equal
 */
int sortSaveList_Compare(const void* a, const void* b)
{
	return strcasecmp(((save_entry_t*) a)->name, ((save_entry_t*) b)->name);
}

static void read_usb_encrypted_saves(const char* userPath, list_t *list, uint64_t account)
{
	DIR *d, *d2;
	struct dirent *dir, *dir2;
	save_entry_t *item;
	char savePath[256];

	d = opendir(userPath);

	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (dir->d_stat.st_mode & SCE_S_IFDIR || strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
			continue;

		snprintf(savePath, sizeof(savePath), "%s%s", userPath, dir->d_name);
		d2 = opendir(savePath);

		if (!d2)
			continue;

		LOG("Reading %s...", savePath);

		while ((dir2 = readdir(d2)) != NULL)
		{
			if (!(dir2->d_stat.st_mode & SCE_S_IFREG) || endsWith(dir2->d_name, ".bin"))
				continue;

			snprintf(savePath, sizeof(savePath), "%s%s/%s.bin", userPath, dir->d_name, dir2->d_name);
			if (file_exists(savePath) != SUCCESS)
				continue;

			snprintf(savePath, sizeof(savePath), "(Encrypted) %s/%s", dir->d_name, dir2->d_name);
			item = _createSaveEntry(SAVE_FLAG_PSV | SAVE_FLAG_LOCKED, savePath);
			item->type = FILE_TYPE_PSV;

			asprintf(&item->path, "%s%s/", userPath, dir->d_name);
			asprintf(&item->title_id, "%.9s", dir->d_name);
			item->dir_name = strdup(dir2->d_name);

			if (apollo_config.account_id == account)
				item->flags |= SAVE_FLAG_OWNER;

			snprintf(savePath, sizeof(savePath), "%s%s/%s", userPath, dir->d_name, dir2->d_name);
			
			uint64_t size = 0;
			get_file_size(savePath, &size);
//			item->blocks = size / ORBIS_SAVE_DATA_BLOCK_SIZE;

			LOG("[%s] F(%d) name '%s'", item->title_id, item->flags, item->name);
			list_append(list, item);

		}
		closedir(d2);
	}

	closedir(d);
}

static void read_psp_savegames(const char* userPath, list_t *list)
{
	DIR *d;
	struct dirent *dir;
	save_entry_t *item;
	char sfoPath[256];

	d = opendir(userPath);

	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (!(dir->d_stat.st_mode & SCE_S_IFDIR) || strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
			continue;

		snprintf(sfoPath, sizeof(sfoPath), "%s%s/PARAM.SFO", userPath, dir->d_name);
		if (file_exists(sfoPath) != SUCCESS)
			continue;

		LOG("Reading %s...", sfoPath);
		sfo_context_t* sfo = sfo_alloc();
		if (sfo_read(sfo, sfoPath) < 0) {
			LOG("Unable to read from '%s'", sfoPath);
			sfo_free(sfo);
			continue;
		}

		item = _createSaveEntry(SAVE_FLAG_PSP, (char*) sfo_get_param_value(sfo, "TITLE"));
		item->type = FILE_TYPE_PSP;
		item->dir_name = strdup((char*) sfo_get_param_value(sfo, "SAVEDATA_DIRECTORY"));
		asprintf(&item->title_id, "%.9s", item->dir_name);
		asprintf(&item->path, "%s%s/", userPath, dir->d_name);

		sfo_free(sfo);
		LOG("[%s] F(%d) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	closedir(d);
}

static void read_usb_savegames(const char* userPath, list_t *list, sqlite3 *db)
{
	DIR *d;
	struct dirent *dir;
	save_entry_t *item;
	char sfoPath[256];

	d = opendir(userPath);

	if (!d)
		return;

	while ((dir = readdir(d)) != NULL)
	{
		if (!(dir->d_stat.st_mode & SCE_S_IFDIR) || strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
			continue;

		snprintf(sfoPath, sizeof(sfoPath), "%s%s/sce_sys/param.sfo", userPath, dir->d_name);
		if (file_exists(sfoPath) != SUCCESS)
			continue;

		LOG("Reading %s...", sfoPath);

		sfo_context_t* sfo = sfo_alloc();
		if (sfo_read(sfo, sfoPath) < 0) {
			LOG("Unable to read from '%s'", sfoPath);
			sfo_free(sfo);
			continue;
		}

		char *sfo_data = (char*)(sfo_get_param_value(sfo, "PARAMS") + 0x28);
		item = _createSaveEntry(SAVE_FLAG_PSV, get_appdb_title(db, sfo_data, sfoPath) ? sfoPath : sfo_data);
		item->type = FILE_TYPE_PSV;
		asprintf(&item->path, "%s%s/", userPath, dir->d_name);
		asprintf(&item->title_id, "%.9s", sfo_data);

		sfo_data = (char*)(sfo_get_param_value(sfo, "PARENT_DIRECTORY") + 1);
		item->dir_name = strdup(sfo_data);

		uint64_t* int_data = (uint64_t*) sfo_get_param_value(sfo, "ACCOUNT_ID");
		if (int_data && (apollo_config.account_id == *int_data))
			item->flags |= SAVE_FLAG_OWNER;

		sfo_free(sfo);
			
		LOG("[%s] F(%d) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	closedir(d);
}

static void read_hdd_savegames(const char* userPath, list_t *list)
{
	char sfoPath[256];
	save_entry_t *item;
	sqlite3_stmt *res;
	sqlite3 *db = open_sqlite_db(userPath);

	if (!db)
		return;

	int rc = sqlite3_prepare_v2(db, "SELECT a.titleId, val, title, iconPath FROM tbl_appinfo_icon AS a, tbl_appinfo AS b "
		" WHERE (type = 0) AND (a.titleId = b.titleId) AND (a.titleid NOT LIKE 'NPX%') AND (key = 278217076)", -1, &res, NULL);
	if (rc != SQLITE_OK)
	{
		LOG("Failed to fetch data: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		return;
	}

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		item = _createSaveEntry(SAVE_FLAG_PSV | SAVE_FLAG_HDD, (const char*) sqlite3_column_text(res, 2));
		item->type = FILE_TYPE_PSV;
		item->dir_name = strdup((const char*) sqlite3_column_text(res, 1));
		item->title_id = strdup((const char*) sqlite3_column_text(res, 0));
		item->blocks = 1; //sqlite3_column_int(res, 3);
		asprintf(&item->path, APOLLO_SANDBOX_PATH, item->dir_name);

		sfo_context_t* sfo = sfo_alloc();
		snprintf(sfoPath, sizeof(sfoPath), APOLLO_SANDBOX_PATH "sce_sys/param.sfo", item->dir_name);
		if (file_exists(sfoPath) == SUCCESS && sfo_read(sfo, sfoPath) == SUCCESS)
		{
			uint64_t* int_data = (uint64_t*) sfo_get_param_value(sfo, "ACCOUNT_ID");
			if (int_data && (apollo_config.account_id == *int_data))
				item->flags |= SAVE_FLAG_OWNER;
		}
		sfo_free(sfo);

		LOG("[%s] F(%d) {%d} '%s'", item->title_id, item->flags, item->blocks, item->name);
		list_append(list, item);
	}

	sqlite3_finalize(res);
	sqlite3_close(db);
}

/*
 * Function:		ReadUserList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Reads the entire userlist folder into a game_entry array
 * Arguments:
 *	gmc:			Set as the number of games read
 * Return:			Pointer to array of game_entry, null if failed
 */
list_t * ReadUsbList(const char* userPath)
{
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;
	sqlite3* appdb;

	if (dir_exists(userPath) != SUCCESS)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PSV, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	item->path = strdup(userPath);
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Resign selected Saves", CMD_RESIGN_SAVES);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_SIGN " Resign all decrypted Saves", CMD_RESIGN_ALL_SAVES);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy selected Saves to User Storage (ux0:user/)", CMD_COPY_SAVES_HDD);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy all decrypted Saves to User Storage (ux0:user/)", CMD_COPY_ALL_SAVES_HDD);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Start local Web Server", CMD_RUN_WEBSERVER);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Dump all decrypted Save Fingerprints", CMD_DUMP_FINGERPRINTS);
	list_append(item->codes, cmd);
	list_append(list, item);

	appdb = open_sqlite_db(USER_PATH_HDD);
	read_usb_savegames(userPath, list, appdb);
	read_psp_savegames(userPath, list);
	sqlite3_close(appdb);

	return list;
}

list_t * ReadUserList(const char* userPath)
{
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;

	if (file_exists(userPath) != SUCCESS)
		return NULL;

	list = list_alloc();

	item = _createSaveEntry(SAVE_FLAG_PSV, CHAR_ICON_COPY " Bulk Save Management");
	item->type = FILE_TYPE_MENU;
	item->codes = list_alloc();
	item->path = strdup(userPath);
	//bulk management hack
	item->dir_name = malloc(sizeof(void**));
	((void**)item->dir_name)[0] = list;

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy selected Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(2, "Copy Saves to Backup Storage", CMD_COPY_SAVES_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Copy all Saves to Backup Storage", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(2, "Copy Saves to Backup Storage", CMD_COPY_ALL_SAVES_USB);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Start local Web Server", CMD_RUN_WEBSERVER);
	list_append(item->codes, cmd);

	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_LOCK " Dump all Save Fingerprints", CMD_DUMP_FINGERPRINTS);
	list_append(item->codes, cmd);
	list_append(list, item);

	read_hdd_savegames(userPath, list);
	read_psp_savegames(PSP_SAVES_PATH_HDD, list);

	return list;
}

/*
 * Function:		ReadOnlineList()
 * File:			saves.c
 * Project:			Apollo PS3
 * Description:		Downloads the entire gamelist file into a game_entry array
 * Arguments:
 *	gmc:			Set as the number of games read
 * Return:			Pointer to array of game_entry, null if failed
 */
static void _ReadOnlineListEx(const char* urlPath, uint16_t flag, list_t *list)
{
	save_entry_t *item;
	char path[256];

	snprintf(path, sizeof(path), APOLLO_LOCAL_CACHE "%04X_games.txt", flag);

	if (file_exists(path) == SUCCESS)
	{
		struct stat stats;
		stat(path, &stats);
		// re-download if file is +1 day old
		if ((stats.st_mtime + ONLINE_CACHE_TIMEOUT) < time(NULL))
			http_download(urlPath, "games.txt", path, 0);
	}
	else
	{
		if (!http_download(urlPath, "games.txt", path, 0))
			return;
	}
	
	long fsize;
	char *data = readTextFile(path, &fsize);
	
	char *ptr = data;
	char *end = data + fsize + 1;

	while (ptr < end && *ptr)
	{
		char *tmp, *content = ptr;

		while (ptr < end && *ptr != '\n' && *ptr != '\r')
		{
			ptr++;
		}
		*ptr++ = 0;

		if ((tmp = strchr(content, '=')) != NULL)
		{
			*tmp++ = 0;
			item = _createSaveEntry(flag | SAVE_FLAG_ONLINE, tmp);
			item->title_id = strdup(content);
			asprintf(&item->path, "%s%s/", urlPath, item->title_id);

			LOG("+ [%s] %s", item->title_id, item->name);
			list_append(list, item);
		}

		if (ptr < end && *ptr == '\r')
		{
			ptr++;
		}
		if (ptr < end && *ptr == '\n')
		{
			ptr++;
		}
	}

	if (data) free(data);
}

list_t * ReadOnlineList(const char* urlPath)
{
	char url[256];
	list_t *list = list_alloc();

	// PSV save-games (Zip folder)
	snprintf(url, sizeof(url), "%s" "PSV/", urlPath);
	_ReadOnlineListEx(url, SAVE_FLAG_PSV, list);

	// PSP save-games (Zip folder)
	snprintf(url, sizeof(url), "%sPSP/", urlPath);
	_ReadOnlineListEx(url, SAVE_FLAG_PSP, list);

/*
	// PS1 save-games (Zip PSV)
	//snprintf(url, sizeof(url), "%s" "PS1/", urlPath);
	//_ReadOnlineListEx(url, SAVE_FLAG_PS1, list);
*/

	if (!list_count(list))
	{
		list_free(list);
		return NULL;
	}

	return list;
}

static int sqlite_trophy_collate(void *foo, int ll, const void *l, int rl, const void *r)
{
    return 0;
}

list_t * ReadTrophyList(const char* userPath)
{
	save_entry_t *item;
	code_entry_t *cmd;
	list_t *list;
	sqlite3 *db;
	sqlite3_stmt *res;

	if ((db = open_sqlite_db(userPath)) == NULL)
		return NULL;

	list = list_alloc();
/*
	item = _createSaveEntry(SAVE_FLAG_PS4, CHAR_ICON_COPY " Export Trophies");
	item->type = FILE_TYPE_MENU;
	item->path = strdup(userPath);
	item->codes = list_alloc();
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_COPY " Backup Trophies to USB", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(2, "Save Trophies to USB", CMD_COPY_TROPHIES_USB);
	list_append(item->codes, cmd);
	cmd = _createCmdCode(PATCH_COMMAND, CHAR_ICON_ZIP " Export Trophies to .Zip", CMD_CODE_NULL);
	cmd->options_count = 1;
	cmd->options = _createOptions(2, "Save .Zip to USB", CMD_ZIP_TROPHY_USB);
	list_append(item->codes, cmd);
	list_append(list, item);
*/
	sqlite3_create_collation(db, "trophy_collator", SQLITE_UTF8, NULL, &sqlite_trophy_collate);
	int rc = sqlite3_prepare_v2(db, "SELECT id, npcommid, title FROM tbl_trophy_title WHERE status = 0", -1, &res, NULL);
	if (rc != SQLITE_OK)
	{
		LOG("Failed to fetch data: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		item = _createSaveEntry(SAVE_FLAG_PSV | SAVE_FLAG_TROPHY, (const char*) sqlite3_column_text(res, 2));
		item->blocks = sqlite3_column_int(res, 0);
		item->path = strdup(userPath);
		item->title_id = strdup((const char*) sqlite3_column_text(res, 1));
		item->type = FILE_TYPE_TRP;

		LOG("[%s] F(%d) name '%s'", item->title_id, item->flags, item->name);
		list_append(list, item);
	}

	sqlite3_finalize(res);
	sqlite3_close(db);

	return list;
}

int get_save_details(const save_entry_t* save, char **details)
{
	char sfoPath[256];
	char mount[32] = "";
	sqlite3 *db;
	sqlite3_stmt *res;
	sdslot_dat_t* sdslot;
	size_t size;

	if (save->flags & SAVE_FLAG_PSP)
	{
		snprintf(sfoPath, sizeof(sfoPath), "%sPARAM.SFO", save->path);
		LOG("Save Details :: Reading %s...", sfoPath);

		sfo_context_t* sfo = sfo_alloc();
		if (sfo_read(sfo, sfoPath) < 0) {
			LOG("Unable to read from '%s'", sfoPath);
			sfo_free(sfo);
			return 0;
		}

		asprintf(details, "%s\n----- PSP Save -----\n"
			"Game: %s\n"
			"Title ID: %s\n"
			"Folder: %s\n"
			"Title: %s\n"
			"Details: %s\n",
			save->path,
			save->name,
			save->title_id,
			save->dir_name,
			(char*)sfo_get_param_value(sfo, "SAVEDATA_TITLE"),
			(char*)sfo_get_param_value(sfo, "SAVEDATA_DETAIL"));

		sfo_free(sfo);
		return 1;
	}

	if (!(save->flags & SAVE_FLAG_PSV))
	{
		asprintf(details, "%s\n\nTitle: %s\n", save->path, save->name);
		return 1;
	}

	if (save->flags & SAVE_FLAG_TROPHY)
	{
		if ((db = open_sqlite_db(save->path)) == NULL)
			return 0;

		char* query = sqlite3_mprintf("SELECT id, description, trophy_num, unlocked_trophy_num, progress,"
			"platinum_num, unlocked_platinum_num, gold_num, unlocked_gold_num, silver_num, unlocked_silver_num,"
			"bronze_num, unlocked_bronze_num FROM tbl_trophy_title WHERE id = %d", save->blocks);

		if (sqlite3_prepare_v2(db, query, -1, &res, NULL) != SQLITE_OK || sqlite3_step(res) != SQLITE_ROW)
		{
			LOG("Failed to fetch data: %s", sqlite3_errmsg(db));
			sqlite3_free(query);
			sqlite3_close(db);
			return 0;
		}

		asprintf(details, "Trophy-Set Details\n\n"
			"Title: %s\n"
			"Description: %s\n"
			"NP Comm ID: %s\n"
			"Progress: %d/%d - %d%%\n"
			"%c Platinum: %d/%d\n"
			"%c Gold: %d/%d\n"
			"%c Silver: %d/%d\n"
			"%c Bronze: %d/%d\n",
			save->name, sqlite3_column_text(res, 1), save->title_id,
			sqlite3_column_int(res, 3), sqlite3_column_int(res, 2), sqlite3_column_int(res, 4),
			CHAR_TRP_PLATINUM, sqlite3_column_int(res, 6), sqlite3_column_int(res, 5),
			CHAR_TRP_GOLD, sqlite3_column_int(res, 8), sqlite3_column_int(res, 7),
			CHAR_TRP_SILVER, sqlite3_column_int(res, 10), sqlite3_column_int(res, 9),
			CHAR_TRP_BRONZE, sqlite3_column_int(res, 12), sqlite3_column_int(res, 11));

		sqlite3_free(query);
		sqlite3_finalize(res);
		sqlite3_close(db);

		return 1;
	}

	if(save->flags & SAVE_FLAG_LOCKED)
	{
		asprintf(details, "%s\n\n"
			"Title ID: %s\n"
			"Dir Name: %s\n"
			"Blocks: %d\n"
			"Account ID: %.16s\n",
			save->path,
			save->title_id,
			save->dir_name,
			save->blocks,
			save->path + 23);

		return 1;
	}

	if(save->flags & SAVE_FLAG_HDD)
		vita_SaveMount(save, mount);

	snprintf(sfoPath, sizeof(sfoPath), "%ssce_sys/param.sfo", save->path);
	LOG("Save Details :: Reading %s...", sfoPath);

	sfo_context_t* sfo = sfo_alloc();
	if (sfo_read(sfo, sfoPath) < 0) {
		LOG("Unable to read from '%s'", sfoPath);
		sfo_free(sfo);
		return 0;
	}

	strcpy(strrchr(sfoPath, '/'), "/sdslot.dat");
	LOG("Save Details :: Reading %s...", sfoPath);
	if (read_buffer(sfoPath, (uint8_t**) &sdslot, &size) != SUCCESS) {
		LOG("Unable to read from '%s'", sfoPath);
		sfo_free(sfo);
		return 0;
	}

	if (sdslot->header.magic == 0x4C534453)
		memcpy(sfoPath, sdslot->header.active_slots, sizeof(sfoPath));
	else
		memset(sfoPath, 0, sizeof(sfoPath));

	char* out = *details = (char*) sdslot;
	uint64_t* account_id = (uint64_t*) sfo_get_param_value(sfo, "ACCOUNT_ID");

	out += sprintf(out, "%s\n----- Save -----\n"
		"Title: %s\n"
		"Title ID: %s\n"
		"Dir Name: %s\n"
		"Account ID: %016llx\n",
		save->path, save->name,
		save->title_id,
		save->dir_name,
		*account_id);

	for (int i = 0; (i < 256) && sfoPath[i]; i++)
	{
		out += sprintf(out, "----- Slot %03d -----\n"
			"Title: %s\n"
			"Subtitle: %s\n"
			"Detail: %s\n",
			i, sdslot->slots[i].title,
			sdslot->slots[i].subtitle,
			sdslot->slots[i].description);
	}

	if(save->flags & SAVE_FLAG_HDD)
		vita_SaveUmount(mount);

	sfo_free(sfo);
	return 1;
}
