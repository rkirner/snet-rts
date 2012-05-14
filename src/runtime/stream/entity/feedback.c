/**
 * Feedback Combinator
 *
 * The feedback combinator implements a real feedback loop.
 * Special care must be taken to avoid the situation of
 * artificial deadlock.
 *
 * Author: Daniel Prokesch <daniel.prokesch@gmail.com>
 * Date:   2011-04-11
 */
#include <assert.h>

#include "snetentities.h"

#include "expression.h"
#include "memfun.h"
#include "locvec.h"
#include "queue.h"
#include "entities.h"

#include "threading.h"
#include "distribution.h"

/**
 * If following is defined, no buffer entity in the back-channel
 * is created. This can lead to artificial deadlock if there is not enough
 * capacity in streams of the feedback network, for records produced in the
 * feedback loop.
 */
//#define FEEDBACK_OMIT_BUFFER

/**
 * Use the stream-emitter implementation of the feedback buffer
 */
//#define FEEDBACK_STREAM_EMITTER

#define FEEDBACK_BACKCHAN_CAPACITY 64

/* Helper function for the feedback-dispatcher
 * - copied from star.c
 */
static bool MatchesBackPattern( snet_record_t *rec,
    snet_variant_list_t *back_patterns, snet_expr_list_t *guards)
{
  snet_expr_t *expr;
  snet_variant_t *pattern;

  LIST_ZIP_EACH(guards, back_patterns, expr, pattern) {
    if (SNetEevaluateBool( expr, rec) &&
        SNetRecPatternMatches(pattern, rec)) {
      return true;
    }
  }

  return false;
}



/******************************************************************************
 * Feedback collector
 *****************************************************************************/

enum fbcoll_mode {
  FBCOLL_IN,
  FBCOLL_FB1,
  FBCOLL_FB0
};

typedef struct {
  snet_stream_t *in, *fbi, *out;
  snet_stream_desc_t *instream, *outstream, *backstream;
  enum fbcoll_mode mode;
} fbcoll_arg_t;

struct fbcoll_state {
    snet_stream_desc_t *instream;
    snet_stream_desc_t *outstream;
    snet_stream_desc_t *backstream;
    bool terminate;
    enum fbcoll_mode mode;
};

/* helper functions to handle mode the feedback collector is in */
static void FbCollReadIn(struct fbcoll_state *state)
{
  snet_record_t *rec;

  assert( false == state->terminate );

  /* read from input stream */
  rec = SNetStreamRead( state->instream);

  switch( SNetRecGetDescriptor( rec)) {

    case REC_data:
      /* relay data record */
      SNetStreamWrite( state->outstream, rec);
      /* append a sort record */
      SNetStreamWrite(
          state->outstream,
          SNetRecCreate( REC_sort_end, 0, 1 ) /*type, lvl, num*/
          );
      /* mode switch to FB1 */
      state->mode = FBCOLL_FB1;
      break;

    case REC_sort_end:
      /* increase the level and forward */
      SNetRecSetLevel( rec, SNetRecGetLevel(rec)+1);
      SNetStreamWrite( state->outstream, rec);
      break;

    case REC_terminate:
      state->terminate = true;
      SNetStreamWrite( state->outstream, rec);
      /* note that no sort record has to be appended */
      break;

    case REC_sync:
      SNetStreamReplace( state->instream, SNetRecGetStream( rec));
      //fbcarg->in = SNetRecGetStream( rec);
      SNetRecDestroy( rec);
      break;

    case REC_collect:
    default:
      assert(0);
      /* if ignoring, at least destroy ... */
      SNetRecDestroy( rec);
      break;
  }
}


