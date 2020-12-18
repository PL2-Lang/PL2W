#include "pl2w.h"

#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>

/*** ----------------- Transmute any char to uchar ----------------- ***/

static unsigned char transmuteU8(char i8) {
  return *(unsigned char*)(&i8);
}

/*** ------------------- Implementation of slice ------------------- ***/

typedef struct st_slice {
  char *start;
  char *end;
} Slice;

static Slice slice(char *start, char *end);
static Slice nullSlice(void);
static char *sliceIntoCStr(Slice slice);
static _Bool isNullSlice(Slice slice);

Slice slice(char *start, char *end) {
  Slice ret;
  if (start == end) {
    ret.start = NULL;
    ret.end = NULL;
  } else {
    ret.start = start;
    ret.end = end;
  }
  return ret;
}

static Slice nullSlice(void) {
  Slice ret;
  ret.start = NULL;
  ret.end = NULL;
  return ret;
}

static char *sliceIntoCStr(Slice slice) {
  if (isNullSlice(slice)) {
    return NULL;
  }
  if (*slice.end != '\0') {
    *slice.end = '\0';
  }
  return slice.start;
}

static _Bool isNullSlice(Slice slice) {
  return slice.start == slice.end;
}

/*** ----------------- Implementation of PL2ERR ---------------- ***/

PL2ERR *ErrorBuffer(uint16_t strBufferSize) {
  PL2ERR *ret = (PL2ERR*)malloc(sizeof(PL2ERR) + strBufferSize);
  if (ret == NULL) {
    return NULL;
  }
  memset(ret, 0, sizeof(PL2ERR) + strBufferSize);
  ret->errorBufferSize = strBufferSize;
  return ret;
}

void ErrPrintf(PL2ERR *error,
                    uint16_t errorCode,
                    SRCINFO sourceInfo,
                    void *extraData,
                    const char *fmt,
                    ...) {
  error->errorCode = errorCode;
  error->extraData = extraData;
  error->sourceInfo = sourceInfo;
  if (error->errorBufferSize == 0) {
    return;
  }

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(error->reason, error->errorBufferSize, fmt, ap);
  va_end(ap);
}

void DropError(PL2ERR *error) {
  if (error->extraData) {
    free(error->extraData);
  }
  free(error);
}

_Bool IsError(PL2ERR *error) {
  return error->errorCode != 0;
}

/*** ------------------- Some toolkit functions -------------------- ***/

SRCINFO SourceInfo(const char *fileName, uint16_t line) {
  SRCINFO ret;
  ret.fileName = fileName;
  ret.line = line;
  return ret;
}

COMMAND *pl2w_cmd3(SRCINFO sourceInfo,
                    char *cmd,
                    char *args[]) {
  return pl2w_cmd6(NULL, NULL, NULL, sourceInfo, cmd, args);
}

COMMAND *pl2w_cmd6(COMMAND *prev,
                    COMMAND *next,
                    void *extraData,
                    SRCINFO sourceInfo,
                    char *cmd,
                    char *args[]) {
  uint16_t argLen = 0;
  for (; args[argLen] != NULL; ++argLen);

  COMMAND *ret = (COMMAND*)malloc(sizeof(COMMAND) +
                                    (argLen + 1) * sizeof(char*));
  if (ret == NULL) {
    return NULL;
  }
  ret->prev = prev;
  if (prev != NULL) {
    prev->next = ret;
  }
  ret->next = next;
  if (next != NULL) {
    next->prev = ret;
  }
  ret->sourceInfo = sourceInfo;
  ret->cmd = cmd;
  ret->extraData = extraData;
  for (uint16_t i = 0; i < argLen; i++) {
    ret->args[i] = args[i];
  }
  ret->args[argLen] = NULL;
  return ret;
}

uint16_t pl2w_argsLen(COMMAND *cmd) {
  uint16_t acc = 0;
  char **iter = cmd->args;
  while (*iter != NULL) {
    iter++;
    acc++;
  }
  return acc;
}

/*** ---------------- Implementation of pl2w_Program --------------- ***/

