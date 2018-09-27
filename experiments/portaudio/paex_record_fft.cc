/*
 * $Id$
 *
 * This program uses the PortAudio Portable Audio Library.
 * For more information see: http://www.portaudio.com
 * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

#include <stdio.h>
#include <stdlib.h>
#include "portaudio.h"
#include <fftw3.h>
#include <iostream>
#include <algorithm>
#include <cmath>
#include "../fftw_test/gnuplot.h"

#include <vector>
#include <iterator>


#define REAL 0
#define IMAG 1
/* #define SAMPLE_RATE  (17932) // Test failure to open with this value. */
#define SAMPLE_RATE  (44100)
#define FRAMES_PER_BUFFER (1024)
#define MAX_BPM    120
#define N_SAMPLES  (SAMPLE_RATE / (MAX_BPM / 60 * 2))
#define N_BINS     (N_SAMPLES/2)-1
#define NUM_SECONDS     (1)
#define NUM_CHANNELS    (1)
#define NUM_SAMPLES    (NUM_SECONDS * SAMPLE_RATE * NUM_CHANNELS)
#define NUM_BINS     (NUM_SAMPLES/2)-1
/* #define DITHER_FLAG     (paDitherOff) */
#define DITHER_FLAG     (0)

#define WRITE_TO_FILE   (0)
//#define N_SAMPLES  (SAMPLE_RATE / (FRAMES_PER_BUFFER / 60 * 2))

/* Select sample format. */
#if 0
#define PA_SAMPLE_TYPE  paFloat32
typedef float SAMPLE;
#define SAMPLE_SILENCE  (0.0f)
#define PRINTF_S_FORMAT "%.8f"
#elif 1
#define PA_SAMPLE_TYPE  paInt16
typedef short SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#elif 0
#define PA_SAMPLE_TYPE  paInt8
typedef char SAMPLE;
#define SAMPLE_SILENCE  (0)
#define PRINTF_S_FORMAT "%d"
#else
#define PA_SAMPLE_TYPE  paUInt8
typedef unsigned char SAMPLE;
#define SAMPLE_SILENCE  (128)
#define PRINTF_S_FORMAT "%d"
#endif

typedef struct
{
    int          frameIndex;  /* Index into sample array. */
    int          maxFrameIndex;
    double      *recordedSamples;
    double      *fftwOutput;
}
paTestData;

fftw_plan plan;

/* This routine will be called by the PortAudio engine when audio is needed.
** It may be called at interrupt level on some machines so don't do anything
** that could mess up the system like calling malloc() or free().
*/
static int recordCallback( const void *inputBuffer, void *outputBuffer,
                           unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo* timeInfo,
                           PaStreamCallbackFlags statusFlags,
                           void *userData )
{
    paTestData *data = (paTestData*)userData;
    const SAMPLE *rptr = (const SAMPLE*)inputBuffer;
    double *wptr = &data->recordedSamples[data->frameIndex * NUM_CHANNELS];
    long framesToCalc;
    long i;
    int finished;
    unsigned long framesLeft = data->maxFrameIndex - data->frameIndex;

    (void) outputBuffer; /* Prevent unused variable warnings. */
    (void) timeInfo;
    (void) statusFlags;
    (void) userData;

    if( framesLeft < framesPerBuffer )
    {
        framesToCalc = framesLeft;
        finished = paComplete;
    }
    else
    {
        framesToCalc = framesPerBuffer;
        finished = paContinue;
    }

    if( inputBuffer == NULL )
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = SAMPLE_SILENCE;  /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = SAMPLE_SILENCE;  /* right */
        }
    }
    else
    {
        for( i=0; i<framesToCalc; i++ )
        {
            *wptr++ = *rptr++;  /* left */
            if( NUM_CHANNELS == 2 ) *wptr++ = *rptr++;  /* right */
        }
    }
    data->frameIndex += framesToCalc;

    return finished;
}

