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
  PCHAR pcStart;
  PCHAR pcEnd;
} SLICE;

typedef SLICE *LPSLICE;
typedef const SLICE* LPCSLICE;

static SLICE Slice(PCHAR pcStart, PCHAR pcEnd);
static SLICE NullSlice(void);
static LPSTR SliceIntoCStr(SLICE slice);
static BOOL IsNullSlice(SLICE slice);

SLICE Slice(PCHAR pcStart, PCHAR pcEnd)
{
  if (pcStart == pcEnd)
    {
      return (SLICE){NULL, NULL};
    }
  else
    {
      return (SLICE){pcStart, pcEnd};
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
  if (*slice.pcEnd != '\0') 
    {
      *slice.pcEnd = '\0';
    }
  return slice.pcStart;
}

static BOOL IsNullSlice(SLICE slice)
{
  return slice.pcStart == slice.pcEnd;
}

/*** ----------------- Implementation of PL2ERR ---------------- ***/

LPERROR ErrorBuffer(WORD nBufferSize)
{
  LPERROR ret = (LPERROR)malloc(sizeof(struct stError) + nBufferSize);
  if (ret == NULL)
    {
      return NULL;
    }
  memset(ret, 0, sizeof(struct stError) + nBufferSize);
  ret->nErrorBufferSize = nBufferSize;
  return ret;
}

void ErrPrintf(LPERROR lpError,
               WORD nLine,
               SRCINFO srcInfo,
               LPVOID lpExtraData,
               LPCSTR szFmt,
               ...)
{
  lpError->nLine = nLine;
  lpError->lpExtraData = lpExtraData;
  lpError->srcInfo = srcInfo;
  if (lpError->nErrorBufferSize == 0)
    {
      return;
    }

  va_list ap;
  va_start(ap, szFmt);
  vsnprintf(lpError->szReason, lpError->nErrorBufferSize, szFmt, ap);
  va_end(ap);
}

void DropError(LPERROR lpError)
{
  if (lpError->lpExtraData)
    {
      free(lpError->lpExtraData);
    }
  free(lpError);
}

BOOL IsError(LPERROR lpError)
{
  return lpError->nLine != 0;
}

/*** ------------------- Some toolkit functions -------------------- ***/

SRCINFO SourceInfo(LPCSTR lpszFileName, WORD nLine)
{
  SRCINFO ret;
  ret.lpszFileName = lpszFileName;
  ret.nLine = nLine;
  return ret;
}

LPCOMMAND CreateCommand(LPCOMMAND lpPrev,
                        LPCOMMAND lpNext,
                        LPVOID lpExtraData,
                        SRCINFO srcInfo,
                        LPSTR lpszCmd,
                        LPSTR aszArgs[])
{
  DWORD nArgCount = 0;
  for (; aszArgs[nArgCount] != NULL; ++nArgCount);

  LPCOMMAND ret = (LPCOMMAND)malloc
    (
      sizeof(struct stCommand) + (nArgCount + 1) * sizeof(LPSTR)
    );
  if (ret == NULL)
    {
      return NULL;
    }
  ret->lpPrev = lpPrev;
  if (lpPrev != NULL)
    {
      lpPrev->lpNext = ret;
    }
  ret->lpNext = lpNext;
  if (lpNext != NULL)
    {
      lpNext->lpPrev = ret;
    }
  ret->srcInfo = srcInfo;
  ret->lpszCmd = lpszCmd;
  ret->lpExtraData = lpExtraData;
  for (WORD i = 0; i < nArgCount; i++)
    {
      ret->aszArgs[i] = aszArgs[i];
    }
  ret->aszArgs[nArgCount] = NULL;
  return ret;
}

WORD CountCommandArgs(LPCOMMAND lpCmd)
{
  WORD nAcc = 0;
  LPSTR *iter = lpCmd->aszArgs;
  while (*iter != NULL)
    {
      iter++;
      nAcc++;
    }
  return nAcc;
}

/*** ---------------- Implementation of pl2w_Program --------------- ***/

void InitProgram(LPPROGRAM lpProgram)
{
  lpProgram->lpCommands = NULL;
}

void DropProgram(LPPROGRAM lpProgram)
{
  LPCOMMAND iter = lpProgram->lpCommands;
  while (iter != NULL)
    {
      LPCOMMAND lpNext = iter->lpNext;
      free(iter);
      iter = lpNext;
    }
}

void DebugPrintProgram(LPCPROGRAM lpProgram)
{
  fprintf(stderr, "program commands\n");
  LPCOMMAND lpCmd = lpProgram->lpCommands;
  while (lpCmd != NULL)
    {
      fprintf(stderr, "\t%s [", lpCmd->lpszCmd);
      for (WORD i = 0; lpCmd->aszArgs[i] != NULL; i++)
        {
          LPSTR lpszArg = lpCmd->aszArgs[i];
          fprintf(stderr, "`%s`, ", lpszArg);
        }
      fprintf(stderr, "\b\b]\n");
      lpCmd = lpCmd->lpNext;
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
  struct stProgram lpProgram;
  LPCOMMAND lpListTail;

  LPSTR lpszSrc;
  DWORD dwSrcIdx;
  PARSEMODE mode;

  SRCINFO srcInfo;

  WORD nParseBufferSize;
  WORD nParseBufferSize;
  SLICE aParseBuffer[0];
} *LPPARSECONTEXT;

static LPPARSECONTEXT CreateParseContext(LPSTR lpszSrc,
                                         WORD parseBufferSize);
static void ParseLine(LPPARSECONTEXT lpCtx, LPERROR lpError);
static void ParseQuesMark(LPPARSECONTEXT lpCtx, LPERROR lpError);
static void ParsePart(LPPARSECONTEXT lpCtx, LPERROR lpError);
static SLICE ParseId(LPPARSECONTEXT lpCtx, LPERROR lpError);
static SLICE ParseStr(LPPARSECONTEXT lpCtx, LPERROR lpError);
static void CheckBufferSize(LPPARSECONTEXT lpCtx, LPERROR lpError);
static void FinishLine(LPPARSECONTEXT lpCtx, LPERROR lpError);
static LPCOMMAND CreateCommandFS2(SRCINFO srcInfo,
                                  SLICE *aParts);
static LPCOMMAND CreateCommandFS5(LPCOMMAND lpPrev,
                                  LPCOMMAND lpNext,
                                  LPVOID lpExtraData,
                                  SRCINFO srcInfo,
                                  SLICE *aParts);
static void SkipWhitespace(LPPARSECONTEXT lpCtx);
static void SkipComment(LPPARSECONTEXT lpCtx);
static CHAR CurChar(LPPARSECONTEXT lpCtx);
static PCHAR CurCharPos(LPPARSECONTEXT lpCtx);
static void NextChar(LPPARSECONTEXT lpCtx);
static BOOL IsIdChar(CHAR ch);
static BOOL IsLineEnd(CHAR ch);
static LPSTR ShrinkConv(PCHAR pcStart, PCHAR pcEnd);

LPPROGRAM ParseProgram(LPSTR lpszSource,
                       WORD nParseBufferSize,
                       LPERROR lpError)
{
  LPPARSECONTEXT lpCtx = CreateParseContext
    (
      lpszSource,
      nParseBufferSize
    );
  if (lpCtx == NULL)
    {
      return NULL;
    }
  while (CurChar(lpCtx) != '\0')
    {
      ParseLine(lpCtx, lpError);
      if (IsError(lpError))
        {
          break;
        }
    }

  LPPROGRAM ret = (LPPROGRAM)malloc(sizeof(struct stProgram));
  memcpy(ret, &lpCtx->lpProgram, sizeof(struct stProgram));
  free(lpCtx);
  return ret;
}

static LPPARSECONTEXT CreateParseContext(LPSTR lpszSrc,
                                         WORD nParseBufferSize) {
  LPPARSECONTEXT ret = (LPPARSECONTEXT)malloc
    (
      sizeof(struct stParseContext) + nParseBufferSize * sizeof(SLICE)
    );
  if (ret == NULL) 
    {
      return NULL;
    }

  InitProgram(&ret->lpProgram);
  ret->lpListTail = NULL;
  ret->lpszSrc = lpszSrc;
  ret->dwSrcIdx = 0;
  ret->srcInfo = SourceInfo("<unknown-file>", 1);
  ret->mode = PARSE_SINGLE_LINE;

  ret->nParseBufferSize = nParseBufferSize;
  ret->nParseBufferSize = 0;
  memset(ret->aParseBuffer, 0, nParseBufferSize * sizeof(SLICE));
  return ret;
}

static void ParseLine(LPPARSECONTEXT lpCtx, LPERROR lpError) {
  if (CurChar(lpCtx) == '?')
    {
      ParseQuesMark(lpCtx, lpError);
      if (IsError(lpError))
        {
          return;
        }
    }

  while (TRUE)
    {
      SkipWhitespace(lpCtx);
      if (CurChar(lpCtx) == '\0' || CurChar(lpCtx) == '\n')
        {
          if (lpCtx->mode == PARSE_SINGLE_LINE)
            {
              FinishLine(lpCtx, lpError);
            }
          if (lpCtx->mode == PARSE_MULTI_LINE && CurChar(lpCtx) == '\0')
            {
              ErrPrintf(lpError, PL2ERR_UNCLOSED_BEGIN, lpCtx->srcInfo,
                        NULL, "unclosed `?begin` block");
            }
          if (CurChar(lpCtx) == '\n')
            {
              NextChar(lpCtx);
            }
          return;
        }
      else if (CurChar(lpCtx) == '#')
        {
          SkipComment(lpCtx);
        }
      else
        {
          ParsePart(lpCtx, lpError);
          if (IsError(lpError))
            {
              return;
            }
        }
  }
}

static void ParseQuesMark(LPPARSECONTEXT lpCtx, LPERROR lpError)
{
  assert(CurChar(lpCtx) == '?');
  NextChar(lpCtx);

  PCHAR pcStart = CurCharPos(lpCtx);
  while (isalnum((int)CurChar(lpCtx)))
    {
      NextChar(lpCtx);
    }
  PCHAR pcEnd = CurCharPos(lpCtx);
  SLICE s = Slice(pcStart, pcEnd);
  LPSTR szQuesCommand = SliceIntoCStr(s);
  
  if (!strcmp(szQuesCommand, "begin")) 
    {
      lpCtx->mode = PARSE_MULTI_LINE;
    }
  else if (!strcmp(szQuesCommand, "end"))
    {
      lpCtx->mode = PARSE_SINGLE_LINE;
      FinishLine(lpCtx, lpError);
    }
  else
    {
      ErrPrintf(lpError, PL2ERR_UNKNOWN_QUES, lpCtx->srcInfo,
                NULL, "unknown question mark operator: `%s`",
                szQuesCommand);
    }
}

static void ParsePart(LPPARSECONTEXT lpCtx, LPERROR lpError)
{
  SLICE part;
  if (CurChar(lpCtx) == '"' || CurChar(lpCtx) == '\'')
    {
      part = ParseStr(lpCtx, lpError);
    }
  else
    {
      part = ParseId(lpCtx, lpError);
    }
  if (IsError(lpError))
    {
      return;
    }

  CheckBufferSize(lpCtx, lpError);
  if (IsError(lpError))
    {
      return;
    }

  lpCtx->aParseBuffer[lpCtx->nParseBufferSize++] = part;
}

static SLICE ParseId(LPPARSECONTEXT lpCtx, LPERROR lpError)
{
  (void)lpError;
  PCHAR pcStart = CurCharPos(lpCtx);
  while (IsIdChar(CurChar(lpCtx)))
    {
      NextChar(lpCtx);
    }
  PCHAR pcEnd = CurCharPos(lpCtx);
  return Slice(pcStart, pcEnd);
}

static SLICE ParseStr(LPPARSECONTEXT lpCtx, LPERROR lpError)
{
  assert(CurChar(lpCtx) == '"' || CurChar(lpCtx) == '\'');
  NextChar(lpCtx);

  PCHAR pcStart = CurCharPos(lpCtx);
  while (CurChar(lpCtx) != '"'
         && CurChar(lpCtx) != '\''
         && !IsLineEnd(CurChar(lpCtx)))
    {
      if (CurChar(lpCtx) == '\\')
        {
          NextChar(lpCtx);
          NextChar(lpCtx);
        }
      else
        {
          NextChar(lpCtx);
        }
    }

  PCHAR pcEnd = CurCharPos(lpCtx);
  pcEnd = ShrinkConv(pcStart, pcEnd);

  if (CurChar(lpCtx) == '"' || CurChar(lpCtx) == '\'')
    {
      NextChar(lpCtx);
    }
  else
    {
      ErrPrintf(lpError, PL2ERR_UNCLOSED_BEGIN, lpCtx->srcInfo,
                NULL, "unclosed string literal");
      return NullSlice();
    }
  return Slice(pcStart, pcEnd);
}

static void CheckBufferSize(LPPARSECONTEXT lpCtx, LPERROR lpError)
{
  if (lpCtx->nParseBufferSize <= lpCtx->nParseBufferSize + 1)
    {
      ErrPrintf(lpError, PL2ERR_UNCLOSED_BEGIN, lpCtx->srcInfo,
                NULL, "command parts exceed internal parsing buffer");
    }
}

static void FinishLine(LPPARSECONTEXT lpCtx, LPERROR lpError)
{
  (void)lpError;

  SRCINFO srcInfo = lpCtx->srcInfo;
  NextChar(lpCtx);
  if (lpCtx->nParseBufferSize == 0)
    {
      return;
    }
  if (lpCtx->lpListTail == NULL)
    {
      assert(lpCtx->lpProgram.lpCommands == NULL);
      lpCtx->lpProgram.lpCommands = lpCtx->lpListTail = CreateCommandFS2
        (
          lpCtx->srcInfo,
          lpCtx->aParseBuffer
        );
    }
  else
    {
      lpCtx->lpListTail = CreateCommandFS5
        (
          lpCtx->lpListTail,
          NULL,
          NULL,
          lpCtx->srcInfo,
          lpCtx->aParseBuffer
        );
    }
  if (lpCtx->lpListTail == NULL)
    {
      ErrPrintf(lpError, PL2ERR_MALLOC, srcInfo, 0,
                "failed allocating COMMAND");
    }
  memset(lpCtx->aParseBuffer, 0, sizeof(SLICE) * lpCtx->nParseBufferSize);
  lpCtx->nParseBufferSize = 0;
}

static LPCOMMAND CreateCommandFS2(SRCINFO srcInfo,
                                  SLICE *aParts)
{
  return CreateCommandFS5(NULL, NULL, NULL, srcInfo, aParts);
}

static LPCOMMAND CreateCommandFS5(LPCOMMAND lpPrev,
                                  LPCOMMAND lpNext,
                                  LPVOID lpExtraData,
                                  SRCINFO srcInfo,
                                  SLICE *aParts)
{
  WORD nPartCount = 0;
  for (; !IsNullSlice(aParts[nPartCount]); ++nPartCount);

  LPCOMMAND ret = (LPCOMMAND)malloc
    (
      sizeof(struct stCommand) + nPartCount * sizeof(LPSTR)
    );
  if (ret == NULL)
    {
      return NULL;
    }

  ret->lpPrev = lpPrev;
  if (lpPrev != NULL)
    {
      lpPrev->lpNext = ret;
    }
  ret->lpNext = lpNext;
  if (lpNext != NULL)
    {
      lpNext->lpPrev = ret;
    }
  ret->lpExtraData = lpExtraData;
  ret->srcInfo = srcInfo;
  ret->lpszCmd = SliceIntoCStr(aParts[0]);
  for (WORD i = 1; i < nPartCount; i++)
    {
      ret->aszArgs[i - 1] = SliceIntoCStr(aParts[i]);
    }
  ret->aszArgs[nPartCount - 1] = NULL;
  return ret;
}

static void SkipWhitespace(LPPARSECONTEXT lpCtx)
{
  while (1)
    {
      switch (CurChar(lpCtx))
        {
          case ' ': case '\t': case '\f': case '\v': case '\r':
            NextChar(lpCtx);
            break;
          default:
            return;
        }
    }
}

static void SkipComment(LPPARSECONTEXT lpCtx)
{
  assert(CurChar(lpCtx) == '#');
  NextChar(lpCtx);

  while (!IsLineEnd(CurChar(lpCtx)))
    {
      NextChar(lpCtx);
    }

  if (CurChar(lpCtx) == '\n')
    {
      NextChar(lpCtx);
    }
}

static CHAR CurChar(LPPARSECONTEXT lpCtx)
{
  return lpCtx->lpszSrc[lpCtx->dwSrcIdx];
}

static PCHAR CurCharPos(LPPARSECONTEXT lpCtx)
{
  return lpCtx->lpszSrc + lpCtx->dwSrcIdx;
}

static void NextChar(LPPARSECONTEXT lpCtx)
{
  if (lpCtx->lpszSrc[lpCtx->dwSrcIdx] == '\0')
    {
      return;
    }
  else
    {
      if (lpCtx->lpszSrc[lpCtx->dwSrcIdx] == '\n')
        {
          lpCtx->srcInfo.nLine += 1;
        }
      lpCtx->dwSrcIdx += 1;
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

static LPSTR ShrinkConv(PCHAR pcStart, PCHAR pcEnd)
{
  PCHAR iter1 = pcStart, iter2 = pcStart;
  while (iter1 != pcEnd)
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

static LPCSTR ParseUint16(LPCSTR lpszSrc,
                          WORD *lpnOutput,
                          LPERROR lpError);

static void ParseSemVerPostfix(LPCSTR lpszSrc,
                               LPSTR lpszOutput,
                               LPERROR lpError);

SEMVER ZeroVersion(void)
{
  SEMVER ret;
  ret.nMajor = 0;
  ret.nMinor = 0;
  ret.nPatch = 0;
  memset(ret.szPostfix, 0, SEMVER_POSTFIX_LEN);
  ret.bExact = FALSE;
  return ret;
}

BOOL IsZeroVersion(SEMVER ver)
{
  return ver.nMajor == 0
         && ver.nMinor == 0
         && ver.nPatch == 0
         && ver.szPostfix[0] == 0
         && ver.bExact == 0;
}

BOOL IsAlpha(SEMVER ver)
{
  return ver.szPostfix[0] != '\0';
}

BOOL IsStable(SEMVER ver)
{
  return !IsAlpha(ver) && ver.nMajor != 0;
}

SEMVER ParseSemVer(LPCSTR lpszSrc, LPERROR lpError)
{
  SEMVER ret = ZeroVersion();
  if (lpszSrc[0] == '^')
    {
      ret.bExact = 1;
      lpszSrc++;
    }

  lpszSrc = ParseUint16(lpszSrc, &ret.nMajor, lpError);
  if (IsError(lpError))
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "missing major version");
      goto done;
    }
  else if (lpszSrc[0] == '\0')
    {
      goto done;
    }
  else if (lpszSrc[0] == '-')
    {
      goto parse_postfix;
    }
  else if (lpszSrc[0] != '.')
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "expected `.`, got `%c`", lpszSrc[0]);
      goto done;
    }
  lpszSrc++;
  lpszSrc = ParseUint16(lpszSrc, &ret.nMinor, lpError);
  if (IsError(lpError))
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0),
                NULL, "missing minor version");
      goto done;
    }
  else if (lpszSrc[0] == '\0')
    {
      goto done;
    }
  else if (lpszSrc[0] == '-')
    {
      goto parse_postfix;
    }
  else if (lpszSrc[0] != '.')
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "expected `.`, got `%c`", lpszSrc[0]);
      goto done;
    }

  lpszSrc++;
  lpszSrc = ParseUint16(lpszSrc, &ret.nPatch, lpError);
  if (IsError(lpError))
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "missing patch version");
      goto done;
    }
  else if (lpszSrc[0] == '\0')
    {
      goto done;
    }
  else if (lpszSrc[0] != '-')
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "unterminated semver, "
                "expected `-` or `\\0`, got `%c`",
                lpszSrc[0]);
      goto done;
    }