void pl2w_initProgram(pl2w_Program *program) {
  program->commands = NULL;
}

void pl2w_dropProgram(pl2w_Program *program) {
  COMMAND *iter = program->commands;
  while (iter != NULL) {
    COMMAND *next = iter->next;
    free(iter);
    iter = next;
  }
}

void pl2w_debugPrintProgram(const pl2w_Program *program) {
  fprintf(stderr, "program commands\n");
  COMMAND *cmd = program->commands;
  while (cmd != NULL) {
    fprintf(stderr, "\t%s [", cmd->cmd);
    for (uint16_t i = 0; cmd->args[i] != NULL; i++) {
      char *arg = cmd->args[i];
      fprintf(stderr, "`%s`, ", arg);
    }
    fprintf(stderr, "\b\b]\n");
    cmd = cmd->next;
  }
  fprintf(stderr, "end program commands\n");
}

/*** ----------------- Implementation of pl2w_parse ---------------- ***/

typedef enum e_parse_mode {
  PARSE_SINGLE_LINE = 1,
  PARSE_MULTI_LINE  = 2
} ParseMode;

typedef enum e_ques_cmd {
  QUES_INVALID = 0,
  QUES_BEGIN   = 1,
  QUES_END     = 2
} QuesCmd;

typedef struct st_parse_context {
  pl2w_Program program;
  COMMAND *listTail;

  char *src;
  uint32_t srcIdx;
  ParseMode mode;

  SRCINFO sourceInfo;

  uint32_t parseBufferSize;
  uint32_t parseBufferUsage;
  Slice parseBuffer[0];
} ParseContext;

static ParseContext *createParseContext(char *src,
                                        uint16_t parseBufferSize);
static void parseLine(ParseContext *ctx, PL2ERR *error);
static void parseQuesMark(ParseContext *ctx, PL2ERR *error);
static void parsePart(ParseContext *ctx, PL2ERR *error);
static Slice parseId(ParseContext *ctx, PL2ERR *error);
static Slice parseStr(ParseContext *ctx, PL2ERR *error);
static void checkBufferSize(ParseContext *ctx, PL2ERR *error);
static void finishLine(ParseContext *ctx, PL2ERR *error);
static COMMAND *cmdFromSlices2(SRCINFO sourceInfo,
                                Slice *parts);
static COMMAND *cmdFromSlices5(COMMAND *prev,
                                COMMAND *next,
                                void *extraData,
                                SRCINFO sourceInfo,
                                Slice *parts);
static void skipWhitespace(ParseContext *ctx);
static void skipComment(ParseContext *ctx);
static char curChar(ParseContext *ctx);
static char *curCharPos(ParseContext *ctx);
static void nextChar(ParseContext *ctx);
static _Bool isIdChar(char ch);
static _Bool isLineEnd(char ch);
static char *shrinkConv(char *start, char *end);

pl2w_Program pl2w_parse(char *source,
                        uint16_t parseBufferSize,
                        PL2ERR *error) {
  ParseContext *context = createParseContext(source, parseBufferSize);

  while (curChar(context) != '\0') {
    parseLine(context, error);
    if (IsError(error)) {
      break;
    }
  }

  pl2w_Program ret = context->program;
  free(context);
  return ret;
}

static ParseContext *createParseContext(char *src,
                                        uint16_t parseBufferSize) {
  ParseContext *ret = (ParseContext*)malloc(
    sizeof(ParseContext) + parseBufferSize * sizeof(Slice)
  );
  if (ret == NULL) {
    return NULL;
  }

  pl2w_initProgram(&ret->program);
  ret->listTail = NULL;
  ret->src = src;
  ret->srcIdx = 0;
  ret->sourceInfo = SourceInfo("<unknown-file>", 1);
  ret->mode = PARSE_SINGLE_LINE;

  ret->parseBufferSize = parseBufferSize;
  ret->parseBufferUsage = 0;
  memset(ret->parseBuffer, 0, parseBufferSize * sizeof(Slice));
  return ret;
}

