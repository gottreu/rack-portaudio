#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "portaudio.h"

typedef struct soundCopyingInfo{
  // this sound is assumed to be malloc'ed, and gets freed when finished.
  short *sound;
  unsigned long curSample;
  unsigned long numSamples;
} soundCopyingInfo;

typedef struct soundStreamInfo{
  unsigned int   bufferFrames;
  char *buffer;

  unsigned int lastFrameRead;
  unsigned int lastOffsetRead;

  // only mutated by Racket
  unsigned int lastFrameWritten;
  unsigned int lastOffsetWritten;

  int   faultCount;
  int   *all_done;
} soundStreamInfo;

#define CHANNELS 2
#define SAMPLEBYTES 2

#define MYMIN(a,b) ((a)<(b) ? (a) : (b))
#define MYMAX(a,b) ((a)>(b) ? (a) : (b))
#define FRAMES_TO_BYTES(a) ((a)*CHANNELS*SAMPLEBYTES)


void freeCopyingInfo(soundCopyingInfo *ri);
void freeStreamingInfo(soundStreamInfo *ssi);

// simplest possible feeder; copy bytes until you run out.
// assumes 16-bit ints, 2 channels.
// CALLS FREE ON THE SOUND AND THE RECORD WHEN FINISHED
int copyingCallback(
    const void *input, void *output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData ) {

  soundCopyingInfo *ri = (soundCopyingInfo *)userData;
  short *copyBegin = ri->sound + ri->curSample;
  unsigned long samplesToCopy = frameCount * CHANNELS;
  unsigned long nextCurSample = ri->curSample + samplesToCopy;
  // !@#$ windows makes me declare them at the top of the function:
  size_t bytesToCopy;
  char *zeroRegionBegin;
  size_t bytesToZero;

  if (ri->numSamples <= nextCurSample) {
    // this is the last chunk.
    bytesToCopy = SAMPLEBYTES * (ri->numSamples - ri->curSample);
    memcpy(output,(void *)copyBegin,bytesToCopy);
    // zero out the rest of the buffer:
    zeroRegionBegin = (char *)output + bytesToCopy;
    bytesToZero = FRAMES_TO_BYTES(frameCount) - bytesToCopy;
    memset(zeroRegionBegin,0,bytesToZero);
    ri->curSample = ri->numSamples;

    return(paComplete);

  } else {
    // this is not the last chunk. 
    bytesToCopy = SAMPLEBYTES * samplesToCopy;
    memcpy(output,(void *)copyBegin,bytesToCopy);
    ri->curSample = nextCurSample;
    return(paContinue);
  }
}

// simplest possible feeder; copy bytes until you run out.
// assumes 16-bit ints, 2 channels.
int copyingCallbackRec(
    const void *input, void *output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData ) {

  soundCopyingInfo *ri = (soundCopyingInfo *)userData;
  short *copyBegin = ri->sound + ri->curSample;
  unsigned long samplesToCopy = frameCount * CHANNELS;
  unsigned long nextCurSample = ri->curSample + samplesToCopy;
  // !@#$ windows makes me declare them at the top of the function:
  size_t bytesToCopy;
  char *zeroRegionBegin;
  size_t bytesToZero;

  if (ri->numSamples <= nextCurSample) {
    // this is the last chunk.
    bytesToCopy = SAMPLEBYTES * (ri->numSamples - ri->curSample);
    memcpy((void *)copyBegin,input,bytesToCopy);
    ri->curSample = ri->numSamples;

    return(paComplete);

  } else {
    // this is not the last chunk. 
    bytesToCopy = SAMPLEBYTES * samplesToCopy;
    memcpy((void *)copyBegin,output,bytesToCopy);
    ri->curSample = nextCurSample;
    return(paContinue);
  }
}


// copy buffers to the output buffer (if they're available)
int streamingCallback(
    const void *input, void *output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData ) {

  soundStreamInfo *ssi = (soundStreamInfo *)userData;

  unsigned int lastFrameRequested = ssi->lastFrameRead + frameCount;
  unsigned int lastFrameToCopy = MYMAX(ssi->lastFrameRead,MYMIN(lastFrameRequested,ssi->lastFrameWritten));
  unsigned int framesToCopy = lastFrameToCopy - ssi->lastFrameRead;
  unsigned int bytesToCopy = FRAMES_TO_BYTES(framesToCopy);
  unsigned int lastOffsetToCopy = ssi->lastOffsetRead + bytesToCopy;
  unsigned int bufferBytes = FRAMES_TO_BYTES(ssi->bufferFrames);
  // stupid windows. I bet there's some way to get around this restriction.
  unsigned int bytesInEnd;
  unsigned int bytesAtBeginning;
  
  if (lastOffsetToCopy > bufferBytes) {
    // break it into two pieces:
    bytesInEnd = bufferBytes - ssi->lastOffsetRead;
    memcpy(output,(void *)((ssi->buffer)+(ssi->lastOffsetRead)),bytesInEnd);
    bytesAtBeginning = bytesToCopy - bytesInEnd;
    memcpy((void *)((char *)output+bytesInEnd),(void *)ssi->buffer,bytesAtBeginning);
  } else {
    // otherwise just copy it all at once:
    memcpy(output,(void *)((ssi->buffer)+(ssi->lastOffsetRead)),bytesToCopy);
  }
  // fill the rest with zeros, if any:
  if (lastFrameToCopy < lastFrameRequested) {
    memset((void *)((char *)output+bytesToCopy),0,FRAMES_TO_BYTES(lastFrameRequested - lastFrameToCopy));
    ssi->faultCount += 1;
  }
  // update record. Advance to the desired point, even
  // if it wasn't available.
  ssi->lastFrameRead = lastFrameRequested;
  ssi->lastOffsetRead = (ssi->lastOffsetRead + FRAMES_TO_BYTES(frameCount)) % bufferBytes;
  
  return(paContinue);

}

// clean up when done:  free the sound data and the
// closure data
void freeCopyingInfo(soundCopyingInfo *ri){
  free(ri->sound);
  free(ri);
}

// clean up a streamingInfo record when done.
void freeStreamingInfo(soundStreamInfo *ssi){

  free(ssi->buffer);
  // when all_done is 1, this triggers self-destruct:
  *(ssi->all_done) = 1;
  free(ssi);
}

// this is just a stub to call malloc.
// it's necessary on windows, to ensure
// that the free & malloc used on the
// sound info blocks are associated
// with the same library.
void *dll_malloc(size_t bytes){
	return malloc(bytes);
}
