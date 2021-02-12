#include "global.h"
#include "item.h"
#include "field_specials.h"
#include "event_object_movement.h"
#include "overworld.h"
#include "pokedex.h"
#include "script.h"
#include "sound.h"
#include "event_data.h"
#include "quest_log.h"
#include "constants/items.h"
#include "constants/map_scripts.h"

#define RAM_SCRIPT_MAGIC 51
#define SCRIPT_STACK_SIZE 20


extern void ResetContextNpcTextColor(void); // field_specials
extern u16 CalcCRC16WithTable(u8 *data, int length); // util
extern bool32 ValidateReceivedWonderCard(void); // mevent

enum
{
    SCRIPT_MODE_STOPPED,
    SCRIPT_MODE_BYTECODE,
    SCRIPT_MODE_NATIVE,
};

EWRAM_DATA u8 gWalkAwayFromSignInhibitTimer = 0;
EWRAM_DATA const u8 *gRAMScriptPtr = NULL;

// iwram bss
static u8 sScriptContext1Status;
static u32 sUnusedVariable1;
static struct ScriptContext sScriptContext1;
static u32 sUnusedVariable2;
static struct ScriptContext sScriptContext2;
static bool8 sScriptContext2Enabled;
static u8 sMsgBoxWalkawayDisabled;
static u8 sMsgBoxIsCancelable;
static u8 sQuestLogInput;
static u8 sQuestLogInputIsDpad;
static u8 sMsgIsSignPost;

extern ScrCmdFunc gScriptCmdTable[];
extern ScrCmdFunc gScriptCmdTableEnd[];
extern void *gNullScriptPtr;

void InitScriptContext(struct ScriptContext *ctx, void *cmdTable, void *cmdTableEnd)
{
    s32 i;

    ctx->mode = SCRIPT_MODE_STOPPED;
    ctx->scriptPtr = NULL;
    ctx->stackDepth = 0;
    ctx->nativePtr = NULL;
    ctx->cmdTable = cmdTable;
    ctx->cmdTableEnd = cmdTableEnd;

    for (i = 0; i < 4; i++)
        ctx->data[i] = 0;

    for (i = 0; i < SCRIPT_STACK_SIZE; i++)
        ctx->stack[i] = 0;
}

u8 SetupBytecodeScript(struct ScriptContext *ctx, const u8 *ptr)
{
    ctx->scriptPtr = ptr;
    ctx->mode = SCRIPT_MODE_BYTECODE;
    return 1;
}

void SetupNativeScript(struct ScriptContext *ctx, bool8 (*ptr)(void))
{
    ctx->mode = SCRIPT_MODE_NATIVE;
    ctx->nativePtr = ptr;
}

void StopScript(struct ScriptContext *ctx)
{
    ctx->mode = SCRIPT_MODE_STOPPED;
    ctx->scriptPtr = NULL;
}

bool8 RunScriptCommand(struct ScriptContext *ctx)
{
    // FRLG disabled this check, where-as it is present
    // in Ruby/Sapphire and Emerald. Why did the programmers
    // bother to remove a redundant check when it still
    // exists in Emerald?
    //if (ctx->mode == SCRIPT_MODE_STOPPED)
    //    return FALSE;

    switch (ctx->mode)
    {
    case SCRIPT_MODE_STOPPED:
        return FALSE;
    case SCRIPT_MODE_NATIVE:
        if (ctx->nativePtr)
        {
            if (ctx->nativePtr() == TRUE)
                ctx->mode = SCRIPT_MODE_BYTECODE;
            return TRUE;
        }
        ctx->mode = SCRIPT_MODE_BYTECODE;
    case SCRIPT_MODE_BYTECODE:
        while (1)
        {
            u8 cmdCode;
            ScrCmdFunc *cmdFunc;

            if (ctx->scriptPtr == NULL)
            {
                ctx->mode = SCRIPT_MODE_STOPPED;
                return FALSE;
            }

            if (ctx->scriptPtr == gNullScriptPtr)
            {
                while (1)
                    asm("svc 2"); // HALT
            }

            cmdCode = *(ctx->scriptPtr);
            ctx->scriptPtr++;
            cmdFunc = &ctx->cmdTable[cmdCode];

            if (cmdFunc >= ctx->cmdTableEnd)
            {
                ctx->mode = SCRIPT_MODE_STOPPED;
                return FALSE;
            }

            if ((*cmdFunc)(ctx) == TRUE)
                return TRUE;
        }
    }

    return TRUE;
}