static void parseLine(ParseContext *ctx, PL2ERR *error) {
  if (curChar(ctx) == '?') {
    parseQuesMark(ctx, error);
    if (IsError(error)) {
      return;
    }
  }

  while (1) {
    skipWhitespace(ctx);
    if (curChar(ctx) == '\0' || curChar(ctx) == '\n') {
      if (ctx->mode == PARSE_SINGLE_LINE) {
        finishLine(ctx, error);
      }
      if (ctx->mode == PARSE_MULTI_LINE && curChar(ctx) == '\0') {
        ErrPrintf(error, PL2ERR_UNCLOSED_BEGIN, ctx->sourceInfo,
                       NULL, "unclosed `?begin` block");
      }
      if (curChar(ctx) == '\n') {
        nextChar(ctx);
      }
      return;
    } else if (curChar(ctx) == '#') {
      skipComment(ctx);
    } else {
      parsePart(ctx, error);
      if (IsError(error)) {
        return;
      }
    }
  }
}

static void parseQuesMark(ParseContext *ctx, PL2ERR *error) {
  assert(curChar(ctx) == '?');
  nextChar(ctx);

  char *start = curCharPos(ctx);
  while (isalnum((int)curChar(ctx))) {
    nextChar(ctx);
  }
  char *end = curCharPos(ctx);
  Slice s = slice(start, end);
  char *cstr = sliceIntoCStr(s);

  if (!strcmp(cstr, "begin")) {
    ctx->mode = PARSE_MULTI_LINE;
  } else if (!strcmp(cstr, "end")) {
    ctx->mode = PARSE_SINGLE_LINE;
    finishLine(ctx, error);
  } else {
    ErrPrintf(error, PL2ERR_UNKNOWN_QUES, ctx->sourceInfo,
                   NULL, "unknown question mark operator: `%s`", cstr);
  }
}

static void parsePart(ParseContext *ctx, PL2ERR *error) {
  Slice part;
  if (curChar(ctx) == '"' || curChar(ctx) == '\'') {
    part = parseStr(ctx, error);
  } else {
    part = parseId(ctx, error);
  }
  if (IsError(error)) {
    return;
  }

  checkBufferSize(ctx, error);
  if (IsError(error)) {
    return;
  }

  ctx->parseBuffer[ctx->parseBufferUsage++] = part;
}

static Slice parseId(ParseContext *ctx, PL2ERR *error) {
  (void)error;
  char *start = curCharPos(ctx);
  while (isIdChar(curChar(ctx))) {
    nextChar(ctx);
  }
  char *end = curCharPos(ctx);
  return slice(start, end);
}

static Slice parseStr(ParseContext *ctx, PL2ERR *error) {
  assert(curChar(ctx) == '"' || curChar(ctx) == '\'');
  nextChar(ctx);

  char *start = curCharPos(ctx);
  while (curChar(ctx) != '"'
         && curChar(ctx) != '\''
         && !isLineEnd(curChar(ctx))) {
    if (curChar(ctx) == '\\') {
      nextChar(ctx);
      nextChar(ctx);
    } else {
      nextChar(ctx);
    }
  }
  char *end = curCharPos(ctx);
  end = shrinkConv(start, end);

  if (curChar(ctx) == '"' || curChar(ctx) == '\'') {
    nextChar(ctx);
  } else {
    ErrPrintf(error, PL2ERR_UNCLOSED_BEGIN, ctx->sourceInfo,
                   NULL, "unclosed string literal");
    return nullSlice();
  }
  return slice(start, end);
}

static void checkBufferSize(ParseContext *ctx, PL2ERR *error) {
  if (ctx->parseBufferSize <= ctx->parseBufferUsage + 1) {
    ErrPrintf(error, PL2ERR_UNCLOSED_BEGIN, ctx->sourceInfo,
                   NULL, "command parts exceed internal parsing buffer");
  }
}

