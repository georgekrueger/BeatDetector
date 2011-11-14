/*
 *  wavfile.h
 *
 *  Created by Judah Menter on 1/6/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */
#ifndef WAVFILE_H
#define WAVFILE_H

/**
 * Allocates a sample buffer and reads a wav file into it.
 * @param path The wav file path.
 * @param buffer Buffer pointer to point at the read sample data.
 * @param numFrames Value to hold the number of stereo frames read.
 */
bool ReadWavFile(const char *path, float** buffer, unsigned int *numFrames, unsigned int *numChannels);

/**
 * Writes a wav file header to a file.
 * @param file The file to write to.
 * @param sampleRate The sample rate of the audio data.
 */
bool WriteWavHeader(FILE *file, int sampleRate, short numChannels);

/**
 * Completes a wav file by writing the data size to the header.
 * @param file The file to write to.
 * @param numBytes The size of the audio data in bytes.
 */
bool WriteWavDataSize(FILE *file, unsigned int numBytes);

#endif  /* WAVFILE_H */