u8 ScriptPush(struct ScriptContext *ctx, const u8 *ptr)
{
    if (ctx->stackDepth + 1 >= SCRIPT_STACK_SIZE)
    {
        return 1;
    }
    else
    {
        ctx->stack[ctx->stackDepth] = ptr;
        ctx->stackDepth++;
        return 0;
    }
}

const u8 *ScriptPop(struct ScriptContext *ctx)
{
    if (ctx->stackDepth == 0)
        return NULL;

    ctx->stackDepth--;
    return ctx->stack[ctx->stackDepth];
}

void ScriptJump(struct ScriptContext *ctx, const u8 *ptr)
{
    ctx->scriptPtr = ptr;
}

void ScriptCall(struct ScriptContext *ctx, const u8 *ptr)
{
    ScriptPush(ctx, ctx->scriptPtr);
    ctx->scriptPtr = ptr;
}

void ScriptReturn(struct ScriptContext *ctx)
{
    ctx->scriptPtr = ScriptPop(ctx);
}

u16 ScriptReadHalfword(struct ScriptContext *ctx)
{
    u16 value = *(ctx->scriptPtr++);
    value |= *(ctx->scriptPtr++) << 8;
    return value;
}

u32 ScriptReadWord(struct ScriptContext *ctx)
{
    u32 value0 = *(ctx->scriptPtr++);
    u32 value1 = *(ctx->scriptPtr++);
    u32 value2 = *(ctx->scriptPtr++);
    u32 value3 = *(ctx->scriptPtr++);
    return (((((value3 << 8) + value2) << 8) + value1) << 8) + value0;
}

void ScriptContext2_Enable(void)
{
    sScriptContext2Enabled = TRUE;
}

void ScriptContext2_Disable(void)
{
    sScriptContext2Enabled = FALSE;
}

bool8 ScriptContext2_IsEnabled(void)
{
    return sScriptContext2Enabled;
}

void SetQuestLogInputIsDpadFlag(void)
{
    sQuestLogInputIsDpad = TRUE;
}

void ClearQuestLogInputIsDpadFlag(void)
{
    sQuestLogInputIsDpad = FALSE;
}

bool8 IsQuestLogInputDpad(void)
{
    if(sQuestLogInputIsDpad == TRUE)
        return TRUE;
    else
        return FALSE;
}

void RegisterQuestLogInput(u8 var)
{
    sQuestLogInput = var;
}

void ClearQuestLogInput(void)
{
    sQuestLogInput = 0;
}

u8 GetRegisteredQuestLogInput(void)
{
    return sQuestLogInput;
}

void DisableMsgBoxWalkaway(void)
{
    sMsgBoxWalkawayDisabled = TRUE;
}

void EnableMsgBoxWalkaway(void)
{
    sMsgBoxWalkawayDisabled = FALSE;
}

bool8 IsMsgBoxWalkawayDisabled(void)
{
    return sMsgBoxWalkawayDisabled;
}

void SetWalkingIntoSignVars(void)
{
    gWalkAwayFromSignInhibitTimer = 6;
    sMsgBoxIsCancelable = TRUE;
}

void ClearMsgBoxCancelableState(void)
{
    sMsgBoxIsCancelable = FALSE;
}

bool8 CanWalkAwayToCancelMsgBox(void)
{
    if(sMsgBoxIsCancelable == TRUE)
        return TRUE;
    else
        return FALSE;
}

void MsgSetSignPost(void)
{
    sMsgIsSignPost = TRUE;
}

void MsgSetNotSignPost(void)
{
    sMsgIsSignPost = FALSE;
}

bool8 IsMsgSignPost(void)
{
    if(sMsgIsSignPost == TRUE)
        return TRUE;
    else
        return FALSE;
}

void ResetFacingNpcOrSignPostVars(void)
{
    ResetContextNpcTextColor();
    MsgSetNotSignPost();
}

bool8 ScriptContext1_IsScriptSetUp(void)
{
    if (sScriptContext1Status == 0)
        return TRUE;
    else
        return FALSE;
}