static void finishLine(ParseContext *ctx, PL2ERR *error) {
  (void)error;

  SRCINFO sourceInfo = ctx->sourceInfo;
  nextChar(ctx);
  if (ctx->parseBufferUsage == 0) {
    return;
  }
  if (ctx->listTail == NULL) {
    assert(ctx->program.commands == NULL);
    ctx->program.commands =
      ctx->listTail = cmdFromSlices2(ctx->sourceInfo, ctx->parseBuffer);
  } else {
    ctx->listTail = cmdFromSlices5(ctx->listTail, NULL, NULL,
                                   ctx->sourceInfo, ctx->parseBuffer);
  }
  if (ctx->listTail == NULL) {
    ErrPrintf(error, PL2ERR_MALLOC, sourceInfo, 0,
                   "failed allocating COMMAND");
  }
  memset(ctx->parseBuffer, 0, sizeof(Slice) * ctx->parseBufferSize);
  ctx->parseBufferUsage = 0;
}

static COMMAND *cmdFromSlices2(SRCINFO sourceInfo,
                                Slice *parts) {
  return cmdFromSlices5(NULL, NULL, NULL, sourceInfo, parts);
}

static COMMAND *cmdFromSlices5(COMMAND *prev,
                                COMMAND *next,
                                void *extraData,
                                SRCINFO sourceInfo,
                                Slice *parts) {
  uint16_t partCount = 0;
  for (; !isNullSlice(parts[partCount]); ++partCount);

  COMMAND *ret = (COMMAND*)malloc(sizeof(COMMAND) +
                                    partCount * sizeof(char*));
  if (ret == NULL) {
    return NULL;
  }

  ret->prev = prev;
  if (prev != NULL) {
    prev->next = ret;
  }
  ret->next = next;
  if (next != NULL) {
    next->prev = ret;
  }
  ret->extraData = extraData;
  ret->sourceInfo = sourceInfo;
  ret->cmd = sliceIntoCStr(parts[0]);
  for (uint16_t i = 1; i < partCount; i++) {
    ret->args[i - 1] = sliceIntoCStr(parts[i]);
  }
  ret->args[partCount - 1] = NULL;
  return ret;
}

static void skipWhitespace(ParseContext *ctx) {
  while (1) {
    switch (curChar(ctx)) {
    case ' ': case '\t': case '\f': case '\v': case '\r':
      nextChar(ctx);
      break;
    default:
      return;
    }
  }
}

static void skipComment(ParseContext *ctx) {
  assert(curChar(ctx) == '#');
  nextChar(ctx);

  while (!isLineEnd(curChar(ctx))) {
    nextChar(ctx);
  }

  if (curChar(ctx) == '\n') {
    nextChar(ctx);
  }
}

static char curChar(ParseContext *ctx) {
  return ctx->src[ctx->srcIdx];
}

static char *curCharPos(ParseContext *ctx) {
  return ctx->src + ctx->srcIdx;
}

static void nextChar(ParseContext *ctx) {
  if (ctx->src[ctx->srcIdx] == '\0') {
    return;
  } else {
    if (ctx->src[ctx->srcIdx] == '\n') {
      ctx->sourceInfo.line += 1;
    }
    ctx->srcIdx += 1;
  }
}

