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

#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*** ------------------------ CMPRESULT ----------------------- ***/

typedef enum
{
  CMP_LESS    = -1, /* LHS < RHS */
  CMP_EQ      = 0,  /* LHS = RHS */
  CMP_GREATER = 1,  /* LHS > RHS */
  CMP_NONE    = 255 /* not comparable */
} CMPRESULT;

/*** ----------------------- SRCINFO  ---------------------- ***/

typedef struct
{
    LPCSTR szFileName;
    WORD wLine;
} SRCINFO;

SRCINFO SourceInfo(LPCSTR szFileName, WORD wLine);

/*** -------------------------- PL2ERR ------------------------- ***/

typedef struct stError
{
  LPVOID pExtraData;
  SRCINFO srcInfo;
  WORD wErrorCode;
  WORD wErrorBufferSize;
  char szReason[0];
} *LPERROR;

typedef enum
{
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

LPERROR ErrorBuffer(WORD wBufferSize);

void ErrPrintf(LPERROR pError,
               WORD wErrorCode,
               SRCINFO srcInfo,
               LPVOID pExtraData,
               LPCSTR szFmt,
               ...);

void DropError(LPERROR pError);

BOOL IsError(LPERROR pError);

/*** --------------------------- COMMAND --------------------------- ***/

typedef struct stCommand
{
  struct stCommand *pPrev;
  struct stCommand *pNext;

  LPVOID pExtraData;
  SRCINFO srcInfo;
  LPSTR szCmd;
  LPSTR aArgs[0];
} *LPCOMMAND;

LPCOMMAND CreateCommand(LPCOMMAND pPrev,
                        LPCOMMAND pNext,
                        LPVOID pExtraData,
                        SRCINFO srcInfo,
                        LPSTR szCmd,
                        LPSTR aArgs[]);

WORD CountCommandArgs(LPCOMMAND pCmd);

/*** ------------------------- pl2w_Program ------------------------ ***/

struct stProgram
{
  LPCOMMAND pCommands;
};

typedef struct stProgram *LPPROGRAM;
typedef const struct stProgram *LPCPROGRAM;

void InitProgram(LPPROGRAM pProgram);
LPPROGRAM ParseProgram(LPSTR szSource,
                       WORD wParseBufferSize,
                       LPERROR pError);
void DropProgram(LPPROGRAM pProgram);
void DebugPrintProgram(LPCPROGRAM pProgram);

/*** -------------------- Semantic-ver parsing  -------------------- ***/

#define SEMVER_POSTFIX_LEN 15

typedef struct
{
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
void FormatSemVer(SEMVER ver, LPSTR szBuffer);

/*** ------------------------ pl2w_Extension ----------------------- ***/

typedef void (*LPSINVPROC)(LPCSTR aStrings[]);
typedef LPCOMMAND (*LPWCALLPROC)(LPPROGRAM program,
                                 LPVOID pContext,
                                 LPCOMMAND pCommand,
                                 LPERROR pEerror);
typedef BOOL (*LPROUTERPROC)(LPCSTR szCommand);

typedef LPVOID (*LPINITPROC)(LPERROR pError);
typedef void (*LPATEXITPROC)(LPVOID pContext);

typedef struct
{
  LPCSTR szCmdName;
  LPSINVPROC pfnHandlerProc;
  BOOL bDeprecated;
  BOOL bRemoved;
} SINVHANDLER;

typedef struct
{
  LPCSTR szCmdName;
  LPROUTERPROC pfnRouterProc;
  LPWCALLPROC pfnHandlerProc;
  BOOL bDeprecated;
  BOOL bRemoved;
} WCALLHANDLER;

#define IS_EMPTY_SINVOKE_CMD(cmd) \
  ((cmd)->szCmdName == 0 && \
   (cmd)->pfnHandlerProc == 0)
#define IS_EMPTY_CMD(cmd) \
  ((cmd)->szCmdName == 0 \
   && (cmd)->pfnRouterProc == 0 \
   && (cmd)->pfnHandlerProc == 0)

typedef struct
{
  LPCSTR szLangName;
  LPCSTR szLangInfo;

  LPCOMMAND pTermCmd;

  LPINITPROC pfnInitProc;
  LPATEXITPROC pfnAtexitProc;
  SINVHANDLER *aSinvokeHandlers;
  WCALLHANDLER *aWCallHandlers;
  LPWCALLPROC pfnFallbackProc;
} *LPLANGUAGE;

typedef LPLANGUAGE (*LPLOADPROC)(SEMVER version,
                                 LPERROR pError);
typedef LPCSTR* (*LPLOADPROC)(void);

/*** ----------------------------- Run ----------------------------- ***/

void RunProgram(LPPROGRAM pProgram, LPERROR pError);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PLAPI_PL2W_H */
