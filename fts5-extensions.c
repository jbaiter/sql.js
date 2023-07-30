/* Add your header comment here */
#include "sqlite3ext.h" /* Do not use <sqlite3.h>! */
SQLITE_EXTENSION_INIT1

#include <stddef.h>
#include <string.h>
/* =================================================================== */
/* =============== Copied from sqlite/fts5_aux.c ===================== */
/* =================================================================== */
#ifndef UNUSED_PARAM2
#define UNUSED_PARAM2(X, Y) (void)(X), (void)(Y)
#endif

/*
** Object used to iterate through all "coalesced phrase instances" in
** a single column of the current row. If the phrase instances in the
** column being considered do not overlap, this object simply iterates
** through them. Or, if they do overlap (share one or more tokens in
** common), each set of overlapping instances is treated as a single
** match. See documentation for the highlight() auxiliary function for
** details.
**
** Usage is:
**
**   for(rc = fts5ext_cinstiter_next(pApi, pFts, iCol, &iter);
**      (rc==SQLITE_OK && 0==fts5CInstIterEof(&iter);
**      rc = fts5ext_cinstiter_next(&iter)
**   ){
**     printf("instance starts at %d, ends at %d\n", iter.iStart, iter.iEnd);
**   }
**
*/
typedef struct CInstIter CInstIter;
struct CInstIter
{
  const Fts5ExtensionApi *pApi; /* API offered by current FTS version */
  Fts5Context *pFts;            /* First arg to pass to pApi functions */
  int iCol;                     /* Column to search */
  int iInst;                    /* Next phrase instance index */
  int nInst;                    /* Total number of phrase instances */

  /* Output variables */
  int iStart; /* First token in coalesced phrase instance */
  int iEnd;   /* Last token in coalesced phrase instance */
};

/*
** Advance the iterator to the next coalesced phrase instance. Return
** an SQLite error code if an error occurs, or SQLITE_OK otherwise.
*/
static int fts5ext_cinstiter_next(CInstIter *pIter)
{
  int rc = SQLITE_OK;
  pIter->iStart = -1;
  pIter->iEnd = -1;

  while (rc == SQLITE_OK && pIter->iInst < pIter->nInst)
  {
    int ip;
    int ic;
    int io;
    rc = pIter->pApi->xInst(pIter->pFts, pIter->iInst, &ip, &ic, &io);
    if (rc == SQLITE_OK)
    {
      if (ic == pIter->iCol)
      {
        int iEnd = io - 1 + pIter->pApi->xPhraseSize(pIter->pFts, ip);
        if (pIter->iStart < 0)
        {
          pIter->iStart = io;
          pIter->iEnd = iEnd;
        }
        else if (io <= pIter->iEnd)
        {
          if (iEnd > pIter->iEnd)
            pIter->iEnd = iEnd;
        }
        else
        {
          break;
        }
      }
      pIter->iInst++;
    }
  }

  return rc;
}

/*
** Initialize the iterator object indicated by the final parameter to
** iterate through coalesced phrase instances in column iCol.
*/
static int fts5ext_cinstiter_init(
    const Fts5ExtensionApi *pApi,
    Fts5Context *pFts,
    int iCol,
    CInstIter *pIter)
{
  int rc;

  memset(pIter, 0, sizeof(CInstIter));
  pIter->pApi = pApi;
  pIter->pFts = pFts;
  pIter->iCol = iCol;
  rc = pApi->xInstCount(pFts, &pIter->nInst);

  if (rc == SQLITE_OK)
  {
    rc = fts5ext_cinstiter_next(pIter);
  }

  return rc;
}
/* =================================================================== */
/* ==================== End of copied source code ==================== */
/* =================================================================== */

/** Context needed for determining offsets of phrase matches */
typedef struct OffsetContext OffsetContext;
struct OffsetContext
{
  CInstIter iter;      /* Coalesced Instance Iterator */
  int iCol;            /* Column number that contains document */
  const char *zIn;     /* Input document */
  int nIn;             /* Size of input document in bytes */
  int iPos;            /* Current token position in document */
  int iOff;            /* Current byte offset in document */
  int iPhraseStartOff; /* Byte offset of start of phrase  */
  char *zOut;          /* Output value */
};

/** Initialize offset context, including coalesced phrase instance
 *  iterator and document text. */
static int fts5ext_offset_ctx_init(const Fts5ExtensionApi *pApi, Fts5Context *pFts, OffsetContext *pCtx, int iCol)
{
  memset(pCtx, 0, sizeof(OffsetContext));
  pCtx->iCol = iCol;
  int rc = fts5ext_cinstiter_init(pApi, pFts, iCol, &pCtx->iter);
  if (rc != SQLITE_OK)
  {
    return rc;
  }
  return pApi->xColumnText(pFts, iCol, &pCtx->zIn, &pCtx->nIn);
}