void ScriptContext1_Init(void)
{
    InitScriptContext(&sScriptContext1, gScriptCmdTable, gScriptCmdTableEnd);
    sScriptContext1Status = 2;
}

bool8 ScriptContext2_RunScript(void)
{
    if (sScriptContext1Status == 2)
        return 0;

    if (sScriptContext1Status == 1)
        return 0;

    ScriptContext2_Enable();

    if (!RunScriptCommand(&sScriptContext1))
    {
        sScriptContext1Status = 2;
        ScriptContext2_Disable();
        return 0;
    }

    return 1;
}

void ScriptContext1_SetupScript(const u8 *ptr)
{
    ClearMsgBoxCancelableState();
    EnableMsgBoxWalkaway();
    ClearQuestLogInputIsDpadFlag();
    InitScriptContext(&sScriptContext1, gScriptCmdTable, gScriptCmdTableEnd);
    SetupBytecodeScript(&sScriptContext1, ptr);
    ScriptContext2_Enable();
    sScriptContext1Status = 0;
}

void ScriptContext1_Stop(void)
{
    sScriptContext1Status = 1;
}

void EnableBothScriptContexts(void)
{
    sScriptContext1Status = 0;
    ScriptContext2_Enable();
}

void ScriptContext2_RunNewScript(const u8 *ptr)
{
    InitScriptContext(&sScriptContext2, &gScriptCmdTable, &gScriptCmdTableEnd);
    SetupBytecodeScript(&sScriptContext2, ptr);
    while (RunScriptCommand(&sScriptContext2) == TRUE);
}

u8 *mapheader_get_tagged_pointer(u8 tag)
{
    const u8 *mapScripts = gMapHeader.mapScripts;

    if (mapScripts == NULL)
        return NULL;

    while (1)
    {
        if (*mapScripts == 0)
            return NULL;
        if (*mapScripts == tag)
        {
            mapScripts++;
            return T2_READ_PTR(mapScripts);
        }
        mapScripts += 5;
    }
}

void mapheader_run_script_by_tag(u8 tag)
{
    u8 *ptr = mapheader_get_tagged_pointer(tag);
    if (ptr != NULL)
        ScriptContext2_RunNewScript(ptr);
}

u8 *mapheader_get_first_match_from_tagged_ptr_list(u8 tag)
{
    u8 *ptr = mapheader_get_tagged_pointer(tag);

    if (ptr == NULL)
        return NULL;

    while (1)
    {
        u16 varIndex1;
        u16 varIndex2;
        varIndex1 = ptr[0] | (ptr[1] << 8);
        if (!varIndex1)
            return NULL;
        ptr += 2;
        varIndex2 = ptr[0] | (ptr[1] << 8);
        ptr += 2;
        if (VarGet(varIndex1) == VarGet(varIndex2))
            return (u8 *)(ptr[0] + (ptr[1] << 8) + (ptr[2] << 16) + (ptr[3] << 24));
        ptr += 4;
    }
}

void RunOnLoadMapScript(void)
{
    mapheader_run_script_by_tag(1);
}

void RunOnTransitionMapScript(void)
{
    mapheader_run_script_by_tag(3);
}

void RunOnResumeMapScript(void)
{
    mapheader_run_script_by_tag(5);
}

void RunOnReturnToFieldMapScript(void)
{
    mapheader_run_script_by_tag(7);
}

void RunOnDiveWarpMapScript(void)
{
    mapheader_run_script_by_tag(MAP_SCRIPT_ON_DIVE_WARP);
}

bool8 TryRunOnFrameMapScript(void)
{
    u8 *ptr;

    if(gQuestLogState == QL_STATE_PLAYBACK_LAST)
        return 0;

    ptr = mapheader_get_first_match_from_tagged_ptr_list(2);

    if (!ptr)
        return 0;

    ScriptContext1_SetupScript(ptr);
    return 1;
}

void TryRunOnWarpIntoMapScript(void)
{
    u8 *ptr = mapheader_get_first_match_from_tagged_ptr_list(4);
    if (ptr)
        ScriptContext2_RunNewScript(ptr);
}

u32 CalculateRamScriptChecksum(void)
{
    return CalcCRC16WithTable((u8*)(&gSaveBlock1Ptr->ramScript.data), sizeof(gSaveBlock1Ptr->ramScript.data));
}

