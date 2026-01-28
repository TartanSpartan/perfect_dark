#include <vitasdk.h>
#include <vitaGL.h>
#include <stdio.h>

#define DEBUG

#ifndef PD_VITA_COMM_ID
#define PD_VITA_COMM_ID "PDXP00001"
#endif

static char comm_id[12] = {0};
static char signature[160] = {0xb9, 0xdd, 0xe1, 0x3b, 0x01, 0x00};

static int trp_ctx;
static int plat_id = -1;

typedef struct
{
	int sdkVersion;
	SceCommonDialogParam commonParam;
	int context;
	int options;
	uint8_t reserved[128];
} SceNpTrophySetupDialogParam;

typedef struct
{
	uint32_t unk[4];
} SceNpTrophyUnlockState;
SceNpTrophyUnlockState trophies_unlocks;

int sceNpTrophyInit(void *unk);
int sceNpTrophyCreateContext(int *context, char *commId, char *commSign, uint64_t options);
int sceNpTrophySetupDialogInit(SceNpTrophySetupDialogParam *param);
SceCommonDialogStatus sceNpTrophySetupDialogGetStatus();
int sceNpTrophySetupDialogTerm();
int sceNpTrophyCreateHandle(int *handle);
int sceNpTrophyDestroyHandle(int handle);
int sceNpTrophyUnlockTrophy(int ctx, int handle, int id, int *plat_id);
int sceNpTrophyGetTrophyUnlockState(int ctx, int handle, SceNpTrophyUnlockState *state, uint32_t *count);

int trophies_available = 0;

#define TRP_QUEUE_SIZE 32
// Simple circular queue for pending trophy unlock requests.
// Using a queue (instead of a single shared `trp_id` variable) avoids
// a race where multiple trophies unlocked in quick succession would
// overwrite each other and only the last one would be processed.
// This ensures that multiple trophies can be unlocked one after another
// if their unlock conditions are met for the same mission
static volatile int trp_queue[TRP_QUEUE_SIZE];
static volatile int trp_queue_head = 0; // next position to write
static volatile int trp_queue_tail = 0; // next position to read
SceUID trp_request_mutex; // semaphore counting pending items
SceUID trp_queue_mutex;   // mutex protecting head/tail access

int trophies_unlocker(SceSize args, void *argp)
{
	for (;;)
	{
		sceKernelWaitSema(trp_request_mutex, 1, NULL);
		
		sceKernelWaitSema(trp_queue_mutex, 1, NULL);
		int local_trp_id = trp_queue[trp_queue_tail];
		trp_queue_tail = (trp_queue_tail + 1) % TRP_QUEUE_SIZE;
		sceKernelSignalSema(trp_queue_mutex, 1);
		
		int trp_handle;
		sceNpTrophyCreateHandle(&trp_handle);
		sceNpTrophyUnlockTrophy(trp_ctx, trp_handle, local_trp_id, &plat_id);
		sceNpTrophyDestroyHandle(trp_handle);
        
		// Wait a short time between pops so the UI has time to display
		// each trophy notification one after another. Delay is in microseconds.
		sceKernelDelayThread(2000000); // 2000 ms (2 seconds)
	}
}

int trophies_init()
{
	// Starting sceNpTrophy (experimental ID to avoid clashes with main branch ID)
	snprintf(comm_id, sizeof(comm_id), "%s", PD_VITA_COMM_ID);
	sceSysmoduleLoadModule(SCE_SYSMODULE_NP_TROPHY);
	sceNpTrophyInit(NULL);
	int res = sceNpTrophyCreateContext(&trp_ctx, comm_id, signature, 0);
	if (res < 0)
	{
#ifdef DEBUG
		printf("sceNpTrophyCreateContext returned 0x%08X\n", res);
#endif
		return res;
	}
	SceNpTrophySetupDialogParam setupParam;
	sceClibMemset(&setupParam, 0, sizeof(SceNpTrophySetupDialogParam));
	_sceCommonDialogSetMagicNumber(&setupParam.commonParam);
	setupParam.sdkVersion = PSP2_SDK_VERSION;
	setupParam.options = 0;
	setupParam.context = trp_ctx;
	sceNpTrophySetupDialogInit(&setupParam);
	static int trophy_setup = SCE_COMMON_DIALOG_STATUS_RUNNING;
	while (trophy_setup == SCE_COMMON_DIALOG_STATUS_RUNNING)
	{
		trophy_setup = sceNpTrophySetupDialogGetStatus();
		vglSwapBuffers(GL_TRUE);
	}
	sceNpTrophySetupDialogTerm();

	// Starting trophy unlocker thread
	trp_queue_mutex = sceKernelCreateSema("trps queue", 0, 1, 1, NULL);
	trp_request_mutex = sceKernelCreateSema("trps request", 0, 0, TRP_QUEUE_SIZE, NULL);
	SceUID tropies_unlocker_thd = sceKernelCreateThread("trophies unlocker", &trophies_unlocker, 0x10000100, 0x10000, 0, 0, NULL);
	sceKernelStartThread(tropies_unlocker_thd, 0, NULL);

	// Getting current trophy unlocks state
	int trp_handle;
	uint32_t dummy;
	sceNpTrophyCreateHandle(&trp_handle);
	sceNpTrophyGetTrophyUnlockState(trp_ctx, trp_handle, &trophies_unlocks, &dummy);
	sceNpTrophyDestroyHandle(trp_handle);

	trophies_available = 1;
	return res;
}

uint8_t trophies_is_unlocked(uint32_t id)
{
	if (trophies_available)
	{
		return (trophies_unlocks.unk[id >> 5] & (1 << (id & 31))) > 0;
	}
	return 0;
}

void trophies_unlock(uint32_t id)
{
	if (trophies_available && !trophies_is_unlocked(id))
	{
		trophies_unlocks.unk[id >> 5] |= (1 << (id & 31));
		// Enqueue the trophy id for the unlocker thread to process.
		// Protect head/tail with a small mutex to avoid concurrent writes
		// from multiple threads calling `trophies_unlock` at once.
		sceKernelWaitSema(trp_queue_mutex, 1, NULL);
		trp_queue[trp_queue_head] = id;
		trp_queue_head = (trp_queue_head + 1) % TRP_QUEUE_SIZE;
		sceKernelSignalSema(trp_queue_mutex, 1);

		// Signal the unlocker that there is at least one pending trophy.
		sceKernelSignalSema(trp_request_mutex, 1);
	}
}
