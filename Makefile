
makeall: main.cpp
	g++ -g -o BeatDetector main.cpp WavFile.cpp -L../portaudio/lib -lportaudio -I../portaudio/include -Ljdksmidi/tmpbuild/tmp-target/build/lib -ljdksmidi -Ijdksmidi/include
