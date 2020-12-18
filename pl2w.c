#include "pl2w.h"

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>

/*** ----------------- Transmute any LPSTRto uLPSTR----------------- ***/

static BYTE TransmuteU8(CHAR ch)
{
  return *(BYTE*)(&ch);
}

/*** ------------------- Implementation of slice ------------------- ***/

typedef struct {
  PCHAR pStart;
  PCHAR pEnd;
} SLICE;

typedef SLICE *LPSLICE;
typedef const SLICE* LPCSLICE;

static SLICE Slice(PCHAR pStart, PCHAR pEnd);
static SLICE NullSlice (void);
static LPSTR SliceIntoCStr(SLICE slice);
static BOOL IsNullSlice(SLICE slice);

SLICE Slice(PCHAR pStart, PCHAR pEnd)
{
  if (pStart == pEnd)
    {
      return (SLICE){NULL, NULL};
    }
  else
    {
      return (SLICE){pStart, pEnd};
    }
}

static SLICE NullSlice(void)
{
  return (SLICE){NULL, NULL};
}

static LPSTR SliceIntoCStr(SLICE slice)
{
  if (IsNullSlice(slice))
    {
      return NULL;
    }
  if (*slice.pEnd != '\0') 
    {
      *slice.pEnd = '\0';
    }
  return slice.pStart;
}

static BOOL IsNullSlice(SLICE slice)
{
  return slice.pStart == slice.pEnd;
}

/*** ----------------- Implementation of PL2ERR ---------------- ***/

LPERROR ErrorBuffer(WORD wBufferSize)
{
  LPERROR pRet = (LPERROR)malloc(sizeof(struct stError) + wBufferSize);
  if (pRet == NULL)
    {
      return NULL;
    }
  memset(pRet, 0, sizeof(struct stError) + wBufferSize);
  pRet->wErrorBufferSize = wBufferSize;
  return pRet;
}

void ErrPrintf(LPERROR pError,
               WORD wErrorCode,
               SRCINFO srcInfo,
               LPVOID pExtraData,
               LPCSTR szFmt,
               ...)
{
  pError->wErrorCode = wErrorCode;
  pError->pExtraData = pExtraData;
  pError->srcInfo = srcInfo;
  if (pError->wErrorBufferSize == 0)
    {
      return;
    }

  va_list ap;
  va_start(ap, szFmt);
  vsnprintf(pError->szReason, pError->wErrorBufferSize, szFmt, ap);
  va_end(ap);
}

void DropError(LPERROR pError)
{
  if (pError->pExtraData)
    {
      free(pError->pExtraData);
    }
  free(pError);
}

BOOL IsError(LPERROR pError)
{
  return pError->wErrorCode != 0;
}

/*** ------------------- Some toolkit functions -------------------- ***/

SRCINFO SourceInfo(LPCSTR szFileName, WORD wLine)
{
  SRCINFO ret;
  ret.szFileName = szFileName;
  ret.wLine = wLine;
  return ret;
}

LPCOMMAND CreateCommand(LPCOMMAND pPrev,
                        LPCOMMAND pNext,
                        LPVOID pExtraData,
                        SRCINFO srcInfo,
                        LPSTR szCmd,
                        LPSTR aArgs[])
{
  DWORD wArgCount = 0;
  for (; aArgs[wArgCount] != NULL; ++wArgCount);

  LPCOMMAND pRet = (LPCOMMAND)malloc(
    sizeof(struct stCommand) + (wArgCount + 1) * sizeof(LPSTR)
  );
  if (pRet == NULL)
    {
      return NULL;
    }
  pRet->pPrev = pPrev;
  if (pPrev != NULL)
    {
      pPrev->pNext = pRet;
    }
  pRet->pNext = pNext;
  if (pNext != NULL)
    {
      pNext->pPrev = pRet;
    }
  pRet->srcInfo = srcInfo;
  pRet->szCmd = szCmd;
  pRet->pExtraData = pExtraData;
  for (WORD i = 0; i < wArgCount; i++)
    {
      pRet->aArgs[i] = aArgs[i];
    }
  pRet->aArgs[wArgCount] = NULL;
  return pRet;
}

WORD CountCommandArgs(LPCOMMAND pCmd)
{
  WORD wAcc = 0;
  LPSTR *iter = pCmd->aArgs;
  while (*iter != NULL)
    {
      iter++;
      wAcc++;
    }
  return wAcc;
}

/*** ---------------- Implementation of pl2w_Program --------------- ***/

void InitProgram(LPPROGRAM pProgram)
{
  pProgram->pCommands = NULL;
}

void DropProgram(LPPROGRAM pProgram)
{
  LPCOMMAND iter = pProgram->pCommands;
  while (iter != NULL)
    {
      LPCOMMAND pNext = iter->pNext;
      free(iter);
      iter = pNext;
    }
}