static void FbCollReadFbi(struct fbcoll_state *state)
{
  snet_record_t *rec;

  assert( false == state->terminate );

  /* read from feedback stream */
  rec = SNetStreamRead( state->backstream);

  switch( SNetRecGetDescriptor( rec)) {

    case REC_data:
      /* relay data record */
      SNetStreamWrite( state->outstream, rec);
      /* mode switch to FB0 (there is a next iteration) */
      state->mode = FBCOLL_FB0;
      break;

    case REC_sort_end:
      assert( 0 == SNetRecGetLevel(rec) );
      switch(state->mode) {
        case FBCOLL_FB0:
          state->mode = FBCOLL_FB1;
          /* increase counter (non-functional) */
          SNetRecSetNum( rec, SNetRecGetNum(rec)+1);
          SNetStreamWrite( state->outstream, rec);
          break;
        case FBCOLL_FB1:
          state->mode = FBCOLL_IN;
          /* kill the sort record */
          SNetRecDestroy( rec);
          break;
        default: assert(0);
      }
      break;

    case REC_sync:
      SNetStreamReplace( state->backstream, SNetRecGetStream( rec));
      //fbcarg->fbi = SNetRecGetStream( rec);
      SNetRecDestroy( rec);
      break;

    case REC_terminate:
    case REC_collect:
    default:
      assert(0);
      /* if ignoring, at least destroy ... */
      SNetRecDestroy( rec);
      break;
  }
}

/**
 * The feedback collector, the entry point of the
 * feedback combinator
 */
static void FeedbackCollTask(snet_entity_t *ent, void *arg)
{
  fbcoll_arg_t *fbcarg = (fbcoll_arg_t *)arg;

  struct fbcoll_state state;

  /* initialise state */
  state.terminate = false;
  state.mode = fbcarg->mode;

  state.instream   = fbcarg->instream;
  state.backstream = fbcarg->backstream;
  state.outstream  = fbcarg->outstream;

  /* which stream to read from is mode dependent */
  switch(state.mode) {
    case FBCOLL_IN:
      FbCollReadIn(&state);
      break;
    case FBCOLL_FB1:
    case FBCOLL_FB0:
      FbCollReadFbi(&state);
      break;
    default: assert(0); /* should not be reached */
  }

  if(state.terminate){
    SNetStreamClose(fbcarg->instream,   true);
    SNetStreamClose(fbcarg->backstream, true);
    SNetStreamClose(fbcarg->outstream,  false);
    SNetMemFree(fbcarg);
    return;
  }

  fbcarg->mode = state.mode;

  SNetThreadingReSpawn(ent);
}


/**
 * Initialization of the feedback collector
 */
static void InitFeedbackCollTask(snet_entity_t *ent, void *arg)
{
  fbcoll_arg_t *fbcarg = (fbcoll_arg_t *)arg;
  fbcarg->instream   = SNetStreamOpen(fbcarg->in,  'r');
  fbcarg->backstream = SNetStreamOpen(fbcarg->fbi, 'r');
  fbcarg->outstream  = SNetStreamOpen(fbcarg->out, 'w');
  SNetEntitySetFunction(ent, &FeedbackCollTask);
  FeedbackCollTask(ent, arg);
}

/******************************************************************************
 * Feedback dispatcher
 *****************************************************************************/

typedef struct {
  snet_stream_t *in, *out, *fbo;
  snet_stream_desc_t *instream, *outstream, *backstream;
  snet_variant_list_t *back_patterns;
  snet_expr_list_t *guards;
} fbdisp_arg_t;

/**
 * The feedback dispatcher, at the end of the
 * feedback combinator
 */