static _Bool isIdChar(char ch) {
  unsigned char uch = transmuteU8(ch);
  if (uch >= 128) {
    return 1;
  } else if (isalnum(ch)) {
    return 1;
  } else {
    switch (ch) {
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

static _Bool isLineEnd(char ch) {
  return ch == '\0' || ch == '\n';
}

static char *shrinkConv(char *start, char *end) {
  char *iter1 = start, *iter2 = start;
  while (iter1 != end) {
    if (iter1[0] == '\\') {
      switch (iter1[1]) {
      case 'n': *iter2++ = '\n'; iter1 += 2; break;
      case 'r': *iter2++ = '\r'; iter1 += 2; break;
      case 'f': *iter2++ = '\f'; iter1 += 2; break;
      case 'v': *iter2++ = '\v'; iter1 += 2; break;
      case 't': *iter2++ = '\t'; iter1 += 2; break;
      case 'a': *iter2++ = '\a'; iter1 += 2; break;
      case '"': *iter2++ = '\"'; iter1 += 2; break;
      case '0': *iter2++ = '\0'; iter1 += 2; break;
      default:
        *iter2++ = *iter1++;
      }
    } else {
      *iter2++ = *iter1++;
    }
  }
  return iter2;
}

/*** -------------------- Semantic-ver parsing  -------------------- ***/

static const char *parseUint16(const char *src,
                               uint16_t *output,
                               PL2ERR *error);

static void parseSemVerPostfix(const char *src,
                               char *output,
                               PL2ERR *error);

pl2w_SemVer pl2w_zeroVersion(void) {
  pl2w_SemVer ret;
  ret.major = 0;
  ret.minor = 0;
  ret.patch = 0;
  memset(ret.postfix, 0, SEMVER_POSTFIX_LEN);
  ret.exact = 0;
  return ret;
}

_Bool pl2w_isZeroVersion(pl2w_SemVer ver) {
  return ver.major == 0
         && ver.minor == 0
         && ver.patch == 0
         && ver.postfix[0] == 0
         && ver.exact == 0;
}

_Bool pl2w_isAlpha(pl2w_SemVer ver) {
  return ver.postfix[0] != '\0';
}

_Bool pl2w_isStable(pl2w_SemVer ver) {
  return !pl2w_isAlpha(ver) && ver.major != 0;
}

pl2w_SemVer pl2w_parseSemVer(const char *src, PL2ERR *error) {
  pl2w_SemVer ret = pl2w_zeroVersion();
  if (src[0] == '^') {
    ret.exact = 1;
    src++;
  }

  src = parseUint16(src, &ret.major, error);
  if (IsError(error)) {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0), NULL,
                   "missing major version");
    goto done;
  } else if (src[0] == '\0') {
    goto done;
  } else if (src[0] == '-') {
    goto parse_postfix;
  } else if (src[0] != '.') {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0), NULL,
                   "expected `.`, got `%c`", src[0]);
    goto done;
  }

  src++;
  src = parseUint16(src, &ret.minor, error);
  if (IsError(error)) {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0),
                   NULL, "missing minor version");
    goto done;
  } else if (src[0] == '\0') {
    goto done;
  } else if (src[0] == '-') {
    goto parse_postfix;
  } else if (src[0] != '.') {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0), NULL,
                   "expected `.`, got `%c`", src[0]);
    goto done;
  }

  src++;
  src = parseUint16(src, &ret.patch, error);
  if (IsError(error)) {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0), NULL,
                   "missing patch version");
    goto done;
  } else if (src[0] == '\0') {
    goto done;
  } else if (src[0] != '-') {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0), NULL,
                   "unterminated semver, "
                   "expected `-` or `\\0`, got `%c`",
                   src[0]);
    goto done;
  }

parse_postfix:
  parseSemVerPostfix(src, ret.postfix, error);

done:
  return ret;
}

_Bool pl2w_isCompatible(pl2w_SemVer expected, pl2w_SemVer actual) {
  if (strncmp(expected.postfix,
              actual.postfix,
              SEMVER_POSTFIX_LEN) != 0) {
    return 0;
  }
  if (expected.exact) {
    return expected.major == actual.major
           && expected.minor == actual.minor
           && expected.patch == actual.patch;
  } else if (expected.major == actual.major) {
    return (expected.minor == actual.minor && expected.patch < actual.patch)
            || (expected.minor < actual.minor);
  } else {
    return 0;
  }
}

CMPRESULT pl2w_semverCmp(pl2w_SemVer ver1, pl2w_SemVer ver2) {
  if (!strncmp(ver1.postfix, ver2.postfix, SEMVER_POSTFIX_LEN)) {
    return CMP_NONE;
  }

  if (ver1.major < ver2.major) {
    return CMP_LESS;
  } else if (ver1.major > ver2.major) {
    return CMP_GREATER;
  } else if (ver1.minor < ver2.minor) {
    return CMP_LESS;
  } else if (ver1.minor > ver2.minor) {
    return CMP_GREATER;
  } else if (ver1.patch < ver2.patch) {
    return CMP_LESS;
  } else if (ver1.patch > ver2.patch) {
    return CMP_GREATER;
  } else {
    return CMP_EQ;
  }
}