void DebugPrintProgram(LPCPROGRAM pProgram)
{
  fprintf(stderr, "program commands\n");
  LPCOMMAND pCmd = pProgram->pCommands;
  while (pCmd != NULL)
    {
      fprintf(stderr, "\t%s [", pCmd->szCmd);
      for (WORD i = 0; pCmd->aArgs[i] != NULL; i++)
        {
          LPSTR szArg = pCmd->aArgs[i];
          fprintf(stderr, "`%s`, ", szArg);
        }
      fprintf(stderr, "\b\b]\n");
      pCmd = pCmd->pNext;
    }
  fprintf(stderr, "end program commands\n");
}

/*** ----------------- Implementation of pl2w_parse ---------------- ***/

typedef enum
{
  PARSE_SINGLE_LINE = 1,
  PARSE_MULTI_LINE  = 2
} PARSEMODE;

typedef enum
{
  QUES_INVALID = 0,
  QUES_BEGIN   = 1,
  QUES_END     = 2
} QUESCMD;

typedef struct stParseContext
{
  struct stProgram program;
  LPCOMMAND pListTail;

  LPSTR szSrc;
  DWORD dwSrcIdx;
  PARSEMODE mode;

  SRCINFO srcInfo;

  WORD wParseBufferSize;
  WORD wParseBufferUsage;
  SLICE aParseBuffer[0];
} *LPPARSECONTEXT;

static LPPARSECONTEXT CreateParseContext(LPSTR szSrc,
                                         WORD parseBufferSize);
static void ParseLine(LPPARSECONTEXT pCtx, LPERROR pError);
static void ParseQuesMark(LPPARSECONTEXT pCtx, LPERROR pError);
static void ParsePart(LPPARSECONTEXT pCtx, LPERROR pError);
static SLICE ParseId(LPPARSECONTEXT pCtx, LPERROR pError);
static SLICE ParseStr(LPPARSECONTEXT pCtx, LPERROR pError);
static void CheckBufferSize(LPPARSECONTEXT pCtx, LPERROR pError);
static void FinishLine(LPPARSECONTEXT pCtx, LPERROR pError);
static LPCOMMAND CreateCommandFS2(SRCINFO srcInfo,
                                  SLICE *aParts);
static LPCOMMAND CreateCommandFS5(LPCOMMAND pPrev,
                                  LPCOMMAND pNext,
                                  LPVOID pExtraData,
                                  SRCINFO srcInfo,
                                  SLICE *aParts);
static void SkipWhitespace(LPPARSECONTEXT pCtx);
static void SkipComment(LPPARSECONTEXT pCtx);
static CHAR CurChar(LPPARSECONTEXT pCtx);
static PCHAR CurCharPos(LPPARSECONTEXT pCtx);
static void NextChar(LPPARSECONTEXT pCtx);
static BOOL IsIdChar(CHAR ch);
static BOOL IsLineEnd(CHAR ch);
static LPSTR ShrinkConv(PCHAR pStart, PCHAR pEnd);

LPPROGRAM ParseProgram(LPSTR szSource,
                       WORD wParseBufferSize,
                       LPERROR pError)
{
  LPPARSECONTEXT pCtx = CreateParseContext
    (
      szSource,
      wParseBufferSize
    );
  if (pCtx == NULL)
    {
      return NULL;
    }
  while (CurChar(pCtx) != '\0')
    {
      ParseLine(pCtx, pError);
      if (IsError(pError))
        {
          break;
        }
    }

  LPPROGRAM pRet = (LPPROGRAM)malloc(sizeof(struct stProgram));
  memcpy(pRet, &pCtx->program, sizeof(struct stProgram));
  free(pCtx);
  return pRet;
}

static LPPARSECONTEXT CreateParseContext(LPSTR szSrc,
                                         WORD wParseBufferSize) {
  LPPARSECONTEXT pRet = (LPPARSECONTEXT)malloc
    (
      sizeof(struct stParseContext) + wParseBufferSize * sizeof(SLICE)
    );
  if (pRet == NULL) 
    {
      return NULL;
    }

  InitProgram(&pRet->program);
  pRet->pListTail = NULL;
  pRet->szSrc = szSrc;
  pRet->dwSrcIdx = 0;
  pRet->srcInfo = SourceInfo("<unknown-file>", 1);
  pRet->mode = PARSE_SINGLE_LINE;

  pRet->wParseBufferSize = wParseBufferSize;
  pRet->wParseBufferUsage = 0;
  memset(pRet->aParseBuffer, 0, wParseBufferSize * sizeof(SLICE));
  return pRet;
}

