#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
#include <map>

extern "C" {
    typedef struct __attribute__ ((packed)) {
        uint32_t 	    loopLength;
        uint32_t    	size;
        uint32_t    	c2Freq;
        signed char		fineTune;
        signed char		relativeNote;
        unsigned char  	volDefault;
        signed char		panDefault;
        unsigned char  	loop;
        unsigned char	hq;
        signed char  	data[1];
    } Sample;

    typedef struct __attribute__ ((packed)) {
        unsigned short	coord, inc;
    } EnvNode;

    typedef struct __attribute__ ((packed)) {
        EnvNode			nodes[ 12 ];
        unsigned char	max;
        unsigned char	sus;
        unsigned char	loopStart;
        unsigned char	flags;
    } Envelope;


    typedef struct __attribute__ ((packed)) {
        unsigned short	samples[ 96 ];

        Envelope		envVol;
        Envelope		envPan;
        unsigned short	volFade;

        unsigned char	vibType;
        unsigned char	vibSweep;
        unsigned char	vibDepth;
        unsigned char	vibRate;
    } Instrument;

    typedef struct __attribute__ ((packed)) {
        unsigned short 	index[ 16 ];
        unsigned short	rows;
        unsigned char 	data[1];
    } Pattern;

    typedef struct __attribute__ ((packed)) {
        unsigned char 	channels;
        unsigned char 	numOrders;
        unsigned char	songRestart;
        unsigned char 	order[ 256 ];

        signed char 	channelPan[ 32 ];

        unsigned char 	songIndex[ 64 ];

        unsigned char 	volGlobal;

        unsigned char 	initSpeed;
        unsigned char 	initBPM;

        unsigned char	flagInstrumentBased;
        unsigned char	flagLinearSlides;
        unsigned char 	flagVolSlides;
        unsigned char 	flagVolOpt;
        unsigned char 	flagAmigaLimits;
        unsigned char	___padding;

        const Pattern* 	patterns[1];
    } Module;
}

Pattern * readPatternFile(std::string path) {
    FILE* fp = fopen(path.c_str(), "rb");
    fseek(fp, 0, SEEK_END);
    uint32_t size = ftell(fp);
    rewind(fp);
    Pattern * retval = (Pattern*)malloc(size);
    memset(retval, 0, size);
    fread(retval, size, 1, fp);
    fclose(fp);
    return retval;
}

Module * readModuleFile(std::string path, std::vector<std::string>& patternPaths) {
    FILE* fp = fopen(path.c_str(), "rb");
    Module * retval = (Module*)malloc(sizeof(Module) + sizeof(Pattern*) * patternPaths.size());
    memset(retval, 0, sizeof(retval));
    fread(retval, sizeof(Module) - sizeof(Pattern*), 1, fp);
    fclose(fp);
    for (int i = 0; i < patternPaths.size(); i++) retval->patterns[i] = readPatternFile(patternPaths[i]);
    return retval;
}

Instrument readInstrumentFile(std::string path) {
    FILE* fp = fopen(path.c_str(), "rb");
    Instrument retval;
    fread(&retval, sizeof(retval), 1, fp);
    fclose(fp);
    return retval;
}

Sample * readSampleFile(std::string path) {
    FILE* fp = fopen(path.c_str(), "rb");
    fseek(fp, 0, SEEK_END);
    uint32_t size = ftell(fp);
    rewind(fp);
    Sample * retval = (Sample*)malloc(size);
    memset(retval, 0, size);
    fread(retval, size, 1, fp);
    fclose(fp);
    return retval;
}

typedef struct {
    unsigned char xmflag;
    unsigned char note, volume, effect, effectop;
    unsigned short instrument;
} Note;

inline void fputcn(int c, int num, FILE* fp) {for (; num > 0; num--) fputc(c, fp);}