parse_postfix:
  ParseSemVerPostfix(lpszSrc, ret.szPostfix, lpError);

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
      return expected.nMajor == actual.nMajor
             && expected.nMinor == actual.nMinor
             && expected.nPatch == actual.nPatch;
    }
  else if (expected.nMajor == actual.nMajor)
    {
      return (expected.nMinor == actual.nMinor
              && expected.nPatch < actual.nPatch)
             || (expected.nMinor < actual.nMinor);
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

  if (ver1.nMajor < ver2.nMajor)
    {
      return CMP_LESS;
    }
  else if (ver1.nMajor > ver2.nMajor)
    {
      return CMP_GREATER;
    }
  else if (ver1.nMinor < ver2.nMinor)
    {
      return CMP_LESS;
    }
  else if (ver1.nMinor > ver2.nMinor)
    {
      return CMP_GREATER;
    }
  else if (ver1.nPatch < ver2.nPatch)
    {
      return CMP_LESS;
    }
  else if (ver1.nPatch > ver2.nPatch)
    {
      return CMP_GREATER;
    }
  else
    {
      return CMP_EQ;
    }
}

void FormatSemVer(SEMVER ver, LPSTR lpszBuffer)
{
  if (ver.szPostfix[0]) 
    {
      sprintf(lpszBuffer, "%s%u.%u.%u-%s",
              ver.bExact ? "^" : "",
              ver.nMajor,
              ver.nMinor,
              ver.nPatch,
              ver.szPostfix);
    }
  else
    {
      sprintf(lpszBuffer, "%s%u.%u.%u",
              ver.bExact ? "^" : "",
              ver.nMajor,
              ver.nMinor,
              ver.nPatch);
    }
}

