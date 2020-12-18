#ifndef PLAPI_PL2W_H
#define PLAPI_PL2W_H

#define PL2W_API

/*** -------------------------- versioning ------------------------- ***/

#define PL2_EDITION       "PL2-W"    /* Latin name */
#define PL2W_VER_MAJOR    0          /* Major version of PL2W */
#define PL2W_VER_MINOR    1          /* Minor version of PL2W */
#define PL2W_VER_PATCH    1          /* Patch version of PL2W */
#define PL2W_VER_POSTFIX  "halley"   /* Version postfix */

/*** ---------------------- end configurations --------------------- ***/

#include <stddef.h>
#include <stdint.h>
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/*** ------------------------ CMPRESULT ----------------------- ***/

typedef enum {
  CMP_LESS    = -1, /* LHS < RHS */
  CMP_EQ      = 0,  /* LHS = RHS */
  CMP_GREATER = 1,  /* LHS > RHS */
  CMP_NONE    = 255 /* not comparable */
} CMPRESULT;

/*** ----------------------- SRCINFO  ---------------------- ***/

typedef struct st_pl2w_source_info {
    LPCSTR szFileName;
    WORD wLine;
} SRCINFO;

SRCINFO SourceInfo(LPCSTR *szFileName, WORD wLine);

/*** -------------------------- PL2ERR ------------------------- ***/

typedef struct stError {
  LPVOID pExtraData;
  SRCINFO srcInfo;
  WORD wErrorCode;
  WORD wErrorBufferSize;
  char szReason[0];
} *LPERROR;

typedef enum {
  PL2ERR_NONE           = 0,  /* no error */
  PL2ERR_GENERAL        = 1,  /* general hard error */
  PL2ERR_PARSEBUF       = 2,  /* parse buffer exceeded */
  PL2ERR_UNCLOSED_STR   = 3,  /* unclosed string literal */
  PL2ERR_UNCLOSED_BEGIN = 4,  /* unclosed ?begin block */
  PL2ERR_EMPTY_CMD      = 5,  /* empty command */
  PL2ERR_SEMVER_PARSE   = 6,  /* semver parse error */
  PL2ERR_UNKNOWN_QUES   = 7,  /* unknown question mark command */
  PL2ERR_LOAD_LANG      = 8,  /* error loading language */
  PL2ERR_NO_LANG        = 9,  /* language not loaded */
  PL2ERR_UNKNOWN_CMD    = 10, /* unknown command */
  PL2ERR_MALLOC         = 11, /* malloc failure*/

  PL2ERR_USER           = 100 /* generic user error */
} ERRCODE;

LPERROR ErrorBuffer(WORD strBufferSize);

void ErrPrintf(LPERROR pError,
               WORD wErrorCode,
               SRCINFO srcInfo,
               LPVOID pExtraData,
               LPCSTR fmt,
               ...);

void DropError(LPERROR pError);

BOOL IsError(LPERROR pError);

/*** --------------------------- COMMAND --------------------------- ***/

typedef struct stCommand {
  struct stCommand *pPrev;
  struct stCommand *pNext;

  LPVOID pExtraData;
  SRCINFO srcInfo;
  LPSTR szCmd;
  LPSTR szArgs[0];
} *LPCOMMAND;

LPCOMMAND CreateCommand(LPCOMMAND pPrev,
                        LPCOMMAND pNext,
                        LPVOID pExtraData,
                        SRCINFO srcInfo,
                        LPSTR szCmd,
                        LPSTR aArgs[]);

WORD CountCommandArgs(LPCOMMAND cmd);

/*** ------------------------- pl2w_Program ------------------------ ***/

typedef struct stProgram {
  LPCOMMAND commands;
} *LPPROGRAM;

void InitProgram(LPPROGRAM pProgram);
LPPROGRAM ParseProgram(LPSTR szSource,
                       WORD wParseBufferSize,
                       LPERROR pError);
void DropProgram(LPPROGRAM pProgram);
void DebugPrintProgram(LPPROGRAM pProgram);

/*** -------------------- Semantic-ver parsing  -------------------- ***/

#define SEMVER_POSTFIX_LEN 15

typedef struct {
  WORD wMajor;
  WORD wMinor;
  WORD wPatch;
  char szPostfix[SEMVER_POSTFIX_LEN];
  BOOL bExact;
} SEMVER;

SEMVER ZeroVersion(void);
SEMVER ParseSemVer(LPCSTR szSource, LPERROR pError);
BOOL IsZeroVersion(SEMVER ver);
BOOL IsAlpha(SEMVER ver);
BOOL IsStable(SEMVER ver);
BOOL IsCompatible(SEMVER expected, SEMVER actual);
CMPRESULT CompareSemVer(SEMVER ver1, SEMVER ver2);
void FormatSemVer(SEMVER ver, LPSTR buffer);

/*** ------------------------ pl2w_Extension ----------------------- ***/

typedef void (pl2w_SInvokeCmdStub)(const char *strings[]);
typedef COMMAND *(pl2w_PCallCmdStub)(pl2w_Program *program,
                                      void *context,
                                      COMMAND *command,
                                      PL2ERR *error);
typedef _Bool (COMMANDRouterStub)(const char *str);

typedef void *(pl2w_InitStub)(PL2ERR *error);
typedef void (pl2w_AtexitStub)(void *context);

typedef struct st_pl2w_sinvoke_cmd {
  const char *cmdName;
  pl2w_SInvokeCmdStub *stub;
  _Bool deprecated;
  _Bool removed;
} pl2w_SInvokeCmd;

typedef struct st_pl2w_pcall_func {
  const char *cmdName;
  COMMANDRouterStub *routerStub;
  pl2w_PCallCmdStub *stub;
  _Bool deprecated;
  _Bool removed;
} pl2w_PCallCmd;

#define PL2W_EMPTY_SINVOKE_CMD(cmd) \
  ((cmd)->cmdName == 0 && (cmd)->stub == 0)
#define PL2W_EMPTY_CMD(cmd) \
  ((cmd)->cmdName == 0 && (cmd)->routerStub == 0 && (cmd)->stub == 0)

typedef struct st_pl2w_langauge {
  const char *langName;
  const char *langInfo;

  COMMAND *termCmd;

  pl2w_InitStub *init;
  pl2w_AtexitStub *atExit;
  pl2w_SInvokeCmd *sinvokeCmds;
  pl2w_PCallCmd *pCallCmds;
  pl2w_PCallCmdStub *fallback;
} pl2w_Language;

typedef pl2w_Language *(pl2w_LoadLanguage)(pl2w_SemVer version,
                                           PL2ERR *error);
typedef const char **(pl2w_EasyLoadLanguage)(void);

/*** ----------------------------- Run ----------------------------- ***/

void pl2w_run(pl2w_Program *program, PL2ERR *error);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PLAPI_PL2W_H */
