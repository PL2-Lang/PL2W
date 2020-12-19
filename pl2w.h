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
    LPCSTR lpszFileName;
    WORD nLine;
} SRCINFO;

SRCINFO SourceInfo(LPCSTR lpszFileName, WORD nLine);

/*** -------------------------- PL2ERR ------------------------- ***/

typedef struct stError
{
  LPVOID lpExtraData;
  SRCINFO srcInfo;
  WORD nLine;
  WORD nErrorBufferSize;
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

LPERROR ErrorBuffer(WORD nBufferSize);

void ErrPrintf(LPERROR lpError,
               WORD nLine,
               SRCINFO srcInfo,
               LPVOID lpExtraData,
               LPCSTR lpszFmt,
               ...);

void DropError(LPERROR lpError);

BOOL IsError(LPERROR lpError);

/*** --------------------------- COMMAND --------------------------- ***/

typedef struct stCommand
{
  struct stCommand *lpPrev;
  struct stCommand *lpNext;

  LPVOID lpExtraData;
  SRCINFO srcInfo;
  LPSTR lpszCmd;
  LPSTR aszArgs[0];
} *LPCOMMAND;

LPCOMMAND CreateCommand(LPCOMMAND lpPrev,
                        LPCOMMAND lpNext,
                        LPVOID lpExtraData,
                        SRCINFO srcInfo,
                        LPSTR lpszCmd,
                        LPSTR aszArgs[]);

WORD CountCommandArgs(LPCOMMAND lpCmd);

/*** ------------------------- pl2w_Program ------------------------ ***/

struct stProgram
{
  LPCOMMAND lpCommands;
};

typedef struct stProgram *LPPROGRAM;
typedef const struct stProgram *LPCPROGRAM;

void InitProgram(LPPROGRAM lpProgram);
LPPROGRAM ParseProgram(LPSTR lpszSource,
                       WORD nParseBufferSize,
                       LPERROR lpError);
void DropProgram(LPPROGRAM lpProgram);
void DebugPrintProgram(LPCPROGRAM lpProgram);

/*** -------------------- Semantic-ver parsing  -------------------- ***/

#define SEMVER_POSTFIX_LEN 15

typedef struct
{
  WORD nMajor;
  WORD nMinor;
  WORD nPatch;
  CHAR szPostfix[SEMVER_POSTFIX_LEN];
  BOOL bExact;
} SEMVER;

SEMVER ZeroVersion(void);
SEMVER ParseSemVer(LPCSTR lpszSource, LPERROR lpError);
BOOL IsZeroVersion(SEMVER ver);
BOOL IsAlpha(SEMVER ver);
BOOL IsStable(SEMVER ver);
BOOL IsCompatible(SEMVER expected, SEMVER actual);
CMPRESULT CompareSemVer(SEMVER ver1, SEMVER ver2);
void FormatSemVer(SEMVER ver, LPSTR lpszBuffer);

/*** ------------------------ pl2w_Extension ----------------------- ***/

typedef void (*LPSINVPROC)(LPCSTR aStrings[]);
typedef LPCOMMAND (*LPWCALLPROC)(LPPROGRAM lpProgram,
                                 LPVOID lpUserContext,
                                 LPCOMMAND lpCommand,
                                 LPERROR lpError);
typedef BOOL (*LPROUTERPROC)(LPCSTR lpszCommand);

typedef LPVOID (*LPINITPROC)(LPERROR lpError);
typedef void (*LPATEXITPROC)(LPVOID lpContext);

typedef struct
{
  LPCSTR lpszCmdName;
  LPSINVPROC lpfnHandlerProc;
  BOOL bDeprecated;
  BOOL bRemoved;
} SINVHANDLER;

typedef struct
{
  LPCSTR lpszCmdName;
  LPROUTERPROC lpfnRouterProc;
  LPWCALLPROC lpfnHandlerProc;
  BOOL bDeprecated;
  BOOL bRemoved;
} WCALLHANDLER;

#define IS_EMPTY_SINVOKE_CMD(cmd) \
  ((cmd)->lpszCmdName == 0 && \
   (cmd)->lpfnHandlerProc == 0)
#define IS_EMPTY_CMD(cmd) \
  ((cmd)->lpszCmdName == 0 \
   && (cmd)->lpfnRouterProc == 0 \
   && (cmd)->lpfnHandlerProc == 0)

typedef struct stLanguage
{
  LPCSTR lpszLangName;
  LPCSTR lpszLangInfo;

  LPCOMMAND lpTermCmd;

  LPINITPROC lpfnInitProc;
  LPATEXITPROC lpfnAtexitProc;
  SINVHANDLER *aSinvokeHandlers;
  WCALLHANDLER *aWCallHandlers;
  LPWCALLPROC lpfnFallbackProc;
} *LPLANGUAGE;

typedef LPLANGUAGE (*LPLOADPROC)(SEMVER version,
                                 LPERROR lpError);
typedef LPCSTR* (*LPEASYLOADPROC)(void);

/*** ----------------------------- Run ----------------------------- ***/

void RunProgram(LPPROGRAM lpProgram, LPERROR lpError);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PLAPI_PL2W_H */
