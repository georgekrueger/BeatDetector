#include <assert.h>
#include <string>
#include <iostream>
#include "../portaudio/include/portaudio.h"
#include "WavFile.h"
#include <math.h>

#include "jdksmidi/world.h"
#include "jdksmidi/track.h"
#include "jdksmidi/multitrack.h"
#include "jdksmidi/filereadmultitrack.h"
#include "jdksmidi/fileread.h"
#include "jdksmidi/fileshow.h"
#include "jdksmidi/filewritemultitrack.h"
using namespace jdksmidi;

#include <map>
#include <vector>
#include <string>
#include <fstream>

using namespace std;

static int PortAudioCallback( const void *inputBuffer, void *outputBuffer,
							 unsigned long framesPerBuffer,
							 const PaStreamCallbackTimeInfo* timeInfo,
							 PaStreamCallbackFlags statusFlags,
							 void *userData );

const int SampleRate = 44100;
const int InNumChannels = 1;
//float BPM = 120;
//unsigned long BeatTimeInSamples = (1/BPM) * 60 * 44100;
const int MaxRecordLengthInSeconds = 60;
const int RecordBufferNumSamples = SampleRate * MaxRecordLengthInSeconds * InNumChannels;
float RecordBuffer[RecordBufferNumSamples];
unsigned long RecordBufferPos = 0;
PaStream* Stream;

class Audio
{
public:
	Audio()
	{
	}

	~Audio()
	{
		Stop();
	}

	bool Start()
	{
		// Initialize Portaudio
		PaError err = Pa_Initialize();
		if (err != paNoError) { assert(false); return false; }

		PaDeviceIndex deviceIndex = Pa_GetDefaultInputDevice();	
		const PaDeviceInfo* deviceInfo = Pa_GetDeviceInfo(deviceIndex);
		cout << "Default Input Device Name: " << deviceInfo->name << endl;
		cout << "Max Input Channels: " << deviceInfo->maxInputChannels << endl;
		cout << "Max Output Channels: " << deviceInfo->maxOutputChannels << endl;

		PaStreamParameters inParams;
		inParams.device = deviceIndex;
		inParams.channelCount = InNumChannels;
		inParams.sampleFormat = paFloat32;
		inParams.suggestedLatency = Pa_GetDeviceInfo(deviceIndex)->defaultLowOutputLatency;
		inParams.hostApiSpecificStreamInfo = NULL;

		err = Pa_OpenStream(&Stream, &inParams, NULL, 44100, 512, 0, PortAudioCallback, NULL );
		if (err != paNoError) { assert(false); return false; }
		
		err = Pa_StartStream( Stream );
		if (err != paNoError) { assert(false); return false; }
	}

	void Stop()
	{
		PaError err = Pa_StopStream( Stream );
		if (err != paNoError) { assert(false); }
		
		err = Pa_CloseStream( Stream );
		if (err != paNoError) { assert(false); }
		
		Pa_Terminate();
	}
};

void Audio_DetectEnvelope(float* BufferIn, unsigned long BufferInNumSamples, 
						  float* BufferOut, unsigned long SampleRate,
						  float AttackSeconds, float ReleaseSeconds)
{
	//attack and release in seconds
	float ga = (float) exp(-1 / (SampleRate * AttackSeconds));
	float gr = (float) exp(-1 / (SampleRate * ReleaseSeconds));

	float envelope=0;

	for(unsigned long i=0; i<BufferInNumSamples; i++)
	{
		float input = BufferIn[i];

		//get your data into 'input'
		float envIn = fabs(input);

		if(envelope < envIn) {
			envelope *= ga;
			envelope += (1-ga)*envIn;
		}
		else {
			envelope *= gr;
			envelope += (1-gr)*envIn;
		}
		BufferOut[i] = envelope;
	}
}

void WriteBufferToWavFile(const char* Filename, float* Buffer, unsigned long SizeInBytes, unsigned long SampleRate)
{
	FILE* f = fopen(Filename, "w");			
	WriteWavHeader(f, SampleRate, 1);	
	fwrite(Buffer, SizeInBytes, 1, f);
	WriteWavDataSize(f, SizeInBytes);
	fclose(f);
}