/** Append phrase offset information to output. */
static void fts5ext_offset_append(
    int *pRc,
    OffsetContext *p,
    int iPhraseEndOffset)
{
  if (*pRc != SQLITE_OK)
  {
    return; // NOP
  }
  if (p->zOut == NULL) {
    // Don't add a space before the first phrase offset triple
    p->zOut = sqlite3_mprintf(
        "%z%d %d %d", p->zOut, p->iCol, p->iPhraseStartOff, iPhraseEndOffset);
  } else {
    p->zOut = sqlite3_mprintf(
        "%z %d %d %d", p->zOut, p->iCol, p->iPhraseStartOff, iPhraseEndOffset);
  }
  if (p->zOut == NULL)
  {
    *pRc = SQLITE_NOMEM;
  }
}

/** Tokenizer callback that outputs phrase offsets when encountering
 *  the last token of a phrase. */
static int fts5ext_offset_callback(
    void *pContext,     /* Pointer to HighlightContext object */
    int tflags,         /* Mask of FTS5_TOKEN_* flags */
    const char *pToken, /* Buffer containing token */
    int nToken,         /* Size of token in bytes */
    int iStartOff,      /* Start offset of token */
    int iEndOff         /* End offset of token */
)
{
  OffsetContext *ctx = (OffsetContext *)pContext;
  int rc = SQLITE_OK;
  int iPos;

  UNUSED_PARAM2(pToken, nToken);

  if (tflags & FTS5_TOKEN_COLOCATED)
  {
    return SQLITE_OK;
  }
  // Increment token position and current offset in document
  iPos = ctx->iPos++;
  ctx->iOff = iStartOff;

  // Tokenizer is at the start of the current phrase
  if (iPos == ctx->iter.iStart)
  {
    ctx->iPhraseStartOff = ctx->iOff;
    ctx->iOff = iStartOff;
  }

  // Tokenizer is at the end of the current phrase
  if (iPos == ctx->iter.iEnd)
  {
    fts5ext_offset_append(&rc, ctx, iEndOff);
    if (rc == SQLITE_OK)
    {
      rc = fts5ext_cinstiter_next(&ctx->iter);
    }
  }

  return rc;
}

/** FTS5 auxilliary function to return start and end byte offsets for matching
 *  query phrases. */
static void fts5ext_offsets(
    const Fts5ExtensionApi *pApi, /* IN: FTS5 api handle */
    Fts5Context *pFts,            /* IN: FTS5 context handle */
    sqlite3_context *pCtx,        /* IN: SQLite3 context handle */
    int nVal,                     /* IN: Number of values in apVal[] */
    sqlite3_value **apVal         /* IN: Arguments to function */
)
{
  OffsetContext ctx;
  int rc;
  int iCol;
  if (nVal != 1)
  {
    const char *zErr = "wrong number of arguments to function offsets()";
    sqlite3_result_error(pCtx, zErr, -1);
    return;
  }

  iCol = sqlite3_value_int(apVal[0]);
  rc = fts5ext_offset_ctx_init(pApi, pFts, &ctx, iCol);
  if (ctx.zIn)
  {
    if (rc == SQLITE_OK)
    {
      rc = pApi->xTokenize(pFts, ctx.zIn, ctx.nIn, &ctx, fts5ext_offset_callback);
    }
    if (rc == SQLITE_OK || rc == SQLITE_ABORT)
    {
      sqlite3_result_text(pCtx, ctx.zOut, -1, SQLITE_TRANSIENT);
    }
    sqlite3_free(ctx.zOut);
  }
  if (rc != SQLITE_OK)
  {
    sqlite3_result_error_code(pCtx, rc);
  }
}

/*
** Return a pointer to the fts5_api pointer for database connection db.
** If an error occurs, return NULL and leave an error in the database
** handle (accessible using sqlite3_errcode()/errmsg()).
*/
fts5_api *fts5_api_from_db(sqlite3 *db)
{
  fts5_api *pRet = 0;
  sqlite3_stmt *pStmt = 0;

  if (SQLITE_OK == sqlite3_prepare(db, "SELECT fts5(?1)", -1, &pStmt, 0))
  {
    sqlite3_bind_pointer(pStmt, 1, (void *)&pRet, "fts5_api_ptr", NULL);
    sqlite3_step(pStmt);
  }
  sqlite3_finalize(pStmt);
  return pRet;
}

int RegisterFTS5Extensions(sqlite3 *db)
{
  fts5_api *fts5 = fts5_api_from_db(db);
  return fts5->xCreateFunction(
      fts5,
      "offsets",
      NULL,
      &fts5ext_offsets,
      NULL);
}

#ifdef COMPILE_SQLITE_EXTENSIONS_AS_LOADABLE_MODULE
#ifdef _WIN32
__declspec(dllexport)
#endif
    int sqlite3_fts5_extensions_init(
        sqlite3 *db,
        char **pzErrMsg,
        const sqlite3_api_routines *pApi)
{
  int rc = SQLITE_OK;
  SQLITE_EXTENSION_INIT2(pApi);
  return RegisterFTS5Extensions(db);
}
#endif