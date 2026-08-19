/**
 * @file chraidata_stub.c
 * M1 stand-in for src/game/chraidata.c, which builds the built-in AI
 * scripts with IDO-only preprocessor machinery (SWITCH/AND/BITFLAG-style
 * missing-macro-arg tricks GCC rejects).
 *
 * The table layout and IDs are real; every script body is stubbed to a
 * single AI_Return-style terminator byte, which is fine until NPCs run
 * (M3). The M3 fix: pre-expand the original file with IDO's preprocessor
 * (`cc -E`) once the N64 toolchain is set up, commit the result, compile
 * that on PC.
 */
#include <ultra64.h>
#include <bondtypes.h>
#include <bondaicommands.h>

#define STUB_AI_SCRIPT(name) u8 name[] = { 0x04 /* AI_Return */ }

STUB_AI_SCRIPT(m_AimAtBond);
STUB_AI_SCRIPT(m_DeadAI);
STUB_AI_SCRIPT(m_StandardGuard);
STUB_AI_SCRIPT(m_IdleAnimations);
STUB_AI_SCRIPT(m_BashKeyboard);
STUB_AI_SCRIPT(m_SimpleGuardDeaf);
STUB_AI_SCRIPT(m_AttackBond);
STUB_AI_SCRIPT(m_RunToBond);
STUB_AI_SCRIPT(m_TryCloneSendOrRunToBond);
STUB_AI_SCRIPT(m_StandardClone);
STUB_AI_SCRIPT(m_SimpleGuard);
STUB_AI_SCRIPT(m_SimpleGuardAlarmRaiser);
STUB_AI_SCRIPT(m_StartleAndRunToBond);
STUB_AI_SCRIPT(m_RunToBondPersistent);
STUB_AI_SCRIPT(m_WaitOneSecond);
STUB_AI_SCRIPT(m_EndLevel);
STUB_AI_SCRIPT(m_DrawPistolAndAttackBond);
STUB_AI_SCRIPT(m_RemoveSelf);

/* Table copied verbatim from chraidata.c — the IDs are load-bearing. */
AIListRecord g_GlobalAILists[] = {
    {(AIRecord *)m_AimAtBond               , GAILIST_AIM_AT_BOND},
    {(AIRecord *)m_DeadAI                  , GAILIST_DEAD_AI},
    {(AIRecord *)m_StandardGuard           , GAILIST_STANDARD_GUARD},
    {(AIRecord *)m_IdleAnimations          , GAILIST_PLAY_IDLE_ANIMATION},
    {(AIRecord *)m_BashKeyboard            , GAILIST_BASH_KEYBOARD},
    {(AIRecord *)m_SimpleGuardDeaf         , GAILIST_SIMPLE_GUARD_DEAF},
    {(AIRecord *)m_AttackBond              , GAILIST_ATTACK_BOND},
    {(AIRecord *)m_SimpleGuard             , GAILIST_SIMPLE_GUARD},
    {(AIRecord *)m_RunToBond               , GAILIST_RUN_TO_BOND},
    {(AIRecord *)m_SimpleGuardAlarmRaiser  , GAILIST_SIMPLE_GUARD_ALARM_RAISER},
    {(AIRecord *)m_StartleAndRunToBond     , GAILIST_STARTLE_AND_RUN_TO_BOND},
    {(AIRecord *)m_TryCloneSendOrRunToBond , GAILIST_TRY_CLONE_SEND_OR_RUN_TO_BOND},
    {(AIRecord *)m_StandardClone           , GAILIST_STANDARD_CLONE},
    {(AIRecord *)m_RunToBondPersistent     , GAILIST_PERSISTENTLY_CHASE_AND_ATTACK_BOND},
    {(AIRecord *)m_WaitOneSecond           , GAILIST_WAIT_ONE_SECOND},
    {(AIRecord *)m_EndLevel                , GAILIST_END_LEVEL},
    {(AIRecord *)m_DrawPistolAndAttackBond , GAILIST_DRAW_TT33_AND_ATTCK_BOND},
    {(AIRecord *)m_RemoveSelf              , GAILIST_REMOVE_CHR},
    {NULL, 0}
};

/* Copied verbatim from chraidata.c — used to pick per-level setup text. */
char *setup_text_pointers[] = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "UsetupsevbunkerZ",
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,"UsetupsiloZ",
    "UsetupsevbunkerZ","UsetupstatueZ","UsetupcontrolZ","UsetuparchZ","UsetuptraZ",
    "UsetupdestZ","UsetupsevbZ","UsetupaztZ","UsetuppeteZ","UsetupdepoZ","UsetuprefZ",
    "UsetupcrypZ","UsetupdamZ","UsetuparkZ","UsetuprunZ","UsetupsevxZ","UsetupjunZ",
    "UsetupdishZ","UsetupcaveZ","UsetupcatZ","UsetupcradZ","UsetupshoZ","UsetupsevxbZ",
    "UsetupeldZ","UsetupimpZ","UsetupashZ","UsetuplueZ","UsetupameZ","UsetupritZ",
    "UsetupoatZ","UsetupearZ","UsetupleeZ","UsetuplipZ","UsetuplenZ","UsetupwaxZ",
    "UsetuppamZ", NULL, NULL
};