void pl2w_semverToString(pl2w_SemVer ver, char *buffer) {
  if (ver.postfix[0]) {
    sprintf(buffer, "%s%u.%u.%u-%s",
            ver.exact ? "^" : "",
            ver.major,
            ver.minor,
            ver.patch,
            ver.postfix);
  } else {
    sprintf(buffer, "%s%u.%u.%u",
            ver.exact ? "^" : "",
            ver.major,
            ver.minor,
            ver.patch);
  }
}

static const char *parseUint16(const char *src,
                               uint16_t *output,
                               PL2ERR *error) {
  if (!isdigit((int)src[0])) {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0),
                   NULL, "expected numeric version");
    return NULL;
  }
  *output = 0;
  while (isdigit((int)src[0])) {
    *output *= 10;
    *output += src[0] - '0';
    ++src;
  }
  return src;
}

static void parseSemVerPostfix(const char *src,
                               char *output,
                               PL2ERR *error) {
  assert(src[0] == '-');
  ++src;
  if (src[0] == '\0') {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0), NULL,
                   "empty semver postfix");
    return;
  }
  for (size_t i = 0; i < SEMVER_POSTFIX_LEN - 1; i++) {
    if (!(*output++ = *src++)) {
      return;
    }
  }
  if (src[0] != '\0') {
    ErrPrintf(error, PL2ERR_SEMVER_PARSE,
                   SourceInfo(NULL, 0), NULL,
                   "semver postfix too long");
    return;
  }
}

/*** ----------------------------- Run ----------------------------- ***/

typedef struct st_run_context {
  pl2w_Program *program;
  COMMAND *curCmd;
  void *userContext;

  HMODULE hModule;
  pl2w_Language *language;
  _Bool ownLanguage;
} RunContext;

static RunContext *createRunContext(pl2w_Program *program);
static void destroyRunContext(RunContext *context);
static _Bool cmdHandler(RunContext *context,
                        COMMAND *cmd,
                        PL2ERR *error);
static _Bool loadLanguage(RunContext *context,
                          COMMAND *cmd,
                          PL2ERR *error);
static pl2w_Language *ezLoad(HMODULE hModule,
                            const char **cmdNames,
                            PL2ERR *error);

void pl2w_run(pl2w_Program *program, PL2ERR *error) {
  RunContext *context = createRunContext(program);
  if (context == NULL) {
    ErrPrintf(error, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                   NULL, "run: cannot allocate memory for run context");
    return;
  }

  while (cmdHandler(context, context->curCmd, error)) {
    if (IsError(error)) {
      break;
    }
  }

  destroyRunContext(context);
}

static RunContext *createRunContext(pl2w_Program *program) {
  RunContext *context = (RunContext*)malloc(sizeof(RunContext));
  if (context == NULL) {
    return NULL;
  }

  context->program = program;
  context->curCmd = program->commands;
  context->userContext = NULL;
  context->hModule = NULL;
  context->language = NULL;
  return context;
}

static void destroyRunContext(RunContext *context) {
  if (context->hModule != NULL) {
    if (context->language != NULL) {
      if (context->language->atExit != NULL) {
        context->language->atExit(context->userContext);
      }
      if (context->ownLanguage) {
        free(context->language->sinvokeCmds);
        free(context->language->pCallCmds);
        free(context->language);
      }
      context->language = NULL;
    }
    if (FreeLibrary(context->hModule) == 0) {
      fprintf(stderr, "[int/e] error invoking FreeLibrary: %ld\n",
              GetLastError());
    }
  }
  free(context);
}

