/*
 * UnkrawerterGBA
 * Version 0.9
 * 
 * This program automatically extracts music files from Gameboy Advance games
 * that use the Krawall sound engine. Audio files are extracted in the XM module
 * format, which can be opened by programs such as OpenMPT.
 * 
 * Copyright (c) 2020 JackMacWindows.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <tuple>
#include <algorithm>
#include <string>
#include <algorithm>
#include <map>

bool dwordAlignment = true; // maybe change this if offsets aren't 4-byte aligned?
                            // I haven't found any blocks that aren't, but who knows, maybe there are ROMs that aren't?

const char * typemap[] = {
    "unknown",
    "module",
    "sample",
    "module or sample",
    "instrument",
    "instrument or module",
    "instrument or sample",
    "any"
};

struct OffsetSearchResult {
    bool success = false;
    uint32_t instrumentAddr = 0;
    uint32_t instrumentCount = 0;
    uint32_t sampleAddr = 0;
    uint32_t sampleCount = 0;
    std::vector<uint32_t> modules;
};

OffsetSearchResult searchForOffsets(FILE* fp, int threshold = 4, bool verbose = false) {
    OffsetSearchResult retval;
    fseek(fp, 0, SEEK_END);
    uint32_t romSize = ftell(fp);
    rewind(fp);
    std::vector<std::tuple<uint32_t, uint32_t, int> > foundAddressLists;
    uint32_t startAddress = 0, count = 0;
    if (dwordAlignment) {
        // Look for lists of pointers (starting with 0x08xxxxxx)
        uint32_t lastDword = 0;
        while (!feof(fp) && !ferror(fp)) {
            fread(&lastDword, 4, 1, fp);
            if ((lastDword & 0x08000000) && !(lastDword & 0xF6000000) && (lastDword & 0x1ffffff) < romSize && lastDword != 0x08080808 && !((uint16_t)(lastDword >> 16) - (uint16_t)(lastDword & 0xffff) < 4 && (lastDword & 0x00ff00ff) == 0x00080008)) {
                if (startAddress == 0 || count == 0) startAddress = ftell(fp) - 4;
                count++;
            } else if (count >= threshold && count < 1024) {
                foundAddressLists.push_back(std::make_tuple(startAddress, count, 0));
                startAddress = 0;
                count = 0;
            } else if (count > 0) {
                startAddress = count = 0;
            }
        }
    } else {
        fprintf(stderr, "Unimplemented\n");
        return retval;
    }

    // Erase a few matches
    foundAddressLists.erase(std::remove_if(foundAddressLists.begin(), foundAddressLists.end(), [fp](std::tuple<uint32_t, uint32_t, int>& addr)->bool {
        // Check for consecutive addresses
        int numsize = std::min(std::get<1>(addr), 4u);
        uint32_t nums[4];
        fseek(fp, std::get<0>(addr), SEEK_SET);
        for (int i = 0; i < numsize; i++) fread(nums + i, 4, 1, fp);
        for (int i = 1; i < numsize; i++) if ((int32_t)nums[i] - (int32_t)nums[i-1] < 0x10) return true; // Spacing too close
        return false;
    }), foundAddressLists.end());

    // Find the type of each match
    std::for_each(foundAddressLists.begin(), foundAddressLists.end(), [fp](std::tuple<uint32_t, uint32_t, int> &p) {
        int possible_mask = 0b111;
        do { // Check for module
            fseek(fp, std::get<0>(p) - 8, SEEK_SET);
            uint32_t tmp = fgetc(fp);
            if (tmp == 0 || tmp > 0x10) {possible_mask &= 0b110; break;}
            tmp = fgetc(fp);
            if (tmp < 30 || tmp > 200) {possible_mask &= 0b110; break;} // tweak this?
            for (int i = 0; i < 5; i++) if (fgetc(fp) & 0xfe) {possible_mask &= 0b110; break;}
            if (!(possible_mask & 1)) break;
            if (fgetc(fp)) {possible_mask &= 0b110; break;}
            fread(&tmp, 4, 1, fp);
            fseek(fp, tmp & 0x1ffffff, SEEK_SET);
            if (fgetc(fp) || fgetc(fp)) {possible_mask &= 0b110; break;}
            fgetc(fp);
            if (fgetc(fp)) {possible_mask &= 0b110; break;}
            fseek(fp, 28, SEEK_CUR);
            uint16_t tmp2 = 0;
            fread(&tmp2, 2, 1, fp);
            if (tmp2 > 256 || (tmp2 & 7)) {possible_mask &= 0b110; break;}
        } while (0);

        for (int i = 0; i < std::min(std::get<1>(p), 4u); i++) { // Check for sample
            fseek(fp, std::get<0>(p) + i*4, SEEK_SET);
            uint32_t addr = 0;
            fread(&addr, 4, 1, fp);
            fseek(fp, addr & 0x1ffffff, SEEK_SET);
            uint32_t tmp = 0, end = 0;
            fread(&tmp, 4, 1, fp);
            fread(&end, 4, 1, fp);
            if (!(end & 0x08000000) || (end & 0xf6000000) || end <= addr + 18 || tmp > end - addr - 18) {possible_mask &= 0b101; break;}
            fread(&tmp, 4, 1, fp);
            if (tmp > 0xFFFF) {possible_mask &= 0b101; break;}
            fseek(fp, 4, SEEK_CUR);
            if ((fgetc(fp) & 0xfe) || (fgetc(fp) & 0xfe)) {possible_mask &= 0b101; break;}
        }

        for (int n = 0; n < std::min(std::get<1>(p), 4u); n++) { // Check for instrument
            fseek(fp, std::get<0>(p) + n*4, SEEK_SET);
            uint32_t addr = 0;
            fread(&addr, 4, 1, fp);
            fseek(fp, addr & 0x1ffffff, SEEK_SET);
            uint16_t tmp = 0, last = 0;
            for (int i = 0; i < 96; i++) {
                fread(&tmp, 2, 1, fp);
                if (tmp > 256 || (i > 0 && abs((int32_t)tmp - (int32_t)last) > 16)) {possible_mask &= 0b011; break;}
                last = tmp;
            }
            if (!(possible_mask & 4)) break;
            fseek(fp, 48, SEEK_CUR);
            fgetc(fp); //if (fgetc(fp) > 12) {possible_mask &= 0b011; break;}
            if (fgetc(fp) > 12) {possible_mask &= 0b011; break;}
            if (fgetc(fp) > 12) {possible_mask &= 0b011; break;}
            fgetc(fp); //if (fgetc(fp) > 0x10) {possible_mask &= 0b011; break;} // I think?
            fseek(fp, 48, SEEK_CUR);
            fgetc(fp); //if (fgetc(fp) > 12) {possible_mask &= 0b011; break;}
            if (fgetc(fp) > 12) {possible_mask &= 0b011; break;}
            if (fgetc(fp) > 12) {possible_mask &= 0b011; break;}
            fgetc(fp); //if (fgetc(fp) > 0x10) {possible_mask &= 0b011; break;}
        }
        std::get<2>(p) = possible_mask;
    });

    // Show results if verbose
    if (verbose) std::for_each(foundAddressLists.begin(), foundAddressLists.end(), [](std::tuple<uint32_t, uint32_t, int> p){printf("Found %d matches at %08X with type %s\n", std::get<1>(p), std::get<0>(p), typemap[std::get<2>(p)]);});

    // Filter results down to one instrument & sample list, and all modules
    for (auto p : foundAddressLists) {
        if (std::get<2>(p) == 1) retval.modules.push_back(std::get<0>(p));
        else if (std::get<2>(p) == 2 && std::get<1>(p) > retval.sampleCount) {retval.sampleCount = std::get<1>(p); retval.sampleAddr = std::get<0>(p);}
        else if (std::get<2>(p) == 4 && std::get<1>(p) > retval.instrumentCount) {retval.instrumentCount = std::get<1>(p); retval.instrumentAddr = std::get<0>(p);}
    }

    if (retval.instrumentAddr) printf("> Found instrument list at address %08X\n", retval.instrumentAddr);
    if (retval.sampleAddr) printf("> Found sample list at address %08X\n", retval.sampleAddr);
    for (int i = 0; i < retval.modules.size(); i++) {
        retval.modules[i] = (retval.modules[i] & 0x1ffffff) - 364;
        printf("> Found module at address %08X\n", retval.modules[i]);
    }

    retval.success = retval.instrumentAddr && retval.sampleAddr && !retval.modules.empty();
    return retval;
}

// This can be used later?
void readSampleToWAV(FILE* fp, uint32_t offset, const char * filename) {
    fseek(fp, offset, SEEK_SET);
    unsigned long loopLength = 0, end = 0;
    fread(&loopLength, 4, 1, fp);
    fread(&end, 4, 1, fp);
    end &= 0x1ffffff;
    unsigned long currentSize = end - ftell(fp) - 10;
    FILE* wav = fopen(filename, "wb");
    fwrite("RIFF", 4, 1, wav);
    unsigned long sampleRate = 0;
    fread(&sampleRate, 4, 1, fp);
    for (int i = 0; i < 6; i++) fgetc(fp);
    unsigned long currentOffset = ftell(fp);
    unsigned long size = end - currentOffset + 18;
    fwrite(&size, 4, 1, wav);
    fwrite("WAVEfmt \x10\0\0\0\x01\0\x01\0", 16, 1, wav);
    fwrite(&sampleRate, 4, 1, wav);
    fwrite(&sampleRate, 4, 1, wav);
    fwrite("\x01\0\x08\0data", 8, 1, wav);
    size -= 36;
    fwrite(&size, 4, 1, wav);
    char * data = (char*)malloc(size);
    fread(data, 1, size, fp);
    fwrite(data, 1, size, wav);
    fclose(wav);
}

extern "C" {
#ifdef _MSC_VER
#pragma pack(push, 1)
#define PACKED 
#endif
#ifdef __GNUC__
#define PACKED __attribute__ ((packed))
#endif
    typedef struct PACKED {
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

    typedef struct PACKED {
        unsigned short	coord, inc;
    } EnvNode;

    typedef struct PACKED {
        EnvNode			nodes[ 12 ];
        unsigned char	max;
        unsigned char	sus;
        unsigned char	loopStart;
        unsigned char	flags;
    } Envelope;


    typedef struct PACKED {
        unsigned short	samples[ 96 ];

        Envelope		envVol;
        Envelope		envPan;
        unsigned short	volFade;

        unsigned char	vibType;
        unsigned char	vibSweep;
        unsigned char	vibDepth;
        unsigned char	vibRate;
    } Instrument;

    typedef struct PACKED {
        unsigned short 	index[ 16 ];
        unsigned short	rows;
        unsigned char 	data[1];
    } Pattern;

    typedef struct PACKED {
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
#ifdef _MSC_VER
#pragma pack(pop)
#endif
#ifdef PACKED
#undef PACKED
#endif
}

Pattern * readPatternFile(FILE* fp, uint32_t offset) {
    fseek(fp, offset + 32, SEEK_SET);
    std::vector<uint8_t> fileContents;
    unsigned short rows = 0;
    fread(&rows, 2, 1, fp);
    for (int row = 0; row < rows; row++) {
        for (;;) {
            unsigned char follow = fgetc(fp);
            fileContents.push_back(follow);
            if (!follow) break;
            if (follow & 0x20) {
                unsigned char note = fgetc(fp);
                fileContents.push_back(note);
                fileContents.push_back(fgetc(fp));
                if (note & 0x80) fileContents.push_back(fgetc(fp));
            }
            if (follow & 0x40) {
                fileContents.push_back(fgetc(fp));
            }
            if (follow & 0x80) {
                fileContents.push_back(fgetc(fp));
                fileContents.push_back(fgetc(fp));
            }
        }
    }
    fseek(fp, offset, SEEK_SET);
    Pattern * retval = (Pattern*)malloc(34 + fileContents.size());
    fread(retval->index, 2, 16, fp);
    fseek(fp, 2, SEEK_CUR);
    retval->rows = rows;
    memcpy(retval->data, &fileContents[0], fileContents.size());
    return retval;
}

Module * readModuleFile(FILE* fp, uint32_t offset) {
    Module * retval = (Module*)malloc(sizeof(Module));
    memset(retval, 0, sizeof(Module));
    fseek(fp, offset, SEEK_SET);
    fread(retval, 364, 1, fp);
    unsigned char maxPattern = 0;
    for (int i = 0; i < retval->numOrders; i++) maxPattern = std::max(maxPattern, retval->order[i]);
    Module * retval2 = (Module*)malloc(sizeof(Module) + sizeof(Pattern*) * (maxPattern + 1));
    memcpy(retval2, retval, sizeof(Module));
    uint32_t addr = 0;
    for (int i = 0; i <= maxPattern; i++) {
        fseek(fp, offset + 364 + i*4, SEEK_SET);
        fread(&addr, 4, 1, fp);
        if (!(addr & 0x08000000) || (addr & 0xf6000000))
            break;
        retval2->patterns[i] = readPatternFile(fp, addr & 0x1ffffff);
    }
    return retval2;
}

Instrument readInstrumentFile(FILE* fp, uint32_t offset) {
    fseek(fp, offset, SEEK_SET);
    Instrument retval;
    fread(&retval, sizeof(retval), 1, fp);
    return retval;
}

Sample * readSampleFile(FILE* fp, uint32_t offset) {
    fseek(fp, offset + 4, SEEK_SET);
    uint32_t size = 0;
    fread(&size, 4, 1, fp);
    size &= 0x1ffffff;
    size -= offset;
    fseek(fp, offset, SEEK_SET);
    Sample * retval = (Sample*)malloc(size);
    memset(retval, 0, size);
    fread(retval, size, 1, fp);
    retval->size = size - 18;
    return retval;
}

typedef struct {
    unsigned char xmflag;
    unsigned char note, volume, effect, effectop;
    unsigned short instrument;
} Note;

inline void fputcn(int c, int num, FILE* fp) {for (; num > 0; num--) fputc(c, fp);}

// XM file format from http://web.archive.org/web/20060809013752/http://pipin.tmd.ns.ac.yu/extra/fileformat/modules/xm/xm.txt
int writeModuleToXM(FILE* fp, uint32_t moduleOffset, const std::vector<uint32_t> &sampleOffsets, const std::vector<uint32_t> &instrumentOffsets, const char * filename) {
    FILE* out = fopen(filename, "wb");
    if (out == NULL) {
        fprintf(stderr, "Could not open output file %s for writing.\n", filename);
        return 2;
    }
    Module * mod = readModuleFile(fp, moduleOffset);
    unsigned char patternCount = 0;
    for (int i = 0; i < mod->numOrders; i++) patternCount = std::max(patternCount, mod->order[i]);
    patternCount++;
    //printf("Writing header\n");
    fwrite("Extended Module: Krawall conversion  \032FastTracker II      \x04\x01\x14\x01\0\0", 1, 64, out);
    fputc(mod->numOrders, out);
    fputcn(0, 3, out);
    fputc(mod->channels, out);
    fputc(0, out);
    unsigned short pnum = patternCount;
    fwrite(&pnum, 2, 1, out);
    pnum = instrumentOffsets.size();
    fwrite(&pnum, 2, 1, out);
    fputc((mod->flagLinearSlides ? 1 : 0), out);
    fputc(0, out);
    fputc(mod->initSpeed, out);
    fputc(0, out);
    fputc(mod->initBPM, out);
    fputc(0, out);
    fwrite(mod->order, 1, 256, out);
    for (int i = 0; i < patternCount; i++) {
        //printf("Writing pattern %d\n", i);
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
                if (channel >= mod->channels) continue;
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
    for (int i = 0; i < instrumentOffsets.size(); i++) {
        //printf("Writing instrument %d\n", i);
        Instrument instr = readInstrumentFile(fp, instrumentOffsets[i]);
        std::vector<unsigned short> samples;
        samples.resize(96);
        samples.erase(std::unique_copy(instr.samples, instr.samples + 96, samples.begin()), samples.end());
        unsigned short snum = samples.size();
        fputc(snum == 0 ? 29 : 252, out);
        fputcn(0, 3, out);
        char name[22];
        memset(name, 0, 22);
        snprintf(name, 22, "Instrument%d", i);
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
            if (samples[j] > sampleOffsets.size()) continue;
            //printf("Writing sample %d\n", j);
            Sample * s = readSampleFile(fp, sampleOffsets[samples[j]]);
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
            snprintf(name, 22, "Sample%d", i);
            fwrite(name, 1, 22, out);
            sarr.push_back(s);
        }
        for (int j = 0; j < sarr.size(); j++) {
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
    }
    for (int i = 0; i < patternCount; i++) free((void*)mod->patterns[i]);
    free(mod);
    fclose(out);
    printf("Successfully wrote module to %s.\n", filename);
    return 0;
}

bool fstr(FILE* fp, const char * str) {
    rewind(fp);
    const char * ptr = str;
    while (!feof(fp)) {
        if (!*ptr) return true;
        else if (fgetc(fp) == *ptr) ptr++;
        else str = ptr;
    }
    return false;
}

int main(int argc, const char * argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0) {
        fprintf(stderr, "Usage: %s <rom.gba> [output dir] [search threshold] [verbose]\n", argv[0]);
        return 1;
    }
    FILE* fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open file %s for reading.", argv[1]);
        return 2;
    }
    if (!fstr(fp, "Krawall")) printf("Warning: Could not find Krawall signature. Are you sure this game uses the Krawall engine?\n");
    rewind(fp);
    OffsetSearchResult offsets;
    if (argc > 3) offsets = searchForOffsets(fp, atoi(argv[3]), argc > 4);
    else offsets = searchForOffsets(fp);
    if (!offsets.success) {
        fprintf(stderr, "Could not find all of the offsets required.\n * Does the ROM use the Krawall engine?\n * Try adjusting the search threshold.\n * You may need to find offsets yourself.\n");
        return 3;
    }
    std::vector<uint32_t> sampleOffsets, instrumentOffsets;
    uint32_t tmp = 0;
    fseek(fp, offsets.sampleAddr, SEEK_SET);
    for (int i = 0; i < offsets.sampleCount; i++) {
        fread(&tmp, 4, 1, fp);
        sampleOffsets.push_back(tmp & 0x1ffffff);
    }
    fseek(fp, offsets.instrumentAddr, SEEK_SET);
    for (int i = 0; i < offsets.instrumentCount; i++) {
        fread(&tmp, 4, 1, fp);
        instrumentOffsets.push_back(tmp & 0x1ffffff);
    }
    for (int i = 0; i < offsets.modules.size(); i++) {
        std::string name = (argc > 2 ? std::string(argv[2]) + "/" : std::string()) + "Module" + std::to_string(i) + ".xm";
        writeModuleToXM(fp, offsets.modules[i], sampleOffsets, instrumentOffsets, name.c_str());
    }
    fclose(fp);
    return 0;
}