static void FeedbackDispTask(snet_entity_t *ent, void *arg)
{
  fbdisp_arg_t *fbdarg = (fbdisp_arg_t *)arg;

  snet_record_t *rec;

  /* read from input stream */
  rec = SNetStreamRead( fbdarg->instream);

  switch( SNetRecGetDescriptor( rec)) {

    case REC_data:
      /* route data record */
      if( MatchesBackPattern( rec, fbdarg->back_patterns, fbdarg->guards)) {
        /* send rec back into the loop */
        SNetStreamWrite( fbdarg->backstream, rec);
      } else {
        /* send to output */
        SNetStreamWrite( fbdarg->outstream, rec);
      }
      break;

    case REC_sort_end:
      {
        int lvl = SNetRecGetLevel(rec);
        if ( 0 == lvl ) {
          SNetStreamWrite( fbdarg->backstream, rec);
        } else {
          assert( lvl > 0 );
          SNetRecSetLevel( rec, lvl-1);
          SNetStreamWrite( fbdarg->outstream, rec);
        }
      }
      break;

    case REC_terminate:
#ifndef FEEDBACK_OMIT_BUFFER
      /* a terminate record is sent in the backloop for the buffer */
      SNetStreamWrite( fbdarg->backstream, SNetRecCopy( rec));
#endif
      SNetStreamWrite( fbdarg->outstream, rec);
      SNetStreamClose(fbdarg->instream,   true);
      SNetStreamClose(fbdarg->outstream,  false);
      SNetStreamClose(fbdarg->backstream, false);

      SNetVariantListDestroy( fbdarg->back_patterns);
      SNetExprListDestroy( fbdarg->guards);
      SNetMemFree( fbdarg);
      return;

    case REC_sync:
      SNetStreamReplace( fbdarg->instream, SNetRecGetStream( rec));
      SNetRecDestroy( rec);
      break;

    case REC_collect:
    default:
      assert(0);
      /* if ignoring, at least destroy ... */
      SNetRecDestroy( rec);
      break;
  }

  SNetThreadingReSpawn(ent);
}

/**
 * Initialization of the feedback dispatcher
 */
static void InitFeedbackDispTask(snet_entity_t *ent, void *arg)
{
  fbdisp_arg_t *fbdarg = (fbdisp_arg_t *)arg;

  fbdarg->instream   = SNetStreamOpen(fbdarg->in,  'r');
  fbdarg->outstream  = SNetStreamOpen(fbdarg->out, 'w');
  fbdarg->backstream = SNetStreamOpen(fbdarg->fbo, 'w');

  SNetEntitySetFunction(ent, &FeedbackDispTask);
  FeedbackDispTask(ent, arg);
}

/******************************************************************************
 * Feedback buffer
 *****************************************************************************/

typedef struct{
  snet_stream_t *in, *out;
  snet_stream_desc_t *instream, *outstream;
  int out_capacity;
#ifdef FEEDBACK_STREAM_EMITTER
  int out_counter;
#else
  snet_queue_t *internal_buffer;
  int max_read;
#endif
} fbbuf_arg_t;

#ifdef FEEDBACK_STREAM_EMITTER
static void FeedbackBufTask(snet_entity_t *ent, void *arg)
{
  fbbuf_arg_t *fbbarg = (fbbuf_arg_t *)arg;

  snet_record_t *rec;
  int out_capacity;

  out_capacity =  fbbarg->out_capacity;

  rec = SNetStreamRead(fbbarg->instream);
  if( SNetRecGetDescriptor(rec) == REC_terminate ) {
    /* this means, the outstream does not exist anymore! */
    SNetRecDestroy(rec);
    SNetStreamClose(fbbarg->instream,  true);
    SNetStreamClose(fbbarg->outstream, false);
    SNetMemFree(fbbarg);
    return;
  }

  if (fbbarg->out_counter+1 >= out_capacity) {
    /* new stream */
    snet_stream_t *new_stream = SNetStreamCreate(out_capacity);
    SNetStreamWrite(fbbarg->outstream,
        SNetRecCreate(REC_sync, new_stream)
        );

    SNetStreamClose(fbbarg->outstream, false);
    fbbarg->outstream = SNetStreamOpen(new_stream, 'w');
    fbbarg->out = new_stream;
    fbbarg->out_counter = 0;
  }
  /* write the record to the stream */
  SNetStreamWrite(fbbarg->outstream, rec);
  fbbarg->out_counter++;

  SNetThreadingReSpawn(ent);
}

