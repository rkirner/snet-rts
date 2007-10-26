/**
 * @file sudokusolve.c
 *
 * Source code of compiled snet-file for runtime.
 *
 * THIS FILE HAS BEEN GENERATED.
 * DO NOT EDIT THIS FILE.
 * EDIT THE ORIGINAL SNET-SOURCE FILE sudokusolve.snet INSTEAD!
 *
 * ALL CHANGES MADE TO THIS FILE WILL BE OVERWRITTEN!
 *
*/

#include "sudokusolve.h"
#include "computeOpts.h"
#include "solveOneLevel.h"

static void SNet__sudokusolve__computeOpts(snet_handle_t *hnd) {
  snet_record_t *rec = NULL;
  void *field_board = NULL;

  rec = SNetHndGetRecord(hnd);

  field_board = SNetRecTakeField(rec, F__sudokusolve__board);

  computeOpts(hnd, field_board);
}

static void SNet__sudokusolve__solveOneLevel(snet_handle_t *hnd) {
  snet_record_t *rec = NULL;
  void *field_board = NULL;
  void *field_opts = NULL;

  rec = SNetHndGetRecord(hnd);

  field_board = SNetRecTakeField(rec, F__sudokusolve__board);

  field_opts = SNetRecTakeField(rec, F__sudokusolve__opts);

  solveOneLevel(hnd, field_board, field_opts);
}

static snet_buffer_t *SNet__sudokusolve___SL(snet_buffer_t *in_buf) {
  snet_buffer_t *out_buf = NULL;
  snet_typeencoding_t *out_type = 
    SNetTencTypeEncode( 1,
        SNetTencVariantEncode(
          SNetTencCreateVector( 2, F__sudokusolve__board, F__sudokusolve__opts),
          SNetTencCreateVector( 0),
          SNetTencCreateVector( 0)));

  out_buf = SNetBox(in_buf, 
              &SNet__sudokusolve__computeOpts, 
              out_type);

  return (out_buf);
}

static snet_buffer_t *SNet__sudokusolve___SR___ST(snet_buffer_t *in_buf) {
  snet_buffer_t *out_buf = NULL;
  snet_typeencoding_t *out_type = 
    SNetTencTypeEncode( 2,
        SNetTencVariantEncode(
          SNetTencCreateVector( 2, F__sudokusolve__board, F__sudokusolve__opts),
          SNetTencCreateVector( 0),
          SNetTencCreateVector( 0)),
        SNetTencVariantEncode(
          SNetTencCreateVector( 1, F__sudokusolve__board),
          SNetTencCreateVector( 1, T__sudokusolve__done),
          SNetTencCreateVector( 0)));

  out_buf = SNetBox(in_buf, 
              &SNet__sudokusolve__solveOneLevel, 
              out_type);

  return (out_buf);
}

static snet_buffer_t *SNet____STAR_INCARNATE_sudokusolve___SR(snet_buffer_t *in_buf) {
  snet_buffer_t *out_buf = NULL;

  out_buf = SNetStarIncarnate(in_buf, 
              SNetTencTypeEncode(1, 
                SNetTencVariantEncode(
                  SNetTencCreateVector(0), 
                  SNetTencCreateVector(1, T__sudokusolve__done), 
                  SNetTencCreateVector(0))), 
              NULL, 
              &SNet__sudokusolve___SR___ST, 
              &SNet____STAR_INCARNATE_sudokusolve___SR);

  return (out_buf);
}

static snet_buffer_t *SNet__sudokusolve___SR(snet_buffer_t *in_buf) {
  snet_buffer_t *out_buf = NULL;

  out_buf = SNetStar(in_buf, 
              SNetTencTypeEncode(1, 
                SNetTencVariantEncode(
                  SNetTencCreateVector(0), 
                  SNetTencCreateVector(1, T__sudokusolve__done), 
                  SNetTencCreateVector(0))), 
              NULL, 
              &SNet__sudokusolve___SR___ST, 
              &SNet____STAR_INCARNATE_sudokusolve___SR);

  return (out_buf);
}

snet_buffer_t *SNet__sudokusolve___sudokusolve(snet_buffer_t *in_buf) {
  snet_buffer_t *out_buf = NULL;

  out_buf = SNetSerial(in_buf, 
              &SNet__sudokusolve___SL, 
              &SNet__sudokusolve___SR);

  return (out_buf);
}