static _Bool cmdHandler(RunContext *context,
                        COMMAND *cmd,
                        PL2ERR *error) {
  if (cmd == NULL) {
    return 0;
  }

  if (!strcmp(cmd->cmd, "language")) {
    return loadLanguage(context, cmd, error);
  } else if (!strcmp(cmd->cmd, "abort")) {
    return 0;
  }

  if (context->language == NULL) {
    ErrPrintf(error, PL2ERR_NO_LANG, cmd->sourceInfo, NULL,
                   "no language loaded to execute user command");
    return 1;
  }

  for (pl2w_SInvokeCmd *iter = context->language->sinvokeCmds;
       iter != NULL && !PL2W_EMPTY_SINVOKE_CMD(iter);
       ++iter) {
    if (!iter->removed && !strcmp(cmd->cmd, iter->cmdName)) {
      if (iter->deprecated) {
        fprintf(stderr, "[int/w] using deprecated command: %s\n",
                iter->cmdName);
      }
      if (iter->stub != NULL) {
        iter->stub((const char**)cmd->args);
      }
      context->curCmd = cmd->next;
      return 1;
    }
  }

  for (pl2w_PCallCmd *iter = context->language->pCallCmds;
       iter != NULL && !PL2W_EMPTY_CMD(iter);
       ++iter) {
    if (!iter->removed && !strcmp(cmd->cmd, iter->cmdName)) {
      if (iter->cmdName != NULL
          && strcmp(cmd->cmd, iter->cmdName) != 0) {
        // Do nothing if so
      } else if (iter->routerStub != NULL
                 && !iter->routerStub(cmd->cmd)) {
        // Do nothing if so
      } else {
        if (iter->deprecated) {
          fprintf(stderr, "[int/w] using deprecated command: %s\n",
                  iter->cmdName);
        }
        if (iter->stub == NULL) {
          context->curCmd = cmd->next;
          return 1;
        }

        COMMAND *nextCmd = iter->stub(context->program,
                                       context->userContext,
                                       cmd,
                                       error);
        if (IsError(error)) {
          return 0;
        }
        if (nextCmd == context->language->termCmd) {
          return 0;
        }
        context->curCmd = nextCmd ? nextCmd : cmd->next;
        return 1;
      }
    }
  }

  if (context->language->fallback == NULL) {
    ErrPrintf(error, PL2ERR_UNKNOWN_CMD, cmd->sourceInfo, NULL,
                   "`%s` is not recognized as an internal or external "
                   "command, operable program or batch file",
                   cmd->cmd);
    return 0;
  }

  COMMAND *nextCmd = context->language->fallback(
    context->program,
    context->userContext,
    cmd,
    error
  );

  if (nextCmd == context->language->termCmd) {
    ErrPrintf(error, PL2ERR_UNKNOWN_CMD, cmd->sourceInfo, NULL,
                   "`%s` is not recognized as an internal or external "
                   "command, operable program or batch file",
                   cmd->cmd);
    return 0;
  }

  if (IsError(error)) {
    return 0;
  }
  if (nextCmd == context->language->termCmd) {
    return 0;
  }

  context->curCmd = nextCmd ? nextCmd : cmd->next;
  return 1;
}

static _Bool loadLanguage(RunContext *context,
                          COMMAND *cmd,
                          PL2ERR *error) {
  if (context->language != NULL) {
    ErrPrintf(error, PL2ERR_LOAD_LANG, cmd->sourceInfo, NULL,
                   "language: another language already loaded");
    return 0;
  }

  uint16_t argsLen = pl2w_argsLen(cmd);
  if (argsLen != 2) {
    ErrPrintf(error, PL2ERR_LOAD_LANG, cmd->sourceInfo, NULL,
                   "language: expected 2 arguments, got %u",
                   argsLen - 1);
    return 0;
  }

  const char *langId = cmd->args[0];
  pl2w_SemVer langVer = pl2w_parseSemVer(cmd->args[1], error);
  if (IsError(error)) {
    return 0;
  }

  static char buffer[4096] = "./lib";
  strcat(buffer, langId);
  strcat(buffer, ".dll");
  context->hModule = LoadLibraryA(buffer);

  if (context->hModule == NULL) {
    ErrPrintf(error, PL2ERR_LOAD_LANG, cmd->sourceInfo, NULL,
                   "language: cannot load language library `%s`: %ld",
                   langId, GetLastError());
    return 0;
  }

  FARPROC loadPtr = GetProcAddress(
    context->hModule,
    "pl2ext_loadLanguage"
  );
  if (loadPtr == NULL) {
    FARPROC ezLoadPtr = GetProcAddress(
      context->hModule,
      "pl2ezload"
    );

    if (ezLoadPtr == NULL) {
      ErrPrintf(error, PL2ERR_LOAD_LANG, cmd->sourceInfo, NULL,
                     "language: cannot locate `%s` or `%s` "
                     "on library `%s`: %ld",
                     "pl2ext_loadLanguage", "pl2ezload", langId,
                     GetLastError());
      return 0;
    }

    pl2w_EasyLoadLanguage *load = (pl2w_EasyLoadLanguage*)ezLoadPtr;
    context->language = ezLoad(context->hModule, load(), error);
    if (IsError(error)) {
      error->sourceInfo = cmd->sourceInfo;
      return 0;
    }
    context->ownLanguage = 1;
  } else {
    pl2w_LoadLanguage *load = (pl2w_LoadLanguage*)loadPtr;
    context->language = load(langVer, error);
    if (IsError(error)) {
      error->sourceInfo = cmd->sourceInfo;
      return 0;
    }
    context->ownLanguage = 0;
  }

  if (context->language != NULL && context->language->init != NULL) {
    context->userContext = context->language->init(error);
    if (IsError(error)) {
      error->sourceInfo = cmd->sourceInfo;
      return 0;
    }
  }

  context->curCmd = cmd->next;
  return 1;
}