static void InitFeedbackBufTask(snet_entity_t *ent, void *arg)
{
  fbbuf_arg_t *fbbarg = (fbbuf_arg_t *)arg;

  fbbarg->out_counter = 0;
  fbbarg->instream   = SNetStreamOpen(fbbarg->in,  'r');
  fbbarg->outstream  = SNetStreamOpen(fbbarg->out, 'w');
  SNetEntitySetFunction(ent, &FeedbackBufTask);
  FeedbackBufTask(ent, arg);
}

#else /* FEEDBACK_STREAM_EMITTER */

static void TerminateFeedbackBufTask(fbbuf_arg_t *fbbarg)
{
  SNetQueueDestroy(fbbarg->internal_buffer);

  SNetStreamClose(fbbarg->instream,   true);
  SNetStreamClose(fbbarg->outstream,  false);
  SNetMemFree(fbbarg);
}

/**
 * The feedback buffer
 */
static void FeedbackBufTask(snet_entity_t *ent, void *arg)
{
  fbbuf_arg_t *fbbarg = (fbbuf_arg_t *)arg;

  snet_record_t *rec;
  int out_capacity;
  int n = 0;
  int max_read = fbbarg->max_read;

  out_capacity =  fbbarg->out_capacity;


  rec = NULL;

  /* STEP 1: read n=min(available,max_read) records from input stream */

  /* read first record of the actual dispatch */
  if (0 == SNetQueueSize(fbbarg->internal_buffer)) {
    rec = SNetStreamRead(fbbarg->instream);
    /* only in empty mode! */
    if( REC_terminate == SNetRecGetDescriptor( rec)) {
      /* this means, the outstream does not exist anymore! */
      SNetRecDestroy(rec);
      TerminateFeedbackBufTask(fbbarg);
      return;
    }
  } else {
    //printf("yield\n");
    //SNetThreadingYield();
    //if ( SNetStreamPeek(fbbarg->instream) != NULL ) {
      rec = SNetStreamRead(fbbarg->instream);
      assert( REC_terminate != SNetRecGetDescriptor( rec) );
    //}
  }

  if (rec != NULL) {
    n = 1;
    /* put record into internal buffer */
    (void) SNetQueuePut(fbbarg->internal_buffer, rec);
  }


  while ( n<=max_read && SNetStreamPeek(fbbarg->instream)!=NULL ) {
    rec = SNetStreamRead(fbbarg->instream);

    /* check if we will need a larger outstream, and if so,
     * create a larger stream
     */
    if (SNetQueueSize(fbbarg->internal_buffer)+1 >= out_capacity) {
      snet_stream_t *new_stream;
      out_capacity *= 2;

      new_stream = SNetStreamCreate(out_capacity);
      (void) SNetQueuePut(fbbarg->internal_buffer,
                          SNetRecCreate(REC_sync, new_stream));
    }

    /* put record into internal buffer */
    (void) SNetQueuePut(fbbarg->internal_buffer, rec);
    n++;
  }

  /* STEP 2: try to empty the internal buffer */
  rec = SNetQueuePeek(fbbarg->internal_buffer);

  while (rec != NULL) {
    snet_stream_t *new_stream = NULL;
    if( REC_sync == SNetRecGetDescriptor( rec)) {
      new_stream = SNetRecGetStream(rec);
    }
    if (0 == SNetStreamTryWrite(fbbarg->outstream, rec)) {
      snet_record_t *rem;
      /* success, also remove from queue */
      rem = SNetQueueGet(fbbarg->internal_buffer);
      assert( rem == rec );

      if (new_stream != NULL) {
        /* written sync record, now change stream */
        SNetStreamClose(fbbarg->outstream, false);
        fbbarg->outstream = SNetStreamOpen(new_stream, 'w');
        fbbarg->out = new_stream;
      }
    } else {
      /* there remain elements in the buffer */
      break;
    }
    /* for the next iteration */
    rec = SNetQueuePeek(fbbarg->internal_buffer);
  }
  fbbarg->out_capacity = out_capacity;

  SNetThreadingReSpawn(ent);
}