static void ParseLine(LPPARSECONTEXT pCtx, LPERROR pError) {
  if (CurChar(pCtx) == '?')
    {
      ParseQuesMark(pCtx, pError);
      if (IsError(pError))
        {
          return;
        }
    }

  while (TRUE)
    {
      SkipWhitespace(pCtx);
      if (CurChar(pCtx) == '\0' || CurChar(pCtx) == '\n')
        {
          if (pCtx->mode == PARSE_SINGLE_LINE)
            {
              FinishLine(pCtx, pError);
            }
          if (pCtx->mode == PARSE_MULTI_LINE && CurChar(pCtx) == '\0')
            {
              ErrPrintf(pError, PL2ERR_UNCLOSED_BEGIN, pCtx->srcInfo,
                        NULL, "unclosed `?begin` block");
            }
          if (CurChar(pCtx) == '\n')
            {
              NextChar(pCtx);
            }
          return;
        }
      else if (CurChar(pCtx) == '#')
        {
          SkipComment(pCtx);
        }
      else
        {
          ParsePart(pCtx, pError);
          if (IsError(pError))
            {
              return;
            }
        }
  }
}

static void ParseQuesMark(LPPARSECONTEXT pCtx, LPERROR pError)
{
  assert(CurChar(pCtx) == '?');
  NextChar(pCtx);

  PCHAR pStart = CurCharPos(pCtx);
  while (isalnum((int)CurChar(pCtx)))
    {
      NextChar(pCtx);
    }
  PCHAR pEnd = CurCharPos(pCtx);
  SLICE s = Slice(pStart, pEnd);
  LPSTR szQuesCommand = SliceIntoCStr(s);
  
  if (!strcmp(szQuesCommand, "begin")) 
    {
      pCtx->mode = PARSE_MULTI_LINE;
    }
  else if (!strcmp(szQuesCommand, "end"))
    {
      pCtx->mode = PARSE_SINGLE_LINE;
      FinishLine(pCtx, pError);
    }
  else
    {
      ErrPrintf(pError, PL2ERR_UNKNOWN_QUES, pCtx->srcInfo,
                NULL, "unknown question mark operator: `%s`",
                szQuesCommand);
    }
}

static void ParsePart(LPPARSECONTEXT pCtx, LPERROR pError)
{
  SLICE part;
  if (CurChar(pCtx) == '"' || CurChar(pCtx) == '\'')
    {
      part = ParseStr(pCtx, pError);
    }
  else
    {
      part = ParseId(pCtx, pError);
    }
  if (IsError(pError))
    {
      return;
    }

  CheckBufferSize(pCtx, pError);
  if (IsError(pError))
    {
      return;
    }

  pCtx->aParseBuffer[pCtx->wParseBufferUsage++] = part;
}

static SLICE ParseId(LPPARSECONTEXT pCtx, LPERROR pError)
{
  (void)pError;
  PCHAR pStart = CurCharPos(pCtx);
  while (IsIdChar(CurChar(pCtx)))
    {
      NextChar(pCtx);
    }
  PCHAR pEnd = CurCharPos(pCtx);
  return Slice(pStart, pEnd);
}

static SLICE ParseStr(LPPARSECONTEXT pCtx, LPERROR pError)
{
  assert(CurChar(pCtx) == '"' || CurChar(pCtx) == '\'');
  NextChar(pCtx);

  PCHAR pStart = CurCharPos(pCtx);
  while (CurChar(pCtx) != '"'
         && CurChar(pCtx) != '\''
         && !IsLineEnd(CurChar(pCtx)))
    {
      if (CurChar(pCtx) == '\\')
        {
          NextChar(pCtx);
          NextChar(pCtx);
        }
      else
        {
          NextChar(pCtx);
        }
    }

  PCHAR pEnd = CurCharPos(pCtx);
  pEnd = ShrinkConv(pStart, pEnd);

  if (CurChar(pCtx) == '"' || CurChar(pCtx) == '\'')
    {
      NextChar(pCtx);
    }
  else
    {
      ErrPrintf(pError, PL2ERR_UNCLOSED_BEGIN, pCtx->srcInfo,
                NULL, "unclosed string literal");
      return NullSlice();
    }
  return Slice(pStart, pEnd);
}

static void CheckBufferSize(LPPARSECONTEXT pCtx, LPERROR pError)
{
  if (pCtx->wParseBufferSize <= pCtx->wParseBufferUsage + 1)
    {
      ErrPrintf(pError, PL2ERR_UNCLOSED_BEGIN, pCtx->srcInfo,
                NULL, "command parts exceed internal parsing buffer");
    }
}