void Normalize(float* Items, unsigned long NumItems, float To)
{
	float Max = 0;
	for (int i=0; i<NumItems; i++) {
		if (Items[i] > Max) {
			Max = Items[i];
		}
	}
	if (Max < To) {
		float Mult = To / Max;
		for (int i=0; i<NumItems; i++) {
			Items[i] *= Mult;
		}
	}
}

void WriteMidiFile(const char* Filename, MIDIMultiTrack* tracks, int numTracks )
{
    MIDIFileWriteStreamFileName out_stream( Filename );
    if( out_stream.IsValid() )
    {
        // the object which takes the midi tracks and writes the midifile to the output stream
        MIDIFileWriteMultiTrack writer( tracks, &out_stream );

        // write the output file
        if ( writer.Write( numTracks ) )
        {
            cout << "\nOK writing file " << Filename << endl;
        }
        else
        {
            cerr << "\nError writing file " << Filename << endl;
        }
    }
    else
    {
        cerr << "\nError opening file " << Filename << endl;
    }
}

uint SamplesToMilliseconds(uint samples, uint sampleRate)
{
	uint result = (samples / (float)sampleRate) * 1000;
	return result;
}

void AudioToMidi(float* Buffer, unsigned long NumSamples, unsigned long SampleRate, const char* Filename)
{
	// convert audio buffer into envelope buffer
	float* EnvBuffer = new float[NumSamples];
	Audio_DetectEnvelope(Buffer, NumSamples, EnvBuffer, SampleRate, .02, .02);
	Normalize(EnvBuffer, NumSamples, 0.95);

    MIDITimedBigMessage m; // the object for individual midi events
    unsigned char chan, // internal midi channel number 0...15 (named 1...16)
        note, velocity, ctrl, val;

    MIDIClockTime t; // time in midi ticks
    MIDIMultiTrack tracks(2, false);  // the object which will hold all the tracks
	MIDITrack infoTrack;
	MIDITrack eventTrack;

	vector<MIDITimedBigMessage> events;

	// extract events from envelope buffer
	const float ThresholdOn = 0.4;
	const float ThresholdOff = 0.2;
	int NoteOnSampleNum = 0;
	bool State = false;
	uint firstEventTime = 0;
	uint lastEventTime = 0;
	uint NumEvents = 0;
	float NoteOnVelocity = 0;

	for (unsigned long i=0; i<NumSamples; i++) {
		float Sample = EnvBuffer[i];
		if (State) {
			if (Sample > NoteOnVelocity) NoteOnVelocity = Sample;

			if (Sample < ThresholdOff) 
			{
				m.SetTime(NoteOnSampleNum);
				m.SetNoteOn( 0, 60, 127 * NoteOnVelocity); 
				events.push_back(m);
				
				m.SetTime(i);
				m.SetNoteOff( 0, 60, 127 * NoteOnVelocity); 
				events.push_back(m);

				cout << "Note Start " << SamplesToMilliseconds(NoteOnSampleNum, SampleRate) << "ms" << 
						" End " << SamplesToMilliseconds(i, SampleRate) << "ms" << 
						" Vel " << int(127 * NoteOnVelocity) << "(" << NoteOnVelocity << ")" << endl;
				
				State = false;
			}
		}
		else {
			if (Sample > ThresholdOn) {
				State = true;
				NoteOnSampleNum = i;
				NoteOnVelocity = Sample;

				if (NumEvents == 0) {
					firstEventTime = i;
				}
				lastEventTime = i;
				NumEvents++;
			}
		}
	}

	// done with envelope buffer
	delete EnvBuffer;

	if (NumEvents <= 2) {
		cerr << "Not enough events in pattern" << endl;
		return;
	}

	// find the BPM of the pattern
	cout << "firstEventTime: " << SamplesToMilliseconds(firstEventTime, SampleRate) << "ms" << endl;
	cout << "lastEventTime: " << SamplesToMilliseconds(lastEventTime, SampleRate) << "ms" << endl;
	float patternLengthSec = (lastEventTime - firstEventTime) / (float)SampleRate;
	uint minBpm = 80;
	uint bar = 0;
	float bpm = 0;
	cout << "patternLength: " << int(patternLengthSec * 1000) << "ms" << endl;

	do {
		bar++;
		float beatLengthSec = patternLengthSec / (bar * 4);
		bpm = (1 / beatLengthSec) * 60;
		cout << "Try bpm: " << bpm << endl;
	} while(bpm < minBpm);

	cout << "BPM: " << bpm << endl;
	cout << "Bars: " << bar << endl;

	float targetBpm = 120;
	int MinuteToTickMult = 60000000;
	float BeatsPerSecond = targetBpm / 60;
    int TicksPerBeat = 100; // number of ticks in quarter note (1...32767)
	float TicksPerSecond = TicksPerBeat * BeatsPerSecond;
	float SampleToTickMult = TicksPerSecond / SampleRate;
	uint firstNoteOffsetInTicks = int(firstEventTime * SampleToTickMult);

    tracks.SetClksPerBeat( TicksPerBeat );

	// add event for time signature
    m.SetTime(0);
    m.SetTimeSig(4, 2); // measure 4/4 (default values for time signature)
    infoTrack.PutEvent(m);

	uint beatTimeInUSec = (1/targetBpm) * MinuteToTickMult;
    m.SetTempo(beatTimeInUSec);
    infoTrack.PutEvent(m);

	tracks.SetTrack(0, &infoTrack);


	// get rid of last note event (on & off), because the last note is just
	// used to signify end of pattern
	events.pop_back();
	events.pop_back();

	float conv = bpm / targetBpm;
	cout << "BPM Conversion Factor: " << conv << endl;

	// process the events
	cout << "Processed events" << endl;
	for (uint i=0; i<events.size(); i++)
	{
		MIDITimedBigMessage* msg = &events[i];

		// shift notes to start at beginning of pattern and convert to 120BPM
		uint eventTimeInSamplesOrig = (msg->GetTime() - firstEventTime);
		uint eventTimeInSamples = (msg->GetTime() - firstEventTime) * conv;
		uint eventTimeInTicks = eventTimeInSamples * SampleToTickMult;
		msg->SetTime(eventTimeInTicks);
		eventTrack.PutEvent(*msg);

		if (msg->GetType() == NOTE_ON) {
			cout << "Note: Orig: " << SamplesToMilliseconds(eventTimeInSamplesOrig, SampleRate) << "ms " <<
					SamplesToMilliseconds(eventTimeInSamples, SampleRate) << "ms " <<
					"(" << eventTimeInTicks << " ticks)" << endl;
		}
	}

	tracks.SetTrack(1, &eventTrack);

	WriteMidiFile(Filename, &tracks, 2);
}