static pl2w_Language *ezLoad(HMODULE hModule,
                             const char **cmdNames,
                             PL2ERR *error) {
  if (cmdNames == NULL || cmdNames[0] == NULL) {
    return NULL;
  }
  uint16_t count = 0;
  for (const char **iter = cmdNames; *iter != NULL; iter++) {
    ++count;
  }

  pl2w_Language *ret = (pl2w_Language*)malloc(sizeof(pl2w_Language));
  if (ret == NULL) {
    ErrPrintf(error, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                   NULL,
                   "language: ezload: "
                   "cannot allocate memory for pl2w_Language");
    return NULL;
  }

  ret->langName = "unknown";
  ret->langInfo = "anonymous language loaded by ezload";
  ret->termCmd = NULL;
  ret->init = NULL;
  ret->atExit = NULL;
  ret->pCallCmds = NULL;
  ret->fallback = NULL;
  ret->sinvokeCmds = (pl2w_SInvokeCmd*)malloc(
    sizeof(pl2w_SInvokeCmd) * (count + 1)
  );
  if (ret->sinvokeCmds == NULL) {
    ErrPrintf(error, PL2ERR_MALLOC, SourceInfo(NULL, 0),
                   NULL,
                   "langauge: ezload: "
                   "cannot allocate memory for "
                   "pl2w_Language->sinvokeCmds");
    free(ret);
    return NULL;
  }

  memset(ret->sinvokeCmds, 0, (count + 1) * sizeof(pl2w_SInvokeCmd));
  static char nameBuffer[512];
  for (uint16_t i = 0; i < count; i++) {
    const char *cmdName = cmdNames[i];
    if (strlen(cmdName) > 504) {
      ErrPrintf(error, PL2ERR_LOAD_LANG, SourceInfo(NULL, 0),
                     NULL,
                     "language: ezload: "
                     "name over 500 chars not supported");
      free(ret->sinvokeCmds);
      free(ret);
      return NULL;
    }
    strcpy(nameBuffer, "pl2ez_");
    strncat(nameBuffer, cmdName, 504);
    void *ptr = GetProcAddress(hModule, nameBuffer);
    if (ptr == NULL) {
      ErrPrintf(error, PL2ERR_LOAD_LANG, SourceInfo(NULL, 0),
                     NULL,
                     "language: ezload: cannot load function `%s`: %ld",
                     nameBuffer, GetLastError());
      free(ret->sinvokeCmds);
      free(ret);
      return NULL;
    }

    ret->sinvokeCmds[i].cmdName = cmdName;
    ret->sinvokeCmds[i].stub = (pl2w_SInvokeCmdStub*)ptr;
    ret->sinvokeCmds[i].deprecated = 0;
    ret->sinvokeCmds[i].removed = 0;
  }

  return ret;
}