static void FinishLine(LPPARSECONTEXT pCtx, LPERROR pError)
{
  (void)pError;

  SRCINFO srcInfo = pCtx->srcInfo;
  NextChar(pCtx);
  if (pCtx->wParseBufferUsage == 0)
    {
      return;
    }
  if (pCtx->pListTail == NULL)
    {
      assert(pCtx->program.pCommands == NULL);
      pCtx->program.pCommands = pCtx->pListTail = CreateCommandFS2
        (
          pCtx->srcInfo,
          pCtx->aParseBuffer
        );
    }
  else
    {
      pCtx->pListTail = CreateCommandFS5
        (
          pCtx->pListTail,
          NULL,
          NULL,
          pCtx->srcInfo,
          pCtx->aParseBuffer
        );
    }
  if (pCtx->pListTail == NULL)
    {
      ErrPrintf(pError, PL2ERR_MALLOC, srcInfo, 0,
                "failed allocating COMMAND");
    }
  memset(pCtx->aParseBuffer, 0, sizeof(SLICE) * pCtx->wParseBufferSize);
  pCtx->wParseBufferUsage = 0;
}

static LPCOMMAND CreateCommandFS2(SRCINFO srcInfo,
                                  SLICE *aParts)
{
  return CreateCommandFS5(NULL, NULL, NULL, srcInfo, aParts);
}

static LPCOMMAND CreateCommandFS5(LPCOMMAND pPrev,
                                  LPCOMMAND pNext,
                                  LPVOID pExtraData,
                                  SRCINFO srcInfo,
                                  SLICE *aParts)
{
  WORD wPartCount = 0;
  for (; !IsNullSlice(aParts[wPartCount]); ++wPartCount);

  LPCOMMAND pRet = (LPCOMMAND)malloc
    (
      sizeof(struct stCommand) + wPartCount * sizeof(LPSTR)
    );
  if (pRet == NULL)
    {
      return NULL;
    }

  pRet->pPrev = pPrev;
  if (pPrev != NULL)
    {
      pPrev->pNext = pRet;
    }
  pRet->pNext = pNext;
  if (pNext != NULL)
    {
      pNext->pPrev = pRet;
    }
  pRet->pExtraData = pExtraData;
  pRet->srcInfo = srcInfo;
  pRet->szCmd = SliceIntoCStr(aParts[0]);
  for (WORD i = 1; i < wPartCount; i++)
    {
      pRet->aArgs[i - 1] = SliceIntoCStr(aParts[i]);
    }
  pRet->aArgs[wPartCount - 1] = NULL;
  return pRet;
}

static void SkipWhitespace(LPPARSECONTEXT pCtx)
{
  while (1)
    {
      switch (CurChar(pCtx))
        {
          case ' ': case '\t': case '\f': case '\v': case '\r':
            NextChar(pCtx);
            break;
          default:
            return;
        }
    }
}

static void SkipComment(LPPARSECONTEXT pCtx)
{
  assert(CurChar(pCtx) == '#');
  NextChar(pCtx);

  while (!IsLineEnd(CurChar(pCtx)))
    {
      NextChar(pCtx);
    }

  if (CurChar(pCtx) == '\n')
    {
      NextChar(pCtx);
    }
}

static CHAR CurChar(LPPARSECONTEXT pCtx)
{
  return pCtx->szSrc[pCtx->dwSrcIdx];
}

static PCHAR CurCharPos(LPPARSECONTEXT pCtx)
{
  return pCtx->szSrc + pCtx->dwSrcIdx;
}

static void NextChar(LPPARSECONTEXT pCtx)
{
  if (pCtx->szSrc[pCtx->dwSrcIdx] == '\0')
    {
      return;
    }
  else
    {
      if (pCtx->szSrc[pCtx->dwSrcIdx] == '\n')
        {
          pCtx->srcInfo.wLine += 1;
        }
      pCtx->dwSrcIdx += 1;
    }
}

static BOOL IsIdChar(CHAR ch)
{
  BYTE uch = TransmuteU8(ch);
  if (uch >= 128)
    {
      return 1;
    }
  else if (isalnum(ch))
    {
      return 1;
    }
  else 
    {
      switch (ch)
        {
          case '!': case '$': case '%': case '^': case '&': case '*':
          case '(': case ')': case '-': case '+': case '_': case '=':
          case '[': case ']': case '{': case '}': case '|': case '\\':
          case ':': case ';': case '\'': case ',': case '<': case '>':
          case '/': case '?': case '~': case '@': case '.':
            return 1;
          default:
            return 0;
        }
    }
}

static BOOL IsLineEnd(CHAR ch)
{
  return ch == '\0' || ch == '\n';
}

static LPSTR ShrinkConv(PCHAR start, PCHAR end)
{
  PCHAR iter1 = start, iter2 = start;
  while (iter1 != end)
    {
      if (iter1[0] == '\\')
        {
          switch (iter1[1])
            {
              case 'n': *iter2++ = '\n'; iter1 += 2; break;
              case 'r': *iter2++ = '\r'; iter1 += 2; break;
              case 'f': *iter2++ = '\f'; iter1 += 2; break;
              case 'v': *iter2++ = '\v'; iter1 += 2; break;
              case 't': *iter2++ = '\t'; iter1 += 2; break;
              case 'a': *iter2++ = '\a'; iter1 += 2; break;
              case '"': *iter2++ = '\"'; iter1 += 2; break;
              case '0': *iter2++ = '\0'; iter1 += 2; break;
              default: *iter2++ = *iter1++;
            }
        }
      else
        {
          *iter2++ = *iter1++;
        }
    }
  return iter2;
}