static LPCSTR ParseUint16(LPCSTR lpszSrc,
                          WORD *lpnOutput,
                          LPERROR lpError)
{
  if (!isdigit((int)lpszSrc[0]))
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0),
                NULL, "expected numeric version");
      return NULL;
    }
  *lpnOutput = 0;
  while (isdigit((int)lpszSrc[0]))
    {
      *lpnOutput *= 10;
      *lpnOutput += lpszSrc[0] - '0';
      ++lpszSrc;
    }
  return lpszSrc;
}

static void ParseSemVerPostfix(LPCSTR lpszSrc,
                               LPSTR lpszOutput,
                               LPERROR lpError)
{
  assert(lpszSrc[0] == '-');
  ++lpszSrc;
  if (lpszSrc[0] == '\0')
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "empty semver postfix");
      return;
    }
  for (size_t i = 0; i < SEMVER_POSTFIX_LEN - 1; i++)
    {
      if (!(*lpszOutput++ = *lpszSrc++))
        {
          return;
        }
    }
  if (lpszSrc[0] != '\0')
    {
      ErrPrintf(lpError, PL2ERR_SEMVER_PARSE,
                SourceInfo(NULL, 0), NULL,
                "semver postfix too long");
      return;
    }
}

/*** ----------------------------- Run ----------------------------- ***/