/*******************************************************************/
int main(void);
int main(void)
{
    PaStreamParameters  inputParameters,
                        outputParameters;
    PaStream*           stream;
    PaError             err = paNoError;
    paTestData          data;
    int                 i;
    int                 totalFrames;
    int                 numSamples;
    int                 numBytes;
    SAMPLE              max, val;
    double              average;
    bool                running;
    Gnuplot             gp_db_spectrum;

    printf("patest_record.c\n"); fflush(stdout);

    std::cout << std::endl;
    std::cout << "SAMPLE_RATE: "       << SAMPLE_RATE       << std::endl;
    std::cout << "FRAMES_PER_BUFFER: " << FRAMES_PER_BUFFER << std::endl;
    std::cout << "MAX_BPM: "           << MAX_BPM           << std::endl;
    std::cout << "N_SAMPLES: "         << N_SAMPLES         << std::endl;
    std::cout << "N_BINS: "            << N_BINS            << std::endl;
    std::cout << "NUM_SECONDS: "       << NUM_SECONDS       << std::endl;
    std::cout << "NUM_CHANNELS: "      << NUM_CHANNELS      << std::endl;
    std::cout << "NUM_SAMPLES: "       << NUM_SAMPLES       << std::endl;
    std::cout << "NUM_BINS: "          << NUM_BINS          << std::endl;

    data.maxFrameIndex = totalFrames = NUM_SECONDS * SAMPLE_RATE; /* Record for a few seconds. */
    data.frameIndex = 0;
    numSamples = totalFrames * NUM_CHANNELS;
    numBytes = numSamples * sizeof(double);

    std::cout << "data.maxFrameIndex: " << data.maxFrameIndex << std::endl;
    std::cout << "data.frameIndex: "    << data.frameIndex    << std::endl;
    std::cout << "numSamples: "         << numSamples         << std::endl;
    std::cout << "numBytes: "           << numBytes           << std::endl;
    std::cout << std::endl;

    data.recordedSamples = (double *) malloc( numBytes ); /* From now on, recordedSamples is initialised. */
    data.fftwOutput = (double *) malloc( numBytes );
    if( data.recordedSamples == NULL )
    {
        printf("Could not allocate record array.\n");
        goto done;
    }
    for( i=0; i<numSamples; i++ ) data.recordedSamples[i] = 0;

    err = Pa_Initialize();
    if( err != paNoError ) goto done;

    /*fftw_plan */plan = fftw_plan_r2r_1d(NUM_SAMPLES,
                                      data.recordedSamples, data.fftwOutput,
                                      FFTW_R2HC,
                                      // FFTW_FORWARD,
                                      FFTW_ESTIMATE);

    inputParameters.device = Pa_GetDefaultInputDevice(); /* default input device */
    if (inputParameters.device == paNoDevice) {
        fprintf(stderr,"Error: No default input device.\n");
        goto done;
    }
    inputParameters.channelCount = 1;                    /* mono input */
    inputParameters.sampleFormat = PA_SAMPLE_TYPE;
    inputParameters.suggestedLatency = Pa_GetDeviceInfo( inputParameters.device )->defaultLowInputLatency;
    inputParameters.hostApiSpecificStreamInfo = NULL;

    /* Record some audio. -------------------------------------------- */
    err = Pa_OpenStream(
              &stream,
              &inputParameters,
              NULL,                  /* &outputParameters, */
              SAMPLE_RATE,
              FRAMES_PER_BUFFER,
              paClipOff,      /* we won't output out of range samples so don't bother clipping them */
              recordCallback,
              &data );
    if( err != paNoError ) goto done;

    err = Pa_StartStream( stream );
    if( err != paNoError ) goto done;
    printf("\n=== Now recording!! Please whistle into the microphone. ===\n"); fflush(stdout);

    while( ( err = Pa_IsStreamActive( stream ) ) == 1 )
    {
      data.frameIndex = 0;
        // Pa_Sleep(1000);
        fftw_execute(plan);

    // Post-process frequency spectrum: transform to decibel
        std::vector<double> db_spectrum(NUM_BINS);
        for(int i = 0; i < NUM_BINS; i++){
           db_spectrum[i] = (20 *
                              log10(sqrt(  data.fftwOutput[i] * data.fftwOutput[i]
                              /*+ data.fftwOutput[i] * data.fftwOutput[i]*/))
                            ) / SAMPLE_RATE;
        }
        std::vector<double>::iterator max = std::max_element(db_spectrum.begin(),
                                                             db_spectrum.end());
        std::cout << "Detected frequency: "
                  << std::distance(db_spectrum.begin(), max)
                  << " Hz at velocity " << *max * 127 << std::endl;
        printf("index = %d\n", data.frameIndex ); fflush(stdout);
    }

    if( err < 0 ) goto done;

    err = Pa_CloseStream( stream );
    if( err != paNoError ) goto done;

    /* Measure maximum peak amplitude. */
    max = 0;
    average = 0.0;
    for( i=0; i<numSamples; i++ )
    {
        val = data.recordedSamples[i];
        if( val < 0 ) val = -val; /* ABS */
        if( val > max )
        {
            max = val;
        }
        average += val;
    }

    average = average / (double)numSamples;

    printf("sample max amplitude = "PRINTF_S_FORMAT"\n", max );
    printf("sample average = %lf\n", average );

    /* Write recorded data to a file. */
#if WRITE_TO_FILE
    {
        FILE  *fid;
        fid = fopen("recorded.raw", "wb");
        if( fid == NULL )
        {
            printf("Could not open file.");
        }
        else
        {
            fwrite( data.recordedSamples, NUM_CHANNELS * sizeof(SAMPLE), totalFrames, fid );
            fclose( fid );
            printf("Wrote data to 'recorded.raw'\n");
        }
    }
#endif


done:
    Pa_Terminate();
    if( data.recordedSamples )       /* Sure it is NULL or valid. */
        free( data.recordedSamples );
    if( err != paNoError )
    {
        fprintf( stderr, "An error occured while using the portaudio stream\n" );
        fprintf( stderr, "Error number: %d\n", err );
        fprintf( stderr, "Error message: %s\n", Pa_GetErrorText( err ) );
        err = 1;          /* Always return 0 or 1, but no other return codes. */
    }
    fftw_destroy_plan(plan);
    return err;
}