/*** -------------------- Semantic-ver parsing  -------------------- ***/

static LPCSTR ParseUint16(LPCSTR szSrc,
                          WORD *pOutput,
                          LPERROR pError);

static void ParseSemVerPostfix(LPCSTR src,
                               LPSTR output,
                               LPERROR pError);

SEMVER ZeroVersion(void)
{
  SEMVER ret;
  ret.wMajor = 0;
  ret.wMinor = 0;
  ret.wPatch = 0;
  memset(ret.szPostfix, 0, SEMVER_POSTFIX_LEN);
  ret.bExact = FALSE;
  return ret;
}

BOOL IsZeroVersion(SEMVER ver)
{
  return ver.wMajor == 0
         && ver.wMinor == 0
         && ver.wPatch == 0
         && ver.szPostfix[0] == 0
         && ver.bExact == 0;
}

BOOL IsAlpha(SEMVER ver)
{
  return ver.szPostfix[0] != '\0';
}

BOOL IsStable(SEMVER ver)
{
  return !IsAlpha(ver) && ver.wMajor != 0;
}

SEMVER ParseSemVer(LPCSTR szSrc, LPERROR pError)
{
  SEMVER ret = ZeroVersion();
  if (szSrc[0] == '^')
    {
      ret.bExact = 1;
      szSrc++;
    }

  szSrc = ParseUint16(szSrc, &ret.wMajor, pError);
  if (IsError(pError))
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "missing major version");
      goto done;
    }
  else if (szSrc[0] == '\0')
    {
      goto done;
    }
  else if (szSrc[0] == '-')
    {
      goto parse_postfix;
    }
  else if (szSrc[0] != '.')
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "expected `.`, got `%c`", szSrc[0]);
      goto done;
    }
  szSrc++;
  szSrc = ParseUint16(szSrc, &ret.wMinor, pError);
  if (IsError(pError))
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0),
                NULL, "missing minor version");
      goto done;
    }
  else if (szSrc[0] == '\0')
    {
      goto done;
    }
  else if (szSrc[0] == '-')
    {
      goto parse_postfix;
    }
  else if (szSrc[0] != '.')
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "expected `.`, got `%c`", szSrc[0]);
      goto done;
    }

  szSrc++;
  szSrc = ParseUint16(szSrc, &ret.wPatch, pError);
  if (IsError(pError))
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "missing patch version");
      goto done;
    }
  else if (szSrc[0] == '\0')
    {
      goto done;
    }
  else if (szSrc[0] != '-')
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "unterminated semver, "
                "expected `-` or `\\0`, got `%c`",
                szSrc[0]);
      goto done;
    }

parse_postfix:
  ParseSemVerPostfix(szSrc, ret.szPostfix, pError);

done:
  return ret;
}

BOOL IsCompatible(SEMVER expected, SEMVER actual)
{
  if (strncmp(expected.szPostfix,
              actual.szPostfix,
              SEMVER_POSTFIX_LEN) != 0)
    {
      return FALSE;
    }
  if (expected.bExact)
    {
      return expected.wMajor == actual.wMajor
             && expected.wMinor == actual.wMinor
             && expected.wPatch == actual.wPatch;
    }
  else if (expected.wMajor == actual.wMajor)
    {
      return (expected.wMinor == actual.wMinor
              && expected.wPatch < actual.wPatch)
             || (expected.wMinor < actual.wMinor);
    }
  else
    {
      return FALSE;
    }
}

CMPRESULT CompareSemVer(SEMVER ver1, SEMVER ver2)
{
  if (!strncmp(ver1.szPostfix, ver2.szPostfix, SEMVER_POSTFIX_LEN))
    {
      return CMP_NONE;
    }

  if (ver1.wMajor < ver2.wMajor)
    {
      return CMP_LESS;
    }
  else if (ver1.wMajor > ver2.wMajor)
    {
      return CMP_GREATER;
    }
  else if (ver1.wMinor < ver2.wMinor)
    {
      return CMP_LESS;
    }
  else if (ver1.wMinor > ver2.wMinor)
    {
      return CMP_GREATER;
    }
  else if (ver1.wPatch < ver2.wPatch)
    {
      return CMP_LESS;
    }
  else if (ver1.wPatch > ver2.wPatch)
    {
      return CMP_GREATER;
    }
  else
    {
      return CMP_EQ;
    }
}