void ClearRamScript(void)
{
    CpuFill32(0, &gSaveBlock1Ptr->ramScript, sizeof(struct RamScript));
}

bool8 InitRamScript(u8 *script, u16 scriptSize, u8 mapGroup, u8 mapNum, u8 objectId)
{
    struct RamScriptData *scriptData = &gSaveBlock1Ptr->ramScript.data;

    ClearRamScript();

    if (scriptSize > sizeof(scriptData->script))
        return FALSE;

    scriptData->magic = RAM_SCRIPT_MAGIC;
    scriptData->mapGroup = mapGroup;
    scriptData->mapNum = mapNum;
    scriptData->objectId = objectId;
    memcpy(scriptData->script, script, scriptSize);
    gSaveBlock1Ptr->ramScript.checksum = CalculateRamScriptChecksum();
    return TRUE;
}

const u8 *GetRamScript(u8 objectId, const u8 *script)
{
    struct RamScriptData *scriptData = &gSaveBlock1Ptr->ramScript.data;
    gRAMScriptPtr = NULL;
    if (scriptData->magic != RAM_SCRIPT_MAGIC)
        return script;
    if (scriptData->mapGroup != gSaveBlock1Ptr->location.mapGroup)
        return script;
    if (scriptData->mapNum != gSaveBlock1Ptr->location.mapNum)
        return script;
    if (scriptData->objectId != objectId)
        return script;
    if (CalculateRamScriptChecksum() != gSaveBlock1Ptr->ramScript.checksum)
    {
        ClearRamScript();
        return script;
    }
    else
    {
        gRAMScriptPtr = script;
        return scriptData->script;
    }
}

bool32 ValidateRamScript(void)
{
    struct RamScriptData *scriptData = &gSaveBlock1Ptr->ramScript.data;
    if (scriptData->magic != RAM_SCRIPT_MAGIC)
        return FALSE;
    if (scriptData->mapGroup != 0xFF)
        return FALSE;
    if (scriptData->mapNum != 0xFF)
        return FALSE;
    if (scriptData->objectId != 0xFF)
        return FALSE;
    if (CalculateRamScriptChecksum() != gSaveBlock1Ptr->ramScript.checksum)
        return FALSE;
    return TRUE;
}

u8 *sub_8069E48(void)
{
    struct RamScriptData *scriptData = &gSaveBlock1Ptr->ramScript.data;
    if (!ValidateReceivedWonderCard())
        return NULL;
    if (scriptData->magic != RAM_SCRIPT_MAGIC)
        return NULL;
    if (scriptData->mapGroup != 0xFF)
        return NULL;
    if (scriptData->mapNum != 0xFF)
        return NULL;
    if (scriptData->objectId != 0xFF)
        return NULL;
    if (CalculateRamScriptChecksum() != gSaveBlock1Ptr->ramScript.checksum)
    {
        ClearRamScript();
        return NULL;
    }
    else
    {
        return scriptData->script;
    }
}

void MEventSetRamScript(u8 *script, u16 scriptSize)
{
    if (scriptSize > sizeof(gSaveBlock1Ptr->ramScript.data.script))
        scriptSize = sizeof(gSaveBlock1Ptr->ramScript.data.script);
    InitRamScript(script, scriptSize, 0xFF, 0xFF, 0xFF);
}

void HandleUseExpiredRepel(void)
{
    VarSet(VAR_REPEL_STEP_COUNT, ItemId_GetHoldEffectParam(VarGet(VAR_LAST_REPEL_USED)));
}

void DetermineCeruleanCaveLayout(void)
{
    u32 trainerId = GetPlayerTrainerId();
    u8 result = trainerId % 3;
    gSpecialVar_Result = result;
}

void CheckTrainerCardStars(void)
{
    u8 stars = 0;

    if(FlagGet(FLAG_SYS_GAME_CLEAR))
    {
        stars++;
    }
    if(HasAllKantoMons())
    {
        stars++;
    }
    if(HasAllMons())
    {
        stars++;
    }
    if((gSaveBlock2Ptr->berryPick.berriesPicked >= 200 && gSaveBlock2Ptr->pokeJump.jumpsInRow >= 200) || gSaveBlock2Ptr->battleTower.bestBattleTowerWinStreak > 49)
    {
        stars++;
    }
    gSpecialVar_Result = stars;
}