static void InitFeedbackBufTask(snet_entity_t *ent, void *arg)
{
  fbbuf_arg_t *fbbarg = (fbbuf_arg_t *)arg;

  fbbarg->instream   = SNetStreamOpen(fbbarg->in,  'r');
  fbbarg->outstream  = SNetStreamOpen(fbbarg->out, 'w');
  fbbarg->max_read = fbbarg->out_capacity; /* TODO better usual stream capacity */
  fbbarg->internal_buffer = SNetQueueCreate();
  SNetEntitySetFunction(ent, &FeedbackBufTask);
  FeedbackBufTask(ent, arg);
}
#endif /* FEEDBACK_STREAM_EMITTER */


/****************************************************************************/
/* CREATION FUNCTION                                                        */
/****************************************************************************/
snet_stream_t *SNetFeedback( snet_stream_t *input,
    snet_info_t *info,
    int location,
    snet_variant_list_t *back_patterns,
    snet_expr_list_t *guards,
    snet_startup_fun_t box_a
    )
{
  snet_stream_t *output;
  snet_locvec_t *locvec;

  locvec = SNetLocvecGet(info);
  SNetLocvecFeedbackEnter(locvec);

  input = SNetRouteUpdate(info, input, location);
  if(SNetDistribIsNodeLocation(location)) {
    snet_stream_t *into_op, *from_op;
    snet_stream_t *back_bufin, *back_bufout;
    fbbuf_arg_t *fbbarg;
    fbcoll_arg_t *fbcarg;
    fbdisp_arg_t *fbdarg;
    int worker;

    /* Get the worker on which the filter entity should be spawned */
    worker = SNetThreadingInitialWorker(info, 1);

    /* create streams */
    into_op = SNetStreamCreate(0);
    output  = SNetStreamCreate(0);
    back_bufout = SNetStreamCreate(FEEDBACK_BACKCHAN_CAPACITY);


#ifndef FEEDBACK_OMIT_BUFFER
    back_bufin  = SNetStreamCreate(0);

    /* create the feedback buffer */
    fbbarg = SNetMemAlloc( sizeof( fbbuf_arg_t));
    fbbarg->in  = back_bufin;
    fbbarg->out = back_bufout;
    fbbarg->out_capacity = FEEDBACK_BACKCHAN_CAPACITY;
    SNetThreadingInitSpawn(
        SNetEntityCreate( ENTITY_fbbuf, location, locvec,
          "<fbbuf>", &InitFeedbackBufTask, (void*)fbbarg),
        worker
        );
#else
    back_bufin = back_bufout;
#endif

    /* create the feedback collector */
    fbcarg = SNetMemAlloc( sizeof( fbcoll_arg_t));
    fbcarg->in = input;
    fbcarg->fbi = back_bufout;
    fbcarg->out = into_op;
    fbcarg->mode = FBCOLL_IN;
    SNetThreadingInitSpawn(
        SNetEntityCreate( ENTITY_fbcoll, location, locvec,
          "<fbcoll>", &InitFeedbackCollTask, (void*)fbcarg),
        worker
        );

    /* create the instance network */
    from_op = box_a(into_op, info, location);
    from_op = SNetRouteUpdate(info, from_op, location);

    /* create the feedback dispatcher */
    fbdarg = SNetMemAlloc( sizeof( fbdisp_arg_t));
    fbdarg->in = from_op;
    fbdarg->fbo = back_bufin;
    fbdarg->out = output;
    fbdarg->back_patterns = back_patterns;
    fbdarg->guards = guards;
    SNetThreadingInitSpawn(
      SNetEntityCreate( ENTITY_fbdisp, location, locvec,
          "<fbdisp>", &InitFeedbackDispTask, (void*)fbdarg),
      worker
      );

  } else {
    SNetVariantListDestroy(back_patterns);
    SNetExprListDestroy(guards);
    output = box_a(input, info, location);
    output = SNetRouteUpdate(info, output, location);
  }

  SNetLocvecFeedbackLeave(locvec);

  return( output);
}