void FormatSemVer(SEMVER ver, LPSTR szBuffer)
{
  if (ver.szPostfix[0]) 
    {
      sprintf(szBuffer, "%s%u.%u.%u-%s",
              ver.bExact ? "^" : "",
              ver.wMajor,
              ver.wMinor,
              ver.wPatch,
              ver.szPostfix);
    }
  else
    {
      sprintf(szBuffer, "%s%u.%u.%u",
              ver.bExact ? "^" : "",
              ver.wMajor,
              ver.wMinor,
              ver.wPatch);
    }
}

static LPCSTR ParseUint16(LPCSTR szSrc,
                          WORD *wOutput,
                          LPERROR pError)
{
  if (!isdigit((int)szSrc[0]))
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0),
                NULL, "expected numeric version");
      return NULL;
    }
  *wOutput = 0;
  while (isdigit((int)szSrc[0]))
    {
      *wOutput *= 10;
      *wOutput += szSrc[0] - '0';
      ++szSrc;
    }
  return szSrc;
}

static void ParseSemVerPostfix(LPCSTR szSrc,
                               LPSTR szOutput,
                               LPERROR pError)
{
  assert(szSrc[0] == '-');
  ++szSrc;
  if (szSrc[0] == '\0')
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "empty semver postfix");
      return;
    }
  for (size_t i = 0; i < SEMVER_POSTFIX_LEN - 1; i++)
    {
      if (!(*szOutput++ = *szSrc++))
        {
          return;
        }
    }
  if (szSrc[0] != '\0')
    {
      ErrPrintf(pError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "semver postfix too long");
      return;
    }
}

/*** ----------------------------- Run ----------------------------- ***/

typedef struct stRunContext
{
  LPPROGRAM pProgram;
  LPCOMMAND pCurCmd;
  LPVOID pUserContext;

  HMODULE hModule;
  LPLANGUAGE pLanguage;
  BOOL bOwnLanguage;
} *LPRUNCONTEXT;

static LPRUNCONTEXT CreateRunContext(LPPROGRAM pProgram);
static void DestroyRunContext(LPRUNCONTEXT pCtx);
static BOOL HandleCommand(LPRUNCONTEXT pContext,
                          LPCOMMAND pCmd,
                          LPERROR pError);
static BOOL LoadLanguage(LPRUNCONTEXT pContext,
                         LPCOMMAND pCmd,
                         LPERROR pError);
static LPLANGUAGE EasyLoad(HMODULE hModule,
                           LPCSTR *aCmdNames,
                           LPERROR pError);

void RunProgram(LPPROGRAM pProgram, LPERROR pError)
{
  LPRUNCONTEXT pContext = CreateRunContext(pProgram);
  if (pContext == NULL)
    {
      ErrPrintf(pError, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                NULL, "run: cannot allocate memory for run context");
      return;
    }

  while (HandleCommand(pContext, pContext->pCurCmd, pError))
    {
      if (IsError(pError))
        {
          break;
        }
    }

  DestroyRunContext(pContext);
}

static LPRUNCONTEXT CreateRunContext(LPPROGRAM pProgram)
{
  LPRUNCONTEXT pRet = (LPRUNCONTEXT)malloc(sizeof(struct stRunContext));
  if (pRet == NULL)
    {
      return NULL;
    }

  pRet->pProgram = pProgram;
  pRet->pCurCmd = pProgram->pCommands;
  pRet->pUserContext = NULL;
  pRet->hModule = NULL;
  pRet->pLanguage = NULL;
  return pRet;
}

static void DestroyRunContext(LPRUNCONTEXT pCtx)
{
  if (pCtx->hModule != NULL) 
    {
      if (pCtx->pLanguage != NULL)
        {
          if (pCtx->pLanguage->pfnAtexitProc != NULL)
            {
              pCtx->pLanguage->pfnAtexitProc(pCtx->pUserContext);
            }
          if (pCtx->bOwnLanguage)
            {
              free(pCtx->pLanguage->aSinvokeHandlers);
              free(pCtx->pLanguage->aWCallHandlers);
              free(pCtx->pLanguage);
            }
          pCtx->pLanguage = NULL;
        }
      if (FreeLibrary(pCtx->hModule) == 0)
        {
        fprintf(stderr, "[int/e] error invoking FreeLibrary: %ld\n",
                GetLastError());
      }
    }
  free(pCtx);
}