typedef struct stRunContext
{
  LPPROGRAM lpProgram;
  LPCOMMAND lpCurCmd;
  LPVOID lpUserContext;

  HMODULE hModule;
  LPLANGUAGE lpLanguage;
  BOOL bOwnLanguage;
} *LPRUNCONTEXT;

static LPRUNCONTEXT CreateRunContext(LPPROGRAM lpProgram);
static void DestroyRunContext(LPRUNCONTEXT lpCtx);
static BOOL HandleCommand(LPRUNCONTEXT lpContext,
                          LPCOMMAND lpCmd,
                          LPERROR lpError);
static BOOL LoadLanguage(LPRUNCONTEXT lpContext,
                         LPCOMMAND lpCmd,
                         LPERROR lpError);
static LPLANGUAGE EasyLoad(HMODULE hModule,
                           LPCSTR *aszCmdNames,
                           LPERROR lpError);

void RunProgram(LPPROGRAM lpProgram, LPERROR lpError)
{
  LPRUNCONTEXT lpContext = CreateRunContext(lpProgram);
  if (lpContext == NULL)
    {
      ErrPrintf(lpError, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                NULL, "run: cannot allocate memory for run context");
      return;
    }

  while (HandleCommand(lpContext, lpContext->lpCurCmd, lpError))
    {
      if (IsError(lpError))
        {
          break;
        }
    }

  DestroyRunContext(lpContext);
}