int main(int argc, char** argv)
{
	Audio audio;
	audio.Start();

	while(1)
	{
		string command;
		getline(cin, command);

		if (command == "q" || command == "quit")
		{
			audio.Stop();
		
			unsigned long NumSamples = RecordBufferPos;

			// write out audio data to file
			/*FILE* f = fopen("out.wav", "w");			
			WriteWavHeader(f, SampleRate, 1);	
			unsigned long SizeInBytes = NumSamples * sizeof(float);		
			cout << "Num Samples: " << NumSammples << ", Buffer Size: " << SizeInBytes << endl;
			fwrite(RecordBuffer, SizeInBytes, 1, f);
			WriteWavDataSize(f, SizeInBytes);
			fclose(f);*/

			AudioToMidi(RecordBuffer, NumSamples, SampleRate, "out.mid");

			exit(1);
		}
	}
}

static int PortAudioCallback( const void *inputBuffer, void *outputBuffer,
							 unsigned long framesPerBuffer,
							 const PaStreamCallbackTimeInfo* timeInfo,
							 PaStreamCallbackFlags statusFlags,
							 void *userData )
{
	if (RecordBufferPos >= RecordBufferNumSamples) {
		std::cerr << "Record buffer is full" << std::endl;
		return paAbort;
	}

	float* inBuffer = (float*)inputBuffer;
	uint inBufferCursor = 0;
	for (uint i=0; i<framesPerBuffer; i++)
	{
		RecordBuffer[RecordBufferPos] = inBuffer[inBufferCursor];
		RecordBufferPos++;
		inBufferCursor += InNumChannels;
	}

	return paContinue;
}