static BOOL HandleCommand(LPRUNCONTEXT pCtx,
                          LPCOMMAND pCmd,
                          LPERROR pError)
{
  if (pCmd == NULL)
    {
      return FALSE;
    }

  if (!strcmp(pCmd->szCmd, "language"))
    {
      return LoadLanguage(pCtx, pCmd, pError);
    }
  else if (!strcmp(pCmd->szCmd, "abort"))
    {
      return FALSE;
    }

  if (pCtx->pLanguage == NULL)
    {
      ErrPrintf(pError, PL2ERR_NO_LANG, pCmd->srcInfo, NULL,
                "no language loaded to execute user command");
      return FALSE;
    }

  for (SINVHANDLER *iter = pCtx->pLanguage->aSinvokeHandlers;
       iter != NULL && !IS_EMPTY_SINVOKE_CMD(iter);
       ++iter)
    {
      if (!iter->bRemoved && !strcmp(pCmd->szCmd, iter->szCmdName))
        {
          if (iter->bDeprecated)
            {
              fprintf(stderr, "[int/w] using deprecated command: %s\n",
                      iter->szCmdName);
            }
          if (iter->pfnHandlerProc != NULL)
            {
              iter->pfnHandlerProc((LPCSTR*)pCmd->aArgs);
            }
          pCtx->pCurCmd = pCmd->pNext;
          return TRUE;
        }
    }

  for (WCALLHANDLER *iter = pCtx->pLanguage->aWCallHandlers;
       iter != NULL && !IS_EMPTY_CMD(iter);
       ++iter)
    {
      if (!iter->bRemoved && !strcmp(pCmd->szCmd, iter->szCmdName))
        {
          if (iter->szCmdName != NULL
              && strcmp(pCmd->szCmd, iter->szCmdName) != 0)
            {
              // Do nothing if so
            }
          else if (iter->pfnRouterProc != NULL
                   && !iter->pfnRouterProc(pCmd->szCmd))
            {
              // Do nothing if so
            }
          else
            {
              if (iter->bDeprecated)
                {
                  fprintf(stderr,
                          "[int/w] using deprecated command: %s\n",
                          iter->szCmdName);
                }
              if (iter->pfnHandlerProc == NULL)
                {
                  pCtx->pCurCmd = pCmd->pNext;
                  return TRUE;
                }

              LPCOMMAND pNextCmd = iter->pfnHandlerProc
                (
                  pCtx->pProgram,
                  pCtx->pUserContext,
                  pCmd,
                  pError
                );
              if (IsError(pError))
                {
                  return 0;
                }
              if (pNextCmd == pCtx->pLanguage->pTermCmd)
                {
                  return 0;
                }
              pCtx->pCurCmd = pNextCmd ? pNextCmd : pCmd->pNext;
              return 1;
            }
        }
    }

  if (pCtx->pLanguage->pfnFallbackProc == NULL)
    {
      ErrPrintf(pError, PL2ERR_UNKNOWN_CMD, pCmd->srcInfo, NULL,
                "`%s` is not recognized as an internal or external "
                "command, operable pProgram or batch file",
                pCmd->szCmd);
      return 0;
    }

  LPCOMMAND pNextCmd = pCtx->pLanguage->pfnFallbackProc
    (
      pCtx->pProgram,
      pCtx->pUserContext,
      pCmd,
      pError
    );

  if (pNextCmd == pCtx->pLanguage->pTermCmd)
    {
      ErrPrintf(pError, PL2ERR_UNKNOWN_CMD, pCmd->srcInfo, NULL,
                "`%s` is not recognized as an internal or external "
                "command, operable pProgram or batch file",
                pCmd->szCmd);
      return 0;
    }

  if (IsError(pError))
    {
      return 0;
    }
  if (pNextCmd == pCtx->pLanguage->pTermCmd)
    {
      return 0;
    }

  pCtx->pCurCmd = pNextCmd ? pNextCmd : pCmd->pNext;
  return 1;
}