static LPRUNCONTEXT CreateRunContext(LPPROGRAM lpProgram)
{
  LPRUNCONTEXT ret = (LPRUNCONTEXT)malloc(sizeof(struct stRunContext));
  if (ret == NULL)
    {
      return NULL;
    }

  ret->lpProgram = lpProgram;
  ret->lpCurCmd = lpProgram->lpCommands;
  ret->lpUserContext = NULL;
  ret->hModule = NULL;
  ret->lpLanguage = NULL;
  return ret;
}

static void DestroyRunContext(LPRUNCONTEXT lpCtx)
{
  if (lpCtx->hModule != NULL) 
    {
      if (lpCtx->lpLanguage != NULL)
        {
          if (lpCtx->lpLanguage->lpfnAtexitProc != NULL)
            {
              lpCtx->lpLanguage->lpfnAtexitProc(lpCtx->lpUserContext);
            }
          if (lpCtx->bOwnLanguage)
            {
              free(lpCtx->lpLanguage->aSinvokeHandlers);
              free(lpCtx->lpLanguage->aWCallHandlers);
              free(lpCtx->lpLanguage);
            }
          lpCtx->lpLanguage = NULL;
        }
      if (FreeLibrary(lpCtx->hModule) == 0)
        {
        fprintf(stderr, "[int/e] error invoking FreeLibrary: %ld\n",
                GetLastError());
      }
    }
  free(lpCtx);
}