// XM file format from http://web.archive.org/web/20060809013752/http://pipin.tmd.ns.ac.yu/extra/fileformat/modules/xm/xm.txt
int main(int argc, const char * argv[]) {
    std::vector<std::string> sampleFiles, instrumentFiles, patternFiles;
    std::string moduleFile, outputFile;
    if (argc < 9) {
        fprintf(stderr, "Usage: %s <module.bin> <output.xm> <-i instruments...> <-p patterns...> <-s samples...>\n", argv[0]);
        return 1;
    }
    moduleFile = argv[1];
    outputFile = argv[2];
    int mode = 0;
    for (int i = 3; i < argc; i++) {
        if (std::string(argv[i]) == "-i") mode = 1;
        else if (std::string(argv[i]) == "-p") mode = 2;
        else if (std::string(argv[i]) == "-s") mode = 3;
        else if (mode == 1) instrumentFiles.push_back(argv[i]);
        else if (mode == 2) patternFiles.push_back(argv[i]);
        else if (mode == 3) sampleFiles.push_back(argv[i]);
    }
    if (instrumentFiles.empty() || patternFiles.empty() || sampleFiles.empty()) {
        fprintf(stderr, "Usage: %s <module.bin> <-i instruments...> <-p patterns...> <-s samples...>\n", argv[0]);
        return 1;
    }
    FILE* out = fopen(outputFile.c_str(), "wb");
    if (out == NULL) {
        fprintf(stderr, "Could not open output file %s for writing.\n", outputFile.c_str());
        return 2;
    }
    Module * mod = readModuleFile(moduleFile, patternFiles);
    printf("Writing header\n");
    fwrite("Extended Module: Krawall conversion  \032FastTracker II      \x04\x01\x14\x01\0\0", 1, 64, out);
    fputc(mod->numOrders, out);
    fputcn(0, 3, out);
    fputc(mod->channels, out);
    fputc(0, out);
    unsigned short pnum = patternFiles.size();
    fwrite(&pnum, 2, 1, out);
    pnum = instrumentFiles.size();
    fwrite(&pnum, 2, 1, out);
    fputc((mod->flagLinearSlides ? 1 : 0), out);
    fputc(0, out);
    fputc(mod->initSpeed, out);
    fputc(0, out);
    fputc(mod->initBPM, out);
    fputc(0, out);
    fwrite(mod->order, 1, 256, out);
    for (int i = 0; i < patternFiles.size(); i++) {
        printf("Writing pattern %d (%s)\n", i, patternFiles[i].c_str());
        fputc(9, out);
        fputcn(0, 4, out);
        fwrite(&mod->patterns[i]->rows, 2, 1, out);
        uint32_t sizePos = ftell(out);
        fputcn(0, 2, out);
        const unsigned char * data = mod->patterns[i]->data;
        Note * thisrow = (Note*)calloc(mod->channels, sizeof(Note));
        for (int row = 0; row < mod->patterns[i]->rows; row++) {
            memset(thisrow, 0, sizeof(Note) * mod->channels);
            for (;;) {
                unsigned char follow = *data++;
                if (!follow) break;
                unsigned char xmflag = 0x80;
                int channel = follow & 0x1f;
                unsigned char note = 0, volume = 0, effect = 0, effectop = 0;
                unsigned short instrument = 0;
                if (follow & 0x20) {
                    xmflag |= 0x03;
                    note = *data++;
                    instrument = *data++;
                    if (note & 0x80) {
                        instrument |= *data++ << 8;
                        note &= 0x7f;
                    }
                    if (note > 97 || note == 0) note = 97;
                }
                if (follow & 0x40) {
                    xmflag |= 0x04;
                    volume = *data++;
                }
                if (follow & 0x80) {
                    xmflag |= 0x18;
                    effect = *data++;
                    effectop = *data++;
                }
                thisrow[channel].xmflag = xmflag;
                thisrow[channel].note = note;
                thisrow[channel].instrument = instrument;
                thisrow[channel].volume = volume;
                thisrow[channel].effect = effect;
                thisrow[channel].effectop = effectop;
            }
            for (int i = 0; i < mod->channels; i++) {
                if (thisrow[i].xmflag) {
                    fputc(thisrow[i].xmflag, out);
                    if (thisrow[i].xmflag & 0x01) fputc(thisrow[i].note, out);
                    if (thisrow[i].xmflag & 0x02) fputc(thisrow[i].instrument & 0x7F, out);
                    if (thisrow[i].xmflag & 0x04) fputc(thisrow[i].volume, out);
                    if (thisrow[i].xmflag & 0x08) fputc(thisrow[i].effect, out);
                    if (thisrow[i].xmflag & 0x10) fputc(thisrow[i].effectop, out);
                } else fputc(0x80, out);
            }
        }
        free(thisrow);
        uint32_t endPos = ftell(out);
        fseek(out, sizePos, SEEK_SET);
        unsigned short size = endPos - sizePos - 2;
        fwrite(&size, 2, 1, out);
        fseek(out, endPos, SEEK_SET);
    }
    for (int i = 0; i < instrumentFiles.size(); i++) {
        printf("Writing instrument %d (%s)\n", i, instrumentFiles[i].c_str());
        Instrument instr = readInstrumentFile(instrumentFiles[i]);
        std::vector<unsigned short> samples;
        samples.resize(96);
        samples.erase(std::unique_copy(instr.samples, instr.samples + 96, samples.begin()), samples.end());
        unsigned short snum = samples.size();
        fputc(snum == 0 ? 29 : 252, out);
        fputcn(0, 3, out);
        char name[22];
        memset(name, 0, 22);
        memcpy(name, instrumentFiles[i].c_str(), std::min(instrumentFiles[i].size(), (size_t)22));
        fwrite(name, 1, 22, out);
        fputc(0, out);
        fwrite(&snum, 2, 1, out);
        if (snum == 0) continue;
        std::map<unsigned short, unsigned short> sample_conversion;
        for (unsigned short i = 0; i < snum; i++) sample_conversion[samples[i]] = i + 1;
        for (int i = 0; i < 96; i++) instr.samples[i] = sample_conversion[instr.samples[i]];
        fputc(40, out);
        fputcn(0, 3, out);
        fwrite(instr.samples, 1, 96, out);
        fwrite(instr.envVol.nodes, 4, 12, out);
        fwrite(instr.envPan.nodes, 4, 12, out);
        fputc(instr.envVol.max, out);
        fputc(instr.envPan.max, out);
        fputc(instr.envVol.sus, out);
        fputc(instr.envVol.loopStart, out);
        fputc(instr.envVol.max, out);
        fputc(instr.envPan.sus, out);
        fputc(instr.envPan.loopStart, out);
        fputc(instr.envPan.max, out);
        fputc(instr.envVol.flags, out);
        fputc(instr.envPan.flags, out);
        fputc(instr.vibType, out);
        fputc(instr.vibSweep, out);
        fputc(instr.vibDepth, out);
        fputc(instr.vibRate, out);
        fwrite(&instr.volFade, 2, 1, out);
        fputcn(0, 11, out);
        std::vector<Sample*> sarr;
        for (int j = 0; j < snum; j++) {
            printf("Writing sample %d (%s)\n", j, sampleFiles[samples[j]].c_str());
            Sample * s = readSampleFile(sampleFiles[samples[j]]);
            if (s->hq) {
                uint32_t ssize = s->size / 2;
                fwrite(&ssize, 4, 1, out);
            } else fwrite(&s->size, 4, 1, out);
            if (s->loopLength == 0) fputcn(0, 4, out);
            else {
                uint32_t start = s->size - s->loopLength;
                fwrite(&start, 4, 1, out);
            }
            fwrite(&s->loopLength, 4, 1, out);
            fputc(s->volDefault, out);
            fputc(s->fineTune, out);
            fputc((s->loop ? 1 : 0) | (s->hq ? 4 : 0), out);
            fputc(s->panDefault + 0x80, out);
            fputc(s->relativeNote, out);
            fputc(0, out);
            memset(name, ' ', 22);
            memcpy(name, sampleFiles[samples[j]].c_str(), std::min(sampleFiles[samples[j]].size(), (size_t)22));
            fwrite(name, 1, 22, out);
            sarr.push_back(s);
        }
        for (int j = 0; j < snum; j++) {
            Sample * s = sarr[j];
            if (s->hq) { // 16-bit
                signed short old = 0;
                for (uint32_t k = 0; k < s->size; k+=2) {
                    fputc(((signed short*)s->data)[k] - old, out);
                    old = ((signed short*)s->data)[k];
                }
            } else { // 8-bit
                unsigned char old = 0;
                for (uint32_t k = 0; k < s->size; k++) {
                    fputc(((int)s->data[k] + 0x80) - old, out);
                    old = (int)s->data[k] + 0x80;
                }
            }
            free(s);
        }
        sarr.clear();
    }
    for (int i = 0; i < patternFiles.size(); i++) free((void*)mod->patterns[i]);
    free(mod);
    fclose(out);
    printf("Successfully wrote module to %s.\n", outputFile.c_str());
}