static BOOL LoadLanguage(LPRUNCONTEXT pCtx,
                         LPCOMMAND pCmd,
                         LPERROR pError)
{
  if (pCtx->pLanguage != NULL)
    {
      ErrPrintf(pError, PL2ERR_LOAD_LANG, pCmd->srcInfo, NULL,
                "language: another language already loaded");
      return FALSE;
    }

  WORD wArgCount = CountCommandArgs(pCmd);
  if (wArgCount != 2)
    {
      ErrPrintf(pError, PL2ERR_LOAD_LANG, pCmd->srcInfo, NULL,
                "language: expected 2 argument, got %u",
                wArgCount);
      return FALSE;
    }

  LPCSTR szLangId = pCmd->aArgs[0];
  SEMVER langVer = ParseSemVer(pCmd->aArgs[1], pError);
  if (IsError(pError)) 
    {
      return FALSE;
    }

  static CHAR s_szBuffer[4096];
  strcpy(s_szBuffer, "./lib");
  strcat(s_szBuffer, szLangId);
  strcat(s_szBuffer, ".dll");
  pCtx->hModule = LoadLibraryA(s_szBuffer);

  if (pCtx->hModule == NULL)
    {
      ErrPrintf(pError, PL2ERR_LOAD_LANG, pCmd->srcInfo, NULL,
                "language: cannot load language library `%s`: %ld",
                szLangId, GetLastError());
      return FALSE;
    }

  LPLOADPROC pfnLoadProc = (LPLOADPROC)GetProcAddress
    (
      pCtx->hModule,
      "LoadLanguageExtension"
    );
  if (pfnLoadProc == NULL)
    {
      LPEASYLOADPROC pfnEasyLoadProc = (LPEASYLOADPROC)GetProcAddress
        (
          pCtx->hModule,
          "EasyLoadLanguageExtension"
        );

      if (pfnEasyLoadProc == NULL)
        {
          ErrPrintf(pError, PL2ERR_LOAD_LANG, pCmd->srcInfo, NULL,
                    "language: cannot locate `%s` or `%s` "
                    "on library `%s`: %ld",
                    "LoadLanguageExtension",
                    "EasyLoadLanguageExtension",
                    szLangId,
                    GetLastError());
          return FALSE;
        }

      pCtx->pLanguage = EasyLoad(pCtx->hModule, pfnEasyLoadProc(), pError);
      if (IsError(pError))
        {
          pError->srcInfo = pCmd->srcInfo;
          return FALSE;
        }
      pCtx->bOwnLanguage = TRUE;
    }
  else
    {
      pCtx->pLanguage = pfnLoadProc(langVer, pError);
      if (IsError(pError))
        {
          pError->srcInfo = pCmd->srcInfo;
          return FALSE;
        }
      pCtx->bOwnLanguage = FALSE;
    }

  if (pCtx->pLanguage != NULL && pCtx->pLanguage->pfnInitProc != NULL)
    {
      pCtx->pUserContext = pCtx->pLanguage->pfnInitProc(pError);
      if (IsError(pError))
        {
          pError->srcInfo = pCmd->srcInfo;
          return FALSE;
        }
    }

  pCtx->pCurCmd = pCmd->pNext;
  return TRUE;
}

static LPLANGUAGE EasyLoad(HMODULE hModule,
                           LPCSTR *aCmdNames,
                           LPERROR pError)
{
  if (aCmdNames == NULL || aCmdNames[0] == NULL)
    {
      return NULL;
    }

  WORD wCount = 0;
  for (LPCSTR* iter = aCmdNames; *iter != NULL; iter++)
    {
      ++wCount;
    }

  LPLANGUAGE pRet = (LPLANGUAGE)malloc(sizeof(struct stLanguage));
  if (pRet == NULL)
    {
      ErrPrintf(pError, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                NULL,
                "language: ezload: "
                "cannot allocate memory for pl2w_Language");
      return NULL;
    }

  pRet->szLangName = "unknown";
  pRet->szLangInfo = "anonymous language loaded by ezload";
  pRet->pTermCmd = NULL;
  pRet->pfnInitProc = NULL;
  pRet->pfnAtexitProc = NULL;
  pRet->aWCallHandlers = NULL;
  pRet->pfnFallbackProc = NULL;
  pRet->aSinvokeHandlers = (SINVHANDLER*)malloc
    (
      sizeof(SINVHANDLER) * (wCount + 1)
    );
  if (pRet->aSinvokeHandlers == NULL)
    {
      ErrPrintf(pError, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                NULL,
                "langauge: EasyLoad: "
                "cannot allocate memory for "
                "LPLANGUAGE->aSinvokeHandlers");
      free(pRet);
      return NULL;
    }

  memset(pRet->aSinvokeHandlers, 0, (wCount + 1) * sizeof(SINVHANDLER));
  static CHAR szNameBuffer[512];
  for (WORD i = 0; i < wCount; i++)
    {
      LPCSTR szCmdName = aCmdNames[i];
      if (strlen(szCmdName) > 504) {
        ErrPrintf(pError, PL2ERR_LOAD_LANG, SourceInfo(NULL, 0),
                  NULL,
                  "language: EasyLoad: "
                  "name over 504 chars not supported");
        free(pRet->aSinvokeHandlers);
        free(pRet);
        return NULL;
      }
      strcpy(szNameBuffer, "EL");
      strncat(szNameBuffer, szCmdName, 504);
      void *ptr = GetProcAddress(hModule, szNameBuffer);
      if (ptr == NULL)
        {
          ErrPrintf(pError, PL2ERR_LOAD_LANG, SourceInfo(NULL, 0),
                    NULL,
                    "language: ezload: cannot load function `%s`: %ld",
                    szNameBuffer, GetLastError());
          free(pRet->aSinvokeHandlers);
          free(pRet);
          return NULL;
        }

      pRet->aSinvokeHandlers[i].szCmdName = szCmdName;
      pRet->aSinvokeHandlers[i].pfnHandlerProc = (LPSINVPROC)ptr;
      pRet->aSinvokeHandlers[i].bDeprecated = FALSE;
      pRet->aSinvokeHandlers[i].bRemoved = FALSE;
    }

  return pRet;
}