static BOOL HandleCommand(LPRUNCONTEXT lpCtx,
                          LPCOMMAND lpCmd,
                          LPERROR lpError)
{
  if (lpCmd == NULL)
    {
      return FALSE;
    }

  if (!strcmp(lpCmd->lpszCmd, "language"))
    {
      return LoadLanguage(lpCtx, lpCmd, lpError);
    }
  else if (!strcmp(lpCmd->lpszCmd, "abort"))
    {
      return FALSE;
    }

  if (lpCtx->lpLanguage == NULL)
    {
      ErrPrintf(lpError, PL2ERR_NO_LANG, lpCmd->srcInfo, NULL,
                "no language loaded to execute user command");
      return FALSE;
    }

  for (SINVHANDLER *iter = lpCtx->lpLanguage->aSinvokeHandlers;
       iter != NULL && !IS_EMPTY_SINVOKE_CMD(iter);
       ++iter)
    {
      if (!iter->bRemoved && !strcmp(lpCmd->lpszCmd, iter->lpszCmdName))
        {
          if (iter->bDeprecated)
            {
              fprintf(stderr, "[int/w] using deprecated command: %s\n",
                      iter->lpszCmdName);
            }
          if (iter->lpfnHandlerProc != NULL)
            {
              iter->lpfnHandlerProc((LPCSTR*)lpCmd->aszArgs);
            }
          lpCtx->lpCurCmd = lpCmd->lpNext;
          return TRUE;
        }
    }

  for (WCALLHANDLER *iter = lpCtx->lpLanguage->aWCallHandlers;
       iter != NULL && !IS_EMPTY_CMD(iter);
       ++iter)
    {
      if (!iter->bRemoved && !strcmp(lpCmd->lpszCmd, iter->lpszCmdName))
        {
          if (iter->lpszCmdName != NULL
              && strcmp(lpCmd->lpszCmd, iter->lpszCmdName) != 0)
            {
              // Do nothing if so
            }
          else if (iter->lpfnRouterProc != NULL
                   && !iter->lpfnRouterProc(lpCmd->lpszCmd))
            {
              // Do nothing if so
            }
          else
            {
              if (iter->bDeprecated)
                {
                  fprintf(stderr,
                          "[int/w] using deprecated command: %s\n",
                          iter->lpszCmdName);
                }
              if (iter->lpfnHandlerProc == NULL)
                {
                  lpCtx->lpCurCmd = lpCmd->lpNext;
                  return TRUE;
                }

              LPCOMMAND pNextCmd = iter->lpfnHandlerProc
                (
                  lpCtx->lpProgram,
                  lpCtx->lpUserContext,
                  lpCmd,
                  lpError
                );
              if (IsError(lpError))
                {
                  return 0;
                }
              if (pNextCmd == lpCtx->lpLanguage->lpTermCmd)
                {
                  return 0;
                }
              lpCtx->lpCurCmd = pNextCmd ? pNextCmd : lpCmd->lpNext;
              return 1;
            }
        }
    }

  if (lpCtx->lpLanguage->lpfnFallbackProc == NULL)
    {
      ErrPrintf(lpError, PL2ERR_UNKNOWN_CMD, lpCmd->srcInfo, NULL,
                "`%s` is not recognized as an internal or external "
                "command, operable lpProgram or batch file",
                lpCmd->lpszCmd);
      return 0;
    }

  LPCOMMAND pNextCmd = lpCtx->lpLanguage->lpfnFallbackProc
    (
      lpCtx->lpProgram,
      lpCtx->lpUserContext,
      lpCmd,
      lpError
    );

  if (pNextCmd == lpCtx->lpLanguage->lpTermCmd)
    {
      ErrPrintf(lpError, PL2ERR_UNKNOWN_CMD, lpCmd->srcInfo, NULL,
                "`%s` is not recognized as an internal or external "
                "command, operable lpProgram or batch file",
                lpCmd->lpszCmd);
      return 0;
    }

  if (IsError(lpError))
    {
      return 0;
    }
  if (pNextCmd == lpCtx->lpLanguage->lpTermCmd)
    {
      return 0;
    }

  lpCtx->lpCurCmd = pNextCmd ? pNextCmd : lpCmd->lpNext;
  return 1;
}