#define HAS_TICKETS 16
#define NEEDS_SHOW_EON 17
#define NEEDS_SHOW_AURORA 18
#define NEEDS_SHOW_MYSTIC 19
#define NEEDS_SHOW_OLD_SEA_MAP 20
#define HAS_NO_TICKETS 21

void CheckEventTickets(void)
{
    bool8 haveEonTicket     = CheckBagHasItem(ITEM_EON_TICKET, 1);
    bool8 haveAuroraTicket  = CheckBagHasItem(ITEM_AURORA_TICKET, 1);
    bool8 haveMysticTicket  = CheckBagHasItem(ITEM_MYSTIC_TICKET, 1);
    bool8 haveOldSeaMap     = CheckBagHasItem(ITEM_OLD_SEA_MAP, 1);

    bool8 shownEonTicket    = FlagGet(FLAG_SHOWN_EON_TICKET);
    bool8 shownAuroraTicket = FlagGet(FLAG_SHOWN_AURORA_TICKET);
    bool8 shownMysticTicket = FlagGet(FLAG_SHOWN_MYSTIC_TICKET);
    bool8 shownOldSeaMap    = FlagGet(FLAG_SHOWN_OLD_SEA_MAP);

    u8 multichoiceCase = 0;

    if(gSpecialVar_Result == 0) //checking for showing tickets for the first time
    {
        if(shownEonTicket && shownAuroraTicket && shownMysticTicket && shownOldSeaMap)
        {
            gSpecialVar_Result = HAS_TICKETS;
            return;
        }
        if(haveEonTicket && !shownEonTicket)
        {
            gSpecialVar_Result = NEEDS_SHOW_EON;
            return;
        }
        if(haveAuroraTicket && !shownAuroraTicket)
        {
            gSpecialVar_Result = NEEDS_SHOW_AURORA;
            return;
        }
        if(haveMysticTicket && !shownMysticTicket)
        {
            gSpecialVar_Result = NEEDS_SHOW_MYSTIC;
            return;
        }
        if(haveOldSeaMap && !shownOldSeaMap)
        {
            gSpecialVar_Result = NEEDS_SHOW_OLD_SEA_MAP;
            return;
        }
        if(shownEonTicket || shownAuroraTicket || shownMysticTicket || shownOldSeaMap)
        {
            gSpecialVar_Result = HAS_TICKETS;
            return;
        }
        gSpecialVar_Result = HAS_NO_TICKETS;
        return;
    }
    if(gSpecialVar_Result == 1) //checking which multichoice combo to display
    {
        if(haveEonTicket && shownEonTicket)
        {
            multichoiceCase |= 1 << 3; //setting Eon bit
        }
        if(haveAuroraTicket && shownAuroraTicket)
        {
            multichoiceCase |= 1 << 2; //setting Aurora bit
        }
        if(haveMysticTicket && shownMysticTicket)
        {
            multichoiceCase |= 1 << 1; //setting Mystic bit
        }
        if(haveOldSeaMap && shownOldSeaMap)
        {
            multichoiceCase |= 1 << 0; //setting Old Sea Map bit
        }
        gSpecialVar_Result = multichoiceCase;
        return;
    }
    return;
}

#undef HAS_TICKETS
#undef NEEDS_SHOW_EON
#undef NEEDS_SHOW_AURORA
#undef NEEDS_SHOW_MYSTIC
#undef NEEDS_SHOW_OLD_SEA_MAP
#undef HAS_NO_TICKETS

void RecalculatePartyStats(void)
{
    u32 i;
    for (i = 0; i < gPlayerPartyCount; i++)
    {
        CalculateMonStats(&gPlayerParty[i], FALSE);
    }
}

void ResetTintFilter(void)
{
    u8 val = 0;
    gUnknown_2036E28 = 0;
    SetInitialPlayerAvatarStateWithDirection(DIR_NORTH);
    StopMapMusic();
    do_load_map_stuff_loop(&val);
}

void SetLastViewedPokedexEntry(void)
{
    gSaveBlock1Ptr->lastViewedPokedexEntry = GetStarterSpecies();
}