static BOOL LoadLanguage(LPRUNCONTEXT lpCtx,
                         LPCOMMAND lpCmd,
                         LPERROR lpError)
{
  if (lpCtx->lpLanguage != NULL)
    {
      ErrPrintf(lpError, PL2ERR_LOAD_LANG, lpCmd->srcInfo, NULL,
                "language: another language already loaded");
      return FALSE;
    }

  WORD nArgCount = CountCommandArgs(lpCmd);
  if (nArgCount != 2)
    {
      ErrPrintf(lpError, PL2ERR_LOAD_LANG, lpCmd->srcInfo, NULL,
                "language: expected 2 argument, got %u",
                nArgCount);
      return FALSE;
    }

  LPCSTR lpszLangId = lpCmd->aszArgs[0];
  SEMVER langVer = ParseSemVer(lpCmd->aszArgs[1], lpError);
  if (IsError(lpError)) 
    {
      return FALSE;
    }

  static CHAR s_szBuffer[4096];
  strcpy(s_szBuffer, "./lib");
  strcat(s_szBuffer, lpszLangId);
  strcat(s_szBuffer, ".dll");
  lpCtx->hModule = LoadLibraryA(s_szBuffer);

  if (lpCtx->hModule == NULL)
    {
      ErrPrintf(lpError, PL2ERR_LOAD_LANG, lpCmd->srcInfo, NULL,
                "language: cannot load language library `%s`: %ld",
                lpszLangId, GetLastError());
      return FALSE;
    }

  LPLOADPROC lpfnLoadProc = (LPLOADPROC)GetProcAddress
    (
      lpCtx->hModule,
      "LoadLanguageExtension"
    );
  if (lpfnLoadProc == NULL)
    {
      LPEASYLOADPROC lpfnEasyLoadProc = (LPEASYLOADPROC)GetProcAddress
        (
          lpCtx->hModule,
          "EasyLoadLanguageExtension"
        );

      if (lpfnEasyLoadProc == NULL)
        {
          ErrPrintf(lpError, PL2ERR_LOAD_LANG, lpCmd->srcInfo, NULL,
                    "language: cannot locate `%s` or `%s` "
                    "on library `%s`: %ld",
                    "LoadLanguageExtension",
                    "EasyLoadLanguageExtension",
                    lpszLangId,
                    GetLastError());
          return FALSE;
        }

      lpCtx->lpLanguage = EasyLoad(lpCtx->hModule, lpfnEasyLoadProc(), lpError);
      if (IsError(lpError))
        {
          lpError->srcInfo = lpCmd->srcInfo;
          return FALSE;
        }
      lpCtx->bOwnLanguage = TRUE;
    }
  else
    {
      lpCtx->lpLanguage = lpfnLoadProc(langVer, lpError);
      if (IsError(lpError))
        {
          lpError->srcInfo = lpCmd->srcInfo;
          return FALSE;
        }
      lpCtx->bOwnLanguage = FALSE;
    }

  if (lpCtx->lpLanguage != NULL && lpCtx->lpLanguage->lpfnInitProc != NULL)
    {
      lpCtx->lpUserContext = lpCtx->lpLanguage->lpfnInitProc(lpError);
      if (IsError(lpError))
        {
          lpError->srcInfo = lpCmd->srcInfo;
          return FALSE;
        }
    }

  lpCtx->lpCurCmd = lpCmd->lpNext;
  return TRUE;
}