void TurnOffNuzlockeMode(void)
{
    if(gSaveBlock1Ptr->keyFlags.nuzlocke == 1)
    {
        gSaveBlock1Ptr->keyFlags.nuzlocke = 0;
    }
}

void TurnOffNoPMC(void)
{
    if(gSaveBlock1Ptr->keyFlags.noPMC == 1)
    {
        gSaveBlock1Ptr->keyFlags.noPMC = 0;
    }
}

void SetNoPMCTest(void)
{
    u8 noPMC = gSaveBlock1Ptr->keyFlags.noPMC;

    switch(noPMC)
    {
        case 0:
        default:
            gSaveBlock1Ptr->keyFlags.noPMC = 1;
            return;
        case 1:
            gSaveBlock1Ptr->keyFlags.noPMC = 0;
            return;
    }
}

void IsVersionFireRedToVarResult(void)
{
    if(gSaveBlock1Ptr->keyFlags.version == 0)
        gSpecialVar_Result = TRUE;
    else
        gSpecialVar_Result = FALSE;
}

void IsChallengeModeToVarResult(void)
{
    u8 difficulty = gSaveBlock1Ptr->keyFlags.difficulty;
    if(difficulty == DIFFICULTY_CHALLENGE)
    {
        gSpecialVar_Result = TRUE;
    }
    else
    {
        gSpecialVar_Result = FALSE;
    }
}

void MoveDaycareMan(void)
{
    if(!FlagGet(FLAG_GOT_RIVAL_STARTER_EGG))
    {
        VarSet(VAR_DAYCARE_MAN_TRIGGERS, 1);
        return;
    }
    if(FlagGet(FLAG_PENDING_DAYCARE_EGG))
    {
        Overworld_SetMapObjTemplateCoords(1, 16, 14);
        TryMoveObjectEventToMapCoords(1, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, 16, 14);
        VarSet(VAR_DAYCARE_MAN_TRIGGERS, 1);
        return;
    }
    /*else
    {
        Overworld_SetMapObjTemplateCoords(1, 16, 13);
        TryMoveObjectEventToMapCoords(1, gSaveBlock1Ptr->location.mapNum, gSaveBlock1Ptr->location.mapGroup, 16, 13);
        VarSet(VAR_DAYCARE_MAN_TRIGGERS, 1);
        return;
    }*/
}

void FillBagsTest(void)
{
    u32 i;
    //pokeballs
    for(i = 1; i < 13; i++)
    {
        AddBagItem(i, 999);
    }

    //regular items starting with Potion
    for(i = 13; i < 52; i++)
    {
        AddBagItem(i, 999);
    }

    //regular items starting with HP UP, skipping ??????????s
    for(i = 63; i < 87; i++)
    {
        if(i == 72 || i == 82) //skipping random ??????????s
        {
            continue;
        }
        AddBagItem(i, 999);
    }

    //regular items starting with Sun Stone, skipping ??????????s
    for(i = 93; i < 99; i++)
    {
        AddBagItem(i, 999);
    }

    //regular items starting with TinyMushroom, skipping ??????????s
    for(i = 103; i < 112; i++)
    {
        if(i == 105) //skipping random ??????????
        {
            continue;
        }
        AddBagItem(i, 999);
    }

    //regular items starting with Orange Mail
    for(i = 121; i < 133; i++)
    {
        AddBagItem(i, 999);
    }

    //hold items starting with Brightpowder
    for(i = 179; i < 226; i++)
    {
        AddBagItem(i, 999);
    }

    //Contest Scarves (skipping a bunch of ??????????s)
    for(i = 254; i < 259; i++)
    {
        AddBagItem(i, 999);
    }

    //RSE key items that get used in FRLG, starting with Coin Case
    for(i = 260; i < 266; i++)
    {
        AddBagItem(i, 1);
    }

    //FRLG key items starting with Oak's Parcel
    for(i = 349; i < 375; i++)
    {
        AddBagItem(i, 1);
    }

    //berries
    for(i = 133; i < 176; i++)
    {
        AddBagItem(i, 999);
    }

    //TMs and HMs
    for(i = 289; i < 347; i++)
    {
        AddBagItem(i, 999);
    }

    //Old Sea Map
    AddBagItem(376, 1);

    //Link Bracelet
    AddBagItem(112, 1);
}