static LPLANGUAGE EasyLoad(HMODULE hModule,
                           LPCSTR *aszCmdNames,
                           LPERROR lpError)
{
  if (aszCmdNames == NULL || aszCmdNames[0] == NULL)
    {
      return NULL;
    }

  WORD wCount = 0;
  for (LPCSTR* iter = aszCmdNames; *iter != NULL; iter++)
    {
      ++wCount;
    }

  LPLANGUAGE ret = (LPLANGUAGE)malloc(sizeof(struct stLanguage));
  if (ret == NULL)
    {
      ErrPrintf(lpError, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                NULL,
                "language: ezload: "
                "cannot allocate memory for pl2w_Language");
      return NULL;
    }

  ret->lpszLangName = "unknown";
  ret->lpszLangInfo = "anonymous language loaded by ezload";
  ret->lpTermCmd = NULL;
  ret->lpfnInitProc = NULL;
  ret->lpfnAtexitProc = NULL;
  ret->aWCallHandlers = NULL;
  ret->lpfnFallbackProc = NULL;
  ret->aSinvokeHandlers = (SINVHANDLER*)malloc
    (
      sizeof(SINVHANDLER) * (wCount + 1)
    );
  if (ret->aSinvokeHandlers == NULL)
    {
      ErrPrintf(lpError, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                NULL,
                "langauge: EasyLoad: "
                "cannot allocate memory for "
                "LPLANGUAGE->aSinvokeHandlers");
      free(ret);
      return NULL;
    }

  memset(ret->aSinvokeHandlers, 0, (wCount + 1) * sizeof(SINVHANDLER));
  static CHAR szNameBuffer[512];
  for (WORD i = 0; i < wCount; i++)
    {
      LPCSTR lpszCmdName = aszCmdNames[i];
      if (strlen(lpszCmdName) > 504) {
        ErrPrintf(lpError, PL2ERR_LOAD_LANG, SourceInfo(NULL, 0),
                  NULL,
                  "language: EasyLoad: "
                  "name over 504 chars not supported");
        free(ret->aSinvokeHandlers);
        free(ret);
        return NULL;
      }
      strcpy(szNameBuffer, "EL");
      strncat(szNameBuffer, lpszCmdName, 504);
      void *ptr = GetProcAddress(hModule, szNameBuffer);
      if (ptr == NULL)
        {
          ErrPrintf(lpError, PL2ERR_LOAD_LANG, SourceInfo(NULL, 0),
                    NULL,
                    "language: ezload: cannot load function `%s`: %ld",
                    szNameBuffer, GetLastError());
          free(ret->aSinvokeHandlers);
          free(ret);
          return NULL;
        }

      ret->aSinvokeHandlers[i].lpszCmdName = lpszCmdName;
      ret->aSinvokeHandlers[i].lpfnHandlerProc = (LPSINVPROC)ptr;
      ret->aSinvokeHandlers[i].bDeprecated = FALSE;
      ret->aSinvokeHandlers[i].bRemoved = FALSE;
    }

  return ret;
}
