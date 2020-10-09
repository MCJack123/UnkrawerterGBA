/*
 * UnkrawerterGBA
 * Version 3.0
 * 
 * This program automatically extracts music files from Gameboy Advance games
 * that use the Krawall sound engine. Audio files are extracted in the XM or S3M
 * module format, which can be opened by programs such as OpenMPT.
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

// Maps type numbers detected in searchForOffsets to strings for display (only used in verbose mode)
static const char * typemap[] = {
    "unknown",
    "module",
    "sample",
    "module or sample",
    "instrument",
    "instrument or module",
    "instrument or sample",
    "any"
};

// Stores the Krawall version (used to determine some conversion parameters)
// This defaults to the latest version, but ROMs using versions before 2004-07-07 MUST set this
static uint32_t version = 0x20050421;

// Structure to hold results of offset search
struct OffsetSearchResult {
    bool success = false;
    uint32_t instrumentAddr = 0;
    uint32_t instrumentCount = 0;
    uint32_t sampleAddr = 0;
    uint32_t sampleCount = 0;
    std::vector<uint32_t> modules;
};

void unkrawerter_setVersion(uint32_t ver) {
    version = ver;
}

// Searches a ROM file pointer for offsets to modules, an instrument list, and a sample list.
// This looks for sets of 4-byte aligned addresses in the form 0x08xxxxxx or 0x09xxxxxx
// Once the sets are found, their types are determined by dereferencing the addresses and checking
// whether the data stored therein is consistent with the structure type.
// Sets that don't match exactly one type are discarded.
// Returns a structure with the addresses to the instrument & sample lists, as well as all modules.
OffsetSearchResult unkrawerter_searchForOffsets(FILE* fp, int threshold = 4, bool verbose = false) {
    OffsetSearchResult retval;
    fseek(fp, 0, SEEK_END);
    uint32_t romSize = ftell(fp); // Store the ROM's size so addresses that go over are ignored
    rewind(fp);
    std::vector<std::tuple<uint32_t, uint32_t, int> > foundAddressLists;
    uint32_t startAddress = 0, count = 0;
    // Look for lists of pointers (starting with 0x08xxxxxx or 0x09xxxxxx)
    uint32_t lastDword = 0;
    while (!feof(fp) && !ferror(fp)) {
        fread(&lastDword, 4, 1, fp);
        if ((lastDword & 0x08000000) && !(lastDword & 0xF6000000) && (lastDword & 0x1ffffff) < romSize && lastDword != 0x08080808 && !((uint16_t)(lastDword >> 16) - (uint16_t)(lastDword & 0xffff) < 4 && (lastDword & 0x00ff00ff) == 0x00080008)) {
            // Count this address in a set
            if (startAddress == 0 || count == 0) startAddress = ftell(fp) - 4;
            count++;
        } else if (count >= threshold && count < 1024) {
            // We found an address list, add it to the results
            foundAddressLists.push_back(std::make_tuple(startAddress, count, 0));
            startAddress = 0;
            count = 0;
        } else if (count > 0) {
            // Ignore this address (list)
            startAddress = count = 0;
        }
    }

    // Erase a few matches
    foundAddressLists.erase(std::remove_if(foundAddressLists.begin(), foundAddressLists.end(), [fp](std::tuple<uint32_t, uint32_t, int>& addr)->bool {
        // Check for addresses that are too close together
        int numsize = std::min(std::get<1>(addr), 4u);
        uint32_t nums[4];
        fseek(fp, std::get<0>(addr), SEEK_SET);
        for (int i = 0; i < numsize; i++) fread(nums + i, 4, 1, fp);
        for (int i = 1; i < numsize; i++) if ((int32_t)nums[i] - (int32_t)nums[i-1] < 0x10) return true;
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
            if (version < 0x20040707) tmp2 = fgetc(fp);
            else fread(&tmp2, 2, 1, fp);
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
                if ((tmp > 256 || (i > 0 && abs((int32_t)tmp - (int32_t)last) > 16)) && i < 94) {possible_mask &= 0b011; break;}
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

    // Show brief of results
    if (retval.instrumentAddr) printf("> Found instrument list at address %08X\n", retval.instrumentAddr);
    if (retval.sampleAddr) printf("> Found sample list at address %08X\n", retval.sampleAddr);
    for (int i = 0; i < retval.modules.size(); i++) {
        retval.modules[i] = (retval.modules[i] & 0x1ffffff) - 364;
        printf("> Found module at address %08X\n", retval.modules[i]);
    }

    retval.success = retval.sampleAddr && !retval.modules.empty();
    return retval;
}

// Reads a Krawall sample from a ROM and writes it to a WAV file
void unkrawerter_readSampleToWAV(FILE* fp, uint32_t offset, const char * filename) {
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

// Taken from Krawall's mtypes.h file
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
        unsigned short  length;    // custom
        unsigned short  s3mlength; // custom
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

// Read a pattern from a file pointer to a Pattern structure pointer
static Pattern * readPatternFile(FILE* fp, uint32_t offset, bool use2003format) {
    fseek(fp, offset + 32, SEEK_SET);
    std::vector<uint8_t> fileContents;
    unsigned short rows = 0;
    unsigned short s3mlength = 0;
    if (use2003format) rows = fgetc(fp);
    else fread(&rows, 2, 1, fp);
    // We don't need to do full decoding; decode just enough to understand the size of the pattern
    for (int row = 0; row < rows; row++) {
        for (;;) {
            unsigned char follow = fgetc(fp);
            s3mlength++;
            fileContents.push_back(follow);
            if (!follow) break;
            if (follow & 0x20) {
                unsigned char note = fgetc(fp);
                fileContents.push_back(note);
                fileContents.push_back(fgetc(fp));
                s3mlength += 2;
                if (!use2003format && (note & 0x80)) fileContents.push_back(fgetc(fp));
            }
            if (follow & 0x40) {
                fileContents.push_back(fgetc(fp));
                s3mlength++;
            }
            if (follow & 0x80) {
                fileContents.push_back(fgetc(fp));
                fileContents.push_back(fgetc(fp));
                s3mlength += 2;
            }
        }
    }
    fseek(fp, offset, SEEK_SET);
    Pattern * retval = (Pattern*)malloc(38 + fileContents.size());
    retval->s3mlength = s3mlength;
    retval->length = fileContents.size();
    fread(retval->index, 2, 16, fp);
    fseek(fp, 2, SEEK_CUR);
    retval->rows = rows;
    memcpy(retval->data, &fileContents[0], fileContents.size());
    return retval;
}

// Read a module from a file pointer to a Module structure pointer
// This reads all its patterns as well
static Module * readModuleFile(FILE* fp, uint32_t offset) {
    Module * retval = (Module*)malloc(sizeof(Module));
    memset(retval, 0, sizeof(Module));
    fseek(fp, offset, SEEK_SET);
    fread(retval, 364, 1, fp);
    int markerAdd = 0;
    for (int i = 0; i < retval->numOrders; i++) {
        retval->order[i] = retval->order[i+markerAdd];
        while (retval->order[i] == 254) {markerAdd++; retval->order[i] = retval->order[i+markerAdd];}
    }
    retval->numOrders -= markerAdd;
    unsigned char maxPattern = 0;
    for (int i = 0; i < retval->numOrders; i++) if (retval->order[i] != 254) maxPattern = std::max(maxPattern, retval->order[i]);
    Module * retval2 = (Module*)malloc(sizeof(Module) + sizeof(Pattern*) * (maxPattern + 1));
    memcpy(retval2, retval, sizeof(Module));
    uint32_t addr = 0;
    for (int i = 0; i <= maxPattern; i++) {
        fseek(fp, offset + 364 + i*4, SEEK_SET);
        fread(&addr, 4, 1, fp);
        if (!(addr & 0x08000000) || (addr & 0xf6000000)) break;
        retval2->patterns[i] = readPatternFile(fp, addr & 0x1ffffff, version < 0x20040707);
    }
    return retval2;
}

// Read an instrument from a file pointer to an Instrument structure
static Instrument readInstrumentFile(FILE* fp, uint32_t offset) {
    fseek(fp, offset, SEEK_SET);
    Instrument retval;
    fread(&retval, sizeof(retval), 1, fp);
    return retval;
}

// Read a sample from a file pointer to a Sample structure pointer
static Sample * readSampleFile(FILE* fp, uint32_t offset) {
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

// Stores note data while converting
typedef struct {
    unsigned char xmflag;
    unsigned char note, volume, effect, effectop;
    unsigned short instrument;
} Note;

// Quick function to repeatedly put a character
inline void fputcn(int c, int num, FILE* fp) {for (; num > 0; num--) fputc(c, fp);}

// Effect map to convert Krawall effects to XM effects
// (effect . effectop) = first | (effectop & second)
// If first == 0xFFFF: ignore
// Some effects must be converted from S3M syntax to XM syntax.
// Some effects are only supported in S3M files, and are not converted.
// Some effects are only supported in MPT/OpenMPT, and may not play properly on other trackers.
const std::pair<unsigned short, unsigned char> effectMap_xm[] = {
    {0xFFFF, 0xFF}, 
    {0x0F00, 0xFF},       // EFF_SPEED
    {0x0F00, 0xFF},       // EFF_BPM
    {0x0F00, 0xFF},       // EFF_SPEEDBPM
    {0x0B00, 0xFF},       // EFF_PATTERN_JUMP
    {0x0D00, 0xFF},       // EFF_PATTERN_BREAK				5
    {0x0A00, 0xFF},       // EFF_VOLSLIDE_S3M               (S3M!)
    {0x0A00, 0xFF},       // EFF_VOLSLIDE_XM
    {0x0EB0, 0x0F},       // EFF_VOLSLIDE_DOWN_XM_FINE
    {0x0EA0, 0x0F},       // EFF_VOLSLIDE_UP_XM_FINE
    {0x0200, 0xFF},       // EFF_PORTA_DOWN_XM				10
    {0x0200, 0xFF},       // EFF_PORTA_DOWN_S3M             (S3M!)
    {0x0E20, 0x0F},       // EFF_PORTA_DOWN_XM_FINE
    {0x2120, 0x0F},       // EFF_PORTA_DOWN_XM_EFINE
    {0x0100, 0xFF},       // EFF_PORTA_UP_XM
    {0x0100, 0xFF},       // EFF_PORTA_UP_S3M				15 (S3M!)
    {0x0E10, 0x0F},       // EFF_PORTA_UP_XM_FINE
    {0x2110, 0x0F},       // EFF_PORTA_UP_XM_EFINE
    {0x0C00, 0xFF},       // EFF_VOLUME
    {0x0300, 0xFF},       // EFF_PORTA_NOTE
    {0x0400, 0xFF},       // EFF_VIBRATO					20
    {0x1D00, 0xFF},       // EFF_TREMOR
    {0x0000, 0xFF},       // EFF_ARPEGGIO
    {0x0600, 0xFF},       // EFF_VOLSLIDE_VIBRATO
    {0x0500, 0xFF},       // EFF_VOLSLIDE_PORTA
    {0xFFFF, 0xFF},       // EFF_CHANNEL_VOL				25 (S3M!)
    {0xFFFF, 0xFF},       // EFF_CHANNEL_VOLSLIDE           (S3M!)
    {0x0900, 0xFF},       // EFF_OFFSET
    {0x1900, 0xFF},       // EFF_PANSLIDE
    {0x1B00, 0xFF},       // EFF_RETRIG
    {0x0700, 0xFF},       // EFF_TREMOLO					30
    {0xFFFF, 0xFF},       // EFF_FVIBRATO                   (S3M!)
    {0x1000, 0xFF},       // EFF_GLOBAL_VOL
    {0x1100, 0xFF},       // EFF_GLOBAL_VOLSLIDE
    {0x0800, 0xFF},       // EFF_PAN
    {0x2200, 0xFF},       // EFF_PANBRELLO					35 (MPT!)
    {0xFFFF, 0xFF},       // EFF_MARK
    {0x0E30, 0x0F},       // EFF_GLISSANDO
    {0x0E40, 0x0F},       // EFF_WAVE_VIBR
    {0x0E70, 0x0F},       // EFF_WAVE_TREMOLO
    {0x2150, 0x0F},       // EFF_WAVE_PANBRELLO				40 (MPT!)
    {0x2160, 0x0F},       // EFF_PATTERN_DELAYF			    (!)
    {0x0E80, 0x0F},       // EFF_OLD_PAN					(!) converted to EFF_PAN
    {0x0E60, 0x0F},       // EFF_PATTERN_LOOP
    {0x0EC0, 0x0F},       // EFF_NOTE_CUT
    {0x0ED0, 0x0F},       // EFF_NOTE_DELAY					45
    {0x0EE0, 0x0F},       // EFF_PATTERN_DELAY			    (*)
    {0x1500, 0xFF},       // EFF_ENV_SETPOS
    {0xFFFF, 0xFF},       // EFF_OFFSET_HIGH
    {0x0600, 0xFF},       // EFF_VOLSLIDE_VIBRATO_XM
    {0x0500, 0xFF}        // EFF_VOLSLIDE_PORTA_XM			50
};

// Same for S3M effects
const std::pair<unsigned short, unsigned char> effectMap_s3m[] = {
    {0xFF00, 0x00}, 
    {0x0100, 0xFF},       //  A: EFF_SPEED
    {0x1400, 0xFF},       //  T: EFF_BPM
    {0xFF00, 0xFF},       //  -: EFF_SPEEDBPM
    {0x0200, 0xFF},       //  B: EFF_PATTERN_JUMP
    {0x0300, 0xFF},       //  C: EFF_PATTERN_BREAK				5
    {0x0400, 0xFF},       //  D: EFF_VOLSLIDE_S3M               
    {0x0400, 0xFF},       //  D: EFF_VOLSLIDE_XM
    {0x04F0, 0x0F},       //  D: EFF_VOLSLIDE_DOWN_XM_FINE
    {0x040F, 0xF0},       //  D: EFF_VOLSLIDE_UP_XM_FINE
    {0x0500, 0xFF},       //  E: EFF_PORTA_DOWN_XM				10
    {0x0500, 0xFF},       //  E: EFF_PORTA_DOWN_S3M             
    {0x05F0, 0x0F},       //  E: EFF_PORTA_DOWN_XM_FINE
    {0x05E0, 0x0F},       //  E: EFF_PORTA_DOWN_XM_EFINE
    {0x0600, 0xFF},       //  F: EFF_PORTA_UP_XM
    {0x0600, 0xFF},       //  F: EFF_PORTA_UP_S3M				15
    {0x06F0, 0x0F},       //  F: EFF_PORTA_UP_XM_FINE
    {0x06E0, 0x0F},       //  F: EFF_PORTA_UP_XM_EFINE
    {0xFF00, 0x00},       //  -: EFF_VOLUME                     (XM!)
    {0x0700, 0xFF},       //  G: EFF_PORTA_NOTE
    {0x0800, 0xFF},       //  H: EFF_VIBRATO					20
    {0x0900, 0xFF},       //  I: EFF_TREMOR
    {0x0A00, 0xFF},       //  J: EFF_ARPEGGIO
    {0x0B00, 0xFF},       //  K: EFF_VOLSLIDE_VIBRATO
    {0x0C00, 0xFF},       //  L: EFF_VOLSLIDE_PORTA
    {0x0D00, 0xFF},       //  M: EFF_CHANNEL_VOL				25
    {0x0E00, 0xFF},       //  N: EFF_CHANNEL_VOLSLIDE           
    {0x0F00, 0xFF},       //  O: EFF_OFFSET
    {0x1000, 0xFF},       //  P: EFF_PANSLIDE
    {0x1100, 0xFF},       //  Q: EFF_RETRIG
    {0x1200, 0xFF},       //  R: EFF_TREMOLO					30
    {0x1500, 0xFF},       //  U: EFF_FVIBRATO                   
    {0x1600, 0xFF},       //  V: EFF_GLOBAL_VOL
    {0x1700, 0xFF},       //  W: EFF_GLOBAL_VOLSLIDE
    {0x1800, 0xFF},       //  X: EFF_PAN
    {0x1900, 0xFF},       //  Y: EFF_PANBRELLO					35 (MPT!)
    {0xFF00, 0x00},       //  -: EFF_MARK                       (KRW!)
    {0x1310, 0x0F},       //  S: EFF_GLISSANDO
    {0x1330, 0x0F},       //  S: EFF_WAVE_VIBR
    {0x1340, 0x0F},       //  S: EFF_WAVE_TREMOLO
    {0x1350, 0x0F},       //  S: EFF_WAVE_PANBRELLO				40 (MPT!)
    {0x1360, 0x0F},       //  S: EFF_PATTERN_DELAYF			    (!)
    {0x1380, 0x0F},       //  S: EFF_OLD_PAN					(!) converted to EFF_PAN
    {0x13B0, 0x0F},       //  S: EFF_PATTERN_LOOP
    {0x13C0, 0x0F},       //  S: EFF_NOTE_CUT
    {0x13D0, 0x0F},       //  S: EFF_NOTE_DELAY					45
    {0x13E0, 0x0F},       //  S: EFF_PATTERN_DELAY			    (*)
    {0xFF00, 0x00},       //  -: EFF_ENV_SETPOS                 (XM!)
    {0x13A0, 0xFF},       //  S: EFF_OFFSET_HIGH
    {0x0B00, 0xFF},       //  K: EFF_VOLSLIDE_VIBRATO_XM
    {0x0C00, 0xFF}        //  L: EFF_VOLSLIDE_PORTA_XM			50
};

// Structure to hold a few per-channel memory things
struct channel_memory {
    unsigned char s3m;
    unsigned char pan;
    int porta;
    unsigned short instrument;
};

// Writes a module from a file pointer to a new XM file.
// XM file format from http://web.archive.org/web/20060809013752/http://pipin.tmd.ns.ac.yu/extra/fileformat/modules/xm/xm.txt
int unkrawerter_writeModuleToXM(FILE* fp, uint32_t moduleOffset, const std::vector<uint32_t> &sampleOffsets, const std::vector<uint32_t> &instrumentOffsets, const char * filename, bool trimInstruments = true, const char * name = NULL, bool fixCompatibility = true) {
    // Die if there are too many instruments for XM & we're not trimming instruments
    if (instrumentOffsets.size() > 255 && !trimInstruments) {
        fprintf(stderr, "Error: This ROM cannot be ripped without trimming instruments.\n");
        return 10;
    }
    // Open the XM file
    FILE* out = fopen(filename, "wb");
    if (out == NULL) {
        fprintf(stderr, "Could not open output file %s for writing.\n", filename);
        return 2;
    }
    // Read the module from the file
    Module * mod = readModuleFile(fp, moduleOffset);
    unsigned char patternCount = 0;
    for (int i = 0; i < mod->numOrders; i++) patternCount = std::max(patternCount, mod->order[i]);
    patternCount++;
    if (mod->flagInstrumentBased && instrumentOffsets.empty()) {
        fprintf(stderr, "Could not find all of the offsets required.\n * Does the ROM use the Krawall engine?\n * Try adjusting the search threshold.\n * You may need to find offsets yourself.\n");
        for (int i = 0; i < patternCount; i++) free((void*)mod->patterns[i]);
        free(mod);
        fclose(out);
        return 3;
    }
    // Write the XM header info
    if (name == NULL) fwrite("Extended Module: Krawall conversion  \032UnkrawerterGBA      \x04\x01\x14\x01\0\0", 1, 64, out);
    else {
        fwrite("Extended Module: ", 1, 17, out);
        fwrite(name, 1, std::min(strlen(name), (size_t)20), out);
        for (int i = std::min(strlen(name), (size_t)20); i < 20; i++) fputc(' ', out);
        fwrite("\032UnkrawerterGBA      \x04\x01\x14\x01\0\0", 1, 27, out);
    }
    fputc(mod->numOrders, out);
    fputc(0, out); // 1-byte padding
    fputc(mod->songRestart, out);
    fputc(0, out); // 1-byte padding
    fputc(mod->channels, out);
    fputc(0, out); // 2-byte padding
    unsigned short pnum = patternCount;
    fwrite(&pnum, 2, 1, out);
    uint32_t instrumentSizePos = ftell(out); // we'll get back to this later
    if (trimInstruments) fputcn(0, 2, out);
    else {pnum = mod->flagInstrumentBased ? instrumentOffsets.size() : sampleOffsets.size(); fwrite(&pnum, 2, 1, out);}
    fputc((mod->flagLinearSlides ? 1 : 0), out);
    fputc(0, out); // 2-byte padding
    fputc(mod->initSpeed, out);
    fputc(0, out); // 2-byte padding
    fputc(mod->initBPM, out);
    fputc(0, out); // 2-byte padding
    fwrite(mod->order, 1, 256, out);
    std::vector<unsigned short> instrumentList; // used to hold the instruments used so we can remove unnecessary instruments
    std::map<unsigned short, std::vector<std::pair<unsigned char, unsigned long> > > sampleOffsetList; // used to hold on to sample offset effects that may need fixing
    // Write each pattern
    for (int i = 0; i < patternCount; i++) {
        // Write pattern header
        fputc(9, out);
        fputcn(0, 4, out); // 4-byte padding + packing type (always 0)
        fwrite(&mod->patterns[i]->rows, 2, 1, out);
        uint32_t sizePos = ftell(out); // Save the position so we can come back to write the size
        fputcn(0, 2, out); // placeholder, we'll come back to this
        // Convert the Krawall data into XM data
        const unsigned char * data = mod->patterns[i]->data;
        Note * thisrow = (Note*)calloc(mod->channels, sizeof(Note)); // stores the current row's notes
        unsigned char warnings = 0; // for S3M/MPT warnings, we only warn once per pattern
        struct channel_memory * memory = new struct channel_memory[mod->channels]; // to store memory for various patches
        for (int i = 0; i < mod->channels; i++) {
            memory[i].s3m = 0;
            memory[i].porta = 0;
            memory[i].pan = 0x80;
            memory[i].instrument = 0;
        }
        unsigned char speed = mod->initSpeed; // to help portamento
        for (int row = 0; row < mod->patterns[i]->rows; row++) {
            memset(thisrow, 0, sizeof(Note) * mod->channels); // Zero so we can check the values for 0 later
            for (;;) {
                // Read the channel/next byte types
                unsigned char follow = *data++;
                if (!follow) break; // If it's 0, the row's done
                unsigned char xmflag = 0x80; // Stores the next byte types in XM format
                int channel = follow & 0x1f;
                unsigned char note = 0, volume = 0, effect = 0, effectop = 0;
                unsigned short instrument = 0;
                if (follow & 0x20) { // Note & instrument follows
                    xmflag |= 0x03;
                    note = *data++;
                    instrument = *data++;
                    if (version < 0x20040707) { // For versions before 2004-07-07, note is high 7 bits & instrument is low 9 bits
                        instrument |= (note & 1) << 8;
                        note >>= 1;
                    } else if (note & 0x80) { // For versions starting with 2004-07-07, if the note > 128, the instrument field is 2 bytes long
                        instrument |= *data++ << 8;
                        note &= 0x7f;
                    }
                    if (note > 97 || note == 0) note = 97;
                }
                if (follow & 0x40) { // Volume follows
                    xmflag |= 0x04;
                    volume = *data++;
                }
                if (follow & 0x80) { // Effect follows
                    xmflag |= 0x18;
                    effect = *data++;
                    effectop = *data++;
                    // Convert the Krawall effect into an XM effect
                    unsigned short xmeffect = effectMap_xm[effect].first;
                    unsigned char effectmask = effectMap_xm[effect].second;
                    if (xmeffect == 0xFFFF) { // Ignored
                        xmflag &= ~0x18;
                        effect = 0;
                        effectop = 0;
                    } else if (effect == 6) { // S3M volume slide
                        if (effectop == 0 && memory[channel].s3m) effectop = memory[channel].s3m;
                        memory[channel].s3m = effectop;
                        if ((effectop & 0xF0) == 0xF0) { // fine decrease
                            effect = 0x0E;
                            effectop = 0xB0 | (effectop & 0x0F);
                        } else if ((effectop & 0x0F) == 0x0F && effectop != 0x0F) { // fine increase (note: 0x0F means normal slide)
                            effect = 0x0E;
                            effectop = 0xA0 | (effectop >> 4);
                        } else { // normal volume slide
                            effect = 0x0A;
                        }
                    } else if (effect == 11) { // S3M porta down
                        if (effectop == 0 && memory[channel].s3m) effectop = memory[channel].s3m;
                        memory[channel].s3m = effectop;
                        if ((effectop & 0xF0) == 0xF0) { // fine
                            effect = 0x0E;
                            effectop = 0x20 | (effectop & 0x0F);
                        } else if ((effectop & 0xF0) == 0xE0) { // extra fine
                            effect = 0x21;
                            effectop = 0x20 | (effectop & 0x0F);
                        } else { // normal
                            effect = 0x02;
                        }
                    } else if (effect == 15) { // S3M porta up
                        if (effectop == 0 && memory[channel].s3m) effectop = memory[channel].s3m;
                        memory[channel].s3m = effectop;
                        if ((effectop & 0xF0) == 0xF0) { // fine
                            effect = 0x0E;
                            effectop = 0x10 | (effectop & 0x0F);
                        } else if ((effectop & 0xF0) == 0xE0) { // extra fine
                            effect = 0x21;
                            effectop = 0x10 | (effectop & 0x0F);
                        } else { // normal
                            effect = 0x01;
                        }
                    } else if (effect == 23) { // S3M volume slide + vibrato
                        if (effectop == 0 && memory[channel].s3m) effectop = memory[channel].s3m;
                        memory[channel].s3m = effectop;
                        // XM doesn't have a fine Vol+Vib command, so put the volume slide command in the volume column & vibrato in the effects column
                        if ((effectop & 0xF0) == 0xF0) { // fine decrease
                            if (!(xmflag & 0x04)) {xmflag |= 0x04; volume = 0x80 | (effectop & 0x0F);}
                            effect = 0x04;
                            effectop = 0;
                        } else if ((effectop & 0x0F) == 0x0F) { // fine increase
                            if (!(xmflag & 0x04)) {xmflag |= 0x04; volume = 0x90 | (effectop >> 4);}
                            effect = 0x04;
                            effectop = 0;
                        } else { // normal volume slide + vibrato
                            effect = 0x06;
                        }
                    } else if (effect == 24) { // S3M volume slide + porta
                        if (effectop == 0 && memory[channel].s3m) effectop = memory[channel].s3m;
                        memory[channel].s3m = effectop;
                        // XM doesn't have a fine Vol+Porta command, so put the volume slide command in the volume column & portamento in the effects column
                        if ((effectop & 0xF0) == 0xF0) { // fine decrease
                            if (!(xmflag & 0x04)) {xmflag |= 0x04; volume = 0x80 | (effectop & 0x0F);}
                            effect = 0x03;
                            effectop = 0;
                        } else if ((effectop & 0x0F) == 0x0F) { // fine increase
                            if (!(xmflag & 0x04)) {xmflag |= 0x04; volume = 0x90 | (effectop >> 4);}
                            effect = 0x03;
                            effectop = 0;
                        } else { // normal volume slide + portamento
                            effect = 0x06;
                        }
                    } else if (effect == 25 || effect == 26 || effect == 31 || (effect == 1 && (effectop >= 0x20 || effectop == 0))) { // Unsupported S3M effects
                        if (!(warnings & 0x02) && !(effect == 1 && effectop == 0)) {warnings |= 0x02; fprintf(stderr, "Warning: Pattern %d uses an S3M effect that isn't compatible with XM. It will not play correctly.\n", i);}
                        xmflag &= ~0x18;
                        effect = 0;
                        effectop = 0;
                    } else { // Other effects
                        // Warn if MPT-only
                        if ((effect == 35 || effect == 40) && !(warnings & 0x01)) {warnings |= 0x01; fprintf(stderr, "Warning: Pattern %d uses an effect specific to OpenMPT. It may not play correctly in other trackers.\n", i);}
                        if (effect == 1 || effect == 3) speed = effectop;
                        if (effect == 29 && (effectop & 0xF0) == 0x00) effectop |= 0x80;
                        xmeffect = xmeffect | (effectop & effectmask);
                        effect = xmeffect >> 8;
                        effectop = xmeffect & 0xFF;
                    }
                }
                // If the channel is OOB then don't store it (prevents segfaults, but that shouldn't happen if the file's good)
                if (channel >= mod->channels) continue;
                if (fixCompatibility) {
                    // Krawall cuts off portamento below 0, while XM underflows below 0 and never stops, so we need to fix that
                    // To do that we need to keep track of the portamento value
                    // Since I kinda don't want to write an entire tracker system just for one effect, we're just keeping track of the main porta effects
                    if (!mod->flagAmigaLimits) {
                        if (note && note < 97) memory[channel].porta = note * 16; // If there's a new note, reset the porta
                        // Look for the new porta value according to the porta value
                        int d = 0;
                        if (effect == 0x02)
                            d = memory[channel].porta - effectop * speed;
                        else if (effect == 0x0E && (effectop & 0xF0) == 0x20)
                            d = memory[channel].porta - (effectop & 0x0F);
                        else if (effect == 0x21 && (effectop & 0xF0) == 0x20)
                            d = memory[channel].porta - ((effectop & 0x0F) >> 2);
                        else if (effect == 0x01)
                            d = memory[channel].porta + effectop * speed;
                        else if (effect == 0x0E && (effectop & 0xF0) == 0x10)
                            d = memory[channel].porta + (effectop & 0x0F);
                        else if (effect == 0x21 && (effectop & 0x0F) == 0x10)
                            d = memory[channel].porta + ((effectop & 0x0F) >> 2);
                        else d = 0xFFFF;
                        // If the new porta is below 0, cut off the note
                        if (d <= 0) {
                            if (memory[channel].porta > 0) {
                                // There's still a bit of porta left until 0, so handle that here
                                if (effect == 0x02)
                                    effectop = (effectop * speed - memory[channel].porta) / speed;
                                else if (effect == 0x0E && (effectop & 0xF0) == 0x20)
                                    effectop = (effectop & 0x0F) - memory[channel].porta;
                                else if (effect == 0x21 && (effectop & 0xF0) == 0x20)
                                    effectop = (((effectop & 0x0F) >> 2) - memory[channel].porta) << 2;
                            } else {
                                // Otherwise just queue a cutoff note and remove the effect
                                note = 97;
                                xmflag &= ~0x18;
                                xmflag |= 0x01;
                                effect = 0;
                                effectop = 0;
                            }
                        }
                        // Set the new porta value in the memory
                        // Skip if we're already below 0 to avoid accidental underflow
                        if (memory[channel].porta > 0 && d != 0xFFFF) memory[channel].porta = d;
                    }
                    // If we're not using instruments then make sure the panning doesn't get messed up
                    if (!mod->flagInstrumentBased) {
                        if (memory[channel].pan != 0x80 && !(effect == 0x08 || (effect == 0x0E && (effectop & 0xF0) == 0x80))) {
                            if ((xmflag & 0x1A) == 0x02) {
                                // Best result, no effect is set so we can squeeze in a pan effect here
                                xmflag |= 0x18;
                                effect = 0x08;
                                effectop = memory[channel].pan;
                            } else if (instrument && instrument == memory[channel].instrument) {
                                // The panning only gets set when the instrument changes, so if the instrument didn't change we can just omit it and keep the last pan
                                xmflag &= ~0x02;
                            } else if ((xmflag & 0x06) == 0x02) {
                                // Effect column is already used, but volume isn't, so use the volume column
                                // Unfortunately this reduces the resolution to 4 bits, so not as desirable
                                xmflag |= 0x04;
                                volume = 0xC0 | (memory[channel].pan >> 4);
                            } else {
                                // Otherwise, both volume and effect columns are in use so we can't fix the panning. Oh well.
                                if (!(warnings & 0x04)) {warnings |= 0x04; fprintf(stderr, "Warning: Pattern %d uses special panning effects not available in XM. It will not play correctly.\n", i);}
                            }
                        }
                        if (effect == 0x08) {effectop <<= 1; memory[channel].pan = effectop;}
                        else if (effect == 0x0E && (effectop & 0xF0) == 0x80) memory[channel].pan = effectop << 4;
                        if (instrument && note < 97) memory[channel].instrument = instrument;
                    }
                }
                // Store the note data in the row
                thisrow[channel].xmflag = xmflag;
                thisrow[channel].note = note;
                thisrow[channel].instrument = instrument;
                thisrow[channel].volume = volume;
                thisrow[channel].effect = effect;
                thisrow[channel].effectop = effectop;
            }
            // Since Krawall doesn't need to fill all channels and XM does, convert that out
            for (int j = 0; j < mod->channels; j++) {
                if (thisrow[j].xmflag) { // If this was set, the note should be added
                    fputc(thisrow[j].xmflag, out);
                    if (thisrow[j].xmflag & 0x01) fputc(thisrow[j].note, out);
                    if (thisrow[j].xmflag & 0x02) {
                        if (thisrow[j].instrument == 0) fputc(0, out);
                        else if (!trimInstruments) fputc(thisrow[j].instrument & 0x7F, out);
                        else {
                            // Convert the instrument number so we can reduce the number of instruments
                            // Check if the instrument number is already in the list
                            unsigned char myInstrument = 0;
                            for (unsigned char k = 0; k < instrumentList.size(); k++) if (instrumentList[k] == thisrow[j].instrument - 1) {
                                myInstrument = k + 1;
                                break;
                            }
                            // If the instrument wasn't already added to the list, then add it
                            if (myInstrument == 0) {
                                // Instruments are listed as 8-bit numbers, so die if there are too many instruments
                                if (instrumentList.size() >= 254) {
                                    fprintf(stderr, "Error: Too many instruments in current pattern, cannot continue.\n");
                                    free(thisrow);
                                    delete[] memory;
                                    for (int l = 0; l < patternCount; l++) free((void*)mod->patterns[l]);
                                    free(mod);
                                    fclose(out);
                                    return 3;
                                }
                                instrumentList.push_back(thisrow[j].instrument - 1);
                                myInstrument = instrumentList.size();
                            }
                            fputc(myInstrument, out);
                        }
                    }
                    if (thisrow[j].xmflag & 0x04) fputc(thisrow[j].volume, out);
                    if (thisrow[j].xmflag & 0x08) {
                        if (fixCompatibility && thisrow[j].effect == 0x09 && (thisrow[j].xmflag & 0x10))
                            sampleOffsetList[thisrow[j].instrument - 1].push_back(std::make_pair(thisrow[j].effectop, ftell(out)));
                        fputc(thisrow[j].effect, out);
                    }
                    if (thisrow[j].xmflag & 0x10) fputc(thisrow[j].effectop, out);
                } else fputc(0x80, out); // Empty note (do nothing this row)
            }
        }
        free(thisrow);
        delete[] memory;
        // Write the size of the packed pattern data
        uint32_t endPos = ftell(out);
        fseek(out, sizePos, SEEK_SET);
        unsigned short size = endPos - sizePos - 2;
        fwrite(&size, 2, 1, out);
        fseek(out, endPos, SEEK_SET);
    }
    // Write the total number of instruments used in the module
    if (trimInstruments) {
        uint32_t endPos = ftell(out);
        fseek(out, instrumentSizePos, SEEK_SET);
        pnum = instrumentList.size();
        fwrite(&pnum, 2, 1, out);
        fseek(out, endPos, SEEK_SET);
    } else if (mod->flagInstrumentBased) for (int i = 0; i < instrumentOffsets.size(); i++) instrumentList.push_back(i); // Add all instruments if not trimming & we're using instruments
    else for (int i = 0; i < sampleOffsets.size(); i++) instrumentList.push_back(i); // Add all samples if not trimming & not using instruments
    if (mod->flagInstrumentBased) {
        // Write all of the instruments used by the module
        for (unsigned short i : instrumentList) {
            // Read the instrument info
            Instrument instr = readInstrumentFile(fp, instrumentOffsets[i]);
            // Find all of the unique samples
            std::vector<unsigned short> samples;
            samples.resize(96);
            samples.erase(std::unique_copy(instr.samples, instr.samples + 96, samples.begin()), samples.end());
            unsigned short snum = samples.size();
            // Start writing instrument header
            fputc(snum == 0 ? 29 : 252, out);
            fputcn(0, 3, out); // 4-byte padding
            char name[22];
            memset(name, 0, 22);
            snprintf(name, 22, "Instrument%d", i);
            fwrite(name, 1, 22, out);
            fputc(0, out);
            fwrite(&snum, 2, 1, out);
            if (snum == 0) continue; // XM spec says if there's no samples then skip the rest
            // Convert arbitrary sample numbers in the sample map to 0, 1, 2, etc.
            // This is because Krawall has a global sample map, while XM counts samples per instrument
            std::map<unsigned short, unsigned char> sample_conversion;
            unsigned char new_samples[96];
            for (unsigned char i = 0; i < snum; i++) sample_conversion[samples[i]] = i;
            for (int i = 0; i < 96; i++) new_samples[i] = sample_conversion[instr.samples[i]];
            // Write instrument data
            fputc(40, out);
            fputcn(0, 3, out); // 4-byte padding
            fwrite(new_samples, 1, 96, out);
            // Convert envelopes to XM format
            // Turns out we don't even need the inc field! Everything's packed in coord.
            unsigned short tmp;
            for (int j = 0; j < 12; j++) {
                tmp = instr.envVol.nodes[j].coord & 0x1ff;
                fwrite(&tmp, 2, 1, out);
                tmp = instr.envVol.nodes[j].coord >> 9;
                fwrite(&tmp, 2, 1, out);
            }
            for (int j = 0; j < 12; j++) {
                tmp = instr.envPan.nodes[j].coord & 0x1ff;
                fwrite(&tmp, 2, 1, out);
                tmp = instr.envPan.nodes[j].coord >> 9;
                fwrite(&tmp, 2, 1, out);
            }
            // Here's a whole bunch of envelope parameters to write
            fputc(instr.envVol.max + 1, out);
            fputc(instr.envPan.max + 1, out);
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
            fputcn(0, 11, out); // Padding as required by XM
            // Write all of the samples required for this instrument
            // XM requires all of the headers to be written before the data, so we read
            // all of the samples in one loop and then write the data in another
            // Seems inefficient but it's impossible to avoid
            std::vector<Sample*> sarr;
            for (int j = 0; j < snum; j++) {
                if (samples[j] > sampleOffsets.size()) continue; // If the sample isn't present then skip it
                // Read the sample from the file
                Sample * s = readSampleFile(fp, sampleOffsets[samples[j]]);
                // Write the sample header
                fwrite(&s->size, 4, 1, out);
                // Loop start has to be computed from the end & length
                if (s->loopLength == 0) fputcn(0, 4, out);
                else {
                    uint32_t start = s->size - s->loopLength;
                    fwrite(&start, 4, 1, out);
                }
                // Some other sample parameters
                fwrite(&s->loopLength, 4, 1, out);
                fputc(s->volDefault, out);
                fputc(s->fineTune, out);
                fputc((s->loop ? 1 : 0), out);
                fputc(s->panDefault + 0x80, out);
                fputc(s->relativeNote, out);
                fputc(0, out);
                memset(name, ' ', 22);
                snprintf(name, 22, "Sample%d", samples[j]);
                fwrite(name, 1, 22, out);
                sarr.push_back(s); // Push the read sample back so we don't have to allocate & read it again
                // Update any offset effects that are too big for the instrument
                if (fixCompatibility && sampleOffsetList.find(i) != sampleOffsetList.end()) {
                    unsigned long retpos = ftell(out);
                    for (std::pair<unsigned char, unsigned long> eff : sampleOffsetList[i]) {
                        if (eff.first >= (s->size >> 8)) {
                            fseek(out, eff.second, SEEK_SET);
                            fputcn(0, 2, out);
                        }
                    }
                    fseek(out, retpos, SEEK_SET);
                }
            }
            // Write the actual sample data
            for (int j = 0; j < sarr.size(); j++) {
                Sample * s = sarr[j];
                // Everything's written as deltas instead of absolute values
                // We also convert from signed to unsigned here since it has to be unsigned
                unsigned char old = 0;
                for (uint32_t k = 0; k < s->size; k++) {
                    fputc(((int)s->data[k] + 0x80) - old, out);
                    old = (int)s->data[k] + 0x80;
                }
                free(s);
            }
        }
    } else {
        // Not using instruments, so one sample = one instrument
        for (unsigned short i : instrumentList) {
            // Basic Instrument header
            fputc(252, out);
            fputcn(0, 3, out); // 4-byte padding
            char name[22];
            memset(name, 0, 22);
            snprintf(name, 22, "Instrument%d", i);
            fwrite(name, 1, 22, out);
            fputc(0, out);
            fputc(1, out); // 1 sample
            fputc(0, out);
            fputc(40, out);
            fputcn(0, 3 + 96 + 96 + 16, out); // 4-byte padding + rest of instrument data (all 0)
            fputcn(0, 11, out); // Padding as required by XM
            Sample * s = readSampleFile(fp, sampleOffsets[i]);
            // Write the sample header
            fwrite(&s->size, 4, 1, out);
            // Loop start has to be computed from the end & length
            if (s->loopLength == 0) fputcn(0, 4, out);
            else {
                uint32_t start = s->size - s->loopLength;
                fwrite(&start, 4, 1, out);
            }
            // Some other sample parameters
            fwrite(&s->loopLength, 4, 1, out);
            fputc(s->volDefault, out);
            fputc(s->fineTune, out);
            fputc((s->loop ? 1 : 0), out);
            fputc(s->panDefault + 0x80, out);
            fputc(s->relativeNote, out);
            fputc(0, out);
            memset(name, ' ', 22);
            snprintf(name, 22, "Sample%d", i);
            fwrite(name, 1, 22, out);
            // Update any offset effects that are too big for the instrument
            if (fixCompatibility && sampleOffsetList.find(i) != sampleOffsetList.end()) {
                unsigned long retpos = ftell(out);
                for (std::pair<unsigned char, unsigned long> eff : sampleOffsetList[i]) {
                    if ((unsigned short)eff.first << 8 > s->size) {
                        fseek(out, eff.second, SEEK_SET);
                        fputcn(0, 2, out);
                    }
                }
                fseek(out, retpos, SEEK_SET);
            }
            // Everything's written as deltas instead of absolute values
            // We also convert from signed to unsigned here since it has to be unsigned
            unsigned char old = 0;
            for (uint32_t k = 0; k < s->size; k++) {
                fputc(((int)s->data[k] + 0x80) - old, out);
                old = (int)s->data[k] + 0x80;
            }
            free(s);
        }
    }
    // Free & close the patterns, module, & file
    for (int i = 0; i < patternCount; i++) free((void*)mod->patterns[i]);
    free(mod);
    fclose(out);
    printf("Successfully wrote module to %s.\n", filename);
    return 0;
}

// Writes a module from a file pointer to a new S3M file.
// S3M file format from http://web.archive.org/web/20060831105434/http://pipin.tmd.ns.ac.yu/extra/fileformat/modules/s3m/s3m.txt
int unkrawerter_writeModuleToS3M(FILE* fp, uint32_t moduleOffset, const std::vector<uint32_t> &sampleOffsets, const char * filename, bool trimInstruments = true, const char * name = NULL) {
    // Die if there are too many instruments for S3M & we're not trimming instruments
    if (sampleOffsets.size() > 255 && !trimInstruments) {
        fprintf(stderr, "Error: This ROM cannot be ripped without trimming instruments.\n");
        return 10;
    }
    // Open the S3M file
    FILE* out = fopen(filename, "wb");
    if (out == NULL) {
        fprintf(stderr, "Could not open output file %s for writing.\n", filename);
        return 2;
    }
    // Read the module from the ROM
    Module * mod = readModuleFile(fp, moduleOffset);
    // Count how many patterns there are
    unsigned char patternCount = 0;
    for (int i = 0; i < mod->numOrders; i++) patternCount = std::max(patternCount, mod->order[i]);
    patternCount++;
    // Check for some basic requirements before going further
    if (mod->flagInstrumentBased || mod->patterns[0]->rows != 64) {
        fprintf(stderr, "Error: This ROM does not support S3M output.\n");
        for (int i = 0; i < patternCount; i++) free((void*)mod->patterns[i]);
        free(mod);
        fclose(out);
        return 3;
    }
    // If we're trimming instruments, go through all of the patterns and see which instruments we need
    std::map<unsigned short, unsigned char> instrumentMap;
    if (trimInstruments) {
        unsigned char nextInstrument = 1;
        for (int i = 0; i < patternCount; i++) {
            const unsigned char * data = mod->patterns[i]->data;
            for (int row = 0; row < 64 && data < mod->patterns[i]->data + mod->patterns[i]->length; row++) {
                for (;;) {
                    // Read the channel/next byte types
                    unsigned char follow = *data++;
                    if (!follow) break; // If it's 0, the row's done
                    if (follow & 0x20) { // Note & instrument follows
                        unsigned char note = *data++;
                        unsigned short instrument = *data++;
                        if (version < 0x20040707) { // For versions before 2004-07-07, note is high 7 bits & instrument is low 9 bits
                            instrument |= (note & 1) << 8;
                            note >>= 1;
                        } else if (note & 0x80) { // For versions starting with 2004-07-07, if the note > 128, the instrument field is 2 bytes long
                            instrument |= *data++ << 8;
                            note &= 0x7f;
                        }
                        if (instrument != 0 && instrumentMap.find(instrument) == instrumentMap.end()) {
                            if (nextInstrument == 255) {
                                fprintf(stderr, "Error: Too many instruments in module, cannot continue.\n");
                                for (int l = 0; l < patternCount; l++) free((void*)mod->patterns[l]);
                                free(mod);
                                fclose(out);
                                return 3;
                            }
                            instrumentMap[instrument] = nextInstrument++;
                        }
                    }
                    if (follow & 0x40) data++;
                    if (follow & 0x80) data += 2;
                }
            }
        }
    }
    // Write the S3M header info
    if (name == NULL) fwrite("Krawall conversion\0\0\0\0\0\0\0\0\0\0", 28, 1, out);
    else {
        fwrite(name, 1, std::min(strlen(name), (size_t)28), out);
        if (strlen(name) < 28) fputcn(0, 28 - strlen(name), out);
    }
    fputc(0x1A, out);
    fputc(16, out); // Type (16=ST3 module)
    fputcn(0, 2, out); // padding
    fputc(mod->numOrders, out);
    fputc(0, out);
    fputc(trimInstruments ? instrumentMap.size() : sampleOffsets.size(), out);
    fputc(0, out);
    fputc(patternCount, out);
    fputc(0, out);
    fputc((mod->flagAmigaLimits ? 16 : 0) | (mod->flagVolOpt ? 8 : 0) | (mod->flagVolSlides ? 64 : 0), out);
    fputc(0, out);
    fputc(0x13, out); // Tracker version
    fputc(0x20, out); // ^^
    fputc(2, out); // Unsigned samples
    fputc(0, out);
    fwrite("SCRM", 4, 1, out);
    fputc(mod->volGlobal, out);
    fputc(mod->initSpeed, out);
    fputc(mod->initBPM, out);
    fputc(64, out); // Master volume (maximum)
    fputc(0, out); // Ultra click removal
    fputc(252, out); // Has channel pan positions
    fputcn(0, 10, out); // padding
    // Write the channel settings
    for (int i = 0; i < mod->channels / 2; i++) fputc(i, out);
    for (int i = 0; i < mod->channels / 2 + mod->channels % 2; i++) fputc(i | 8, out);
    fputcn(0xFF, 32 - mod->channels, out);
    // Write all of the orders
    fwrite(mod->order, 1, mod->numOrders, out);
    // Write parapointers
    int paddingBytes = 0;
    uint16_t tmp;
    // Write the parapointers to each instrument
    for (int i = 0; i < (trimInstruments ? instrumentMap.size() : sampleOffsets.size()); i++) {
        tmp = (0x60 + mod->numOrders + (trimInstruments ? instrumentMap.size() : sampleOffsets.size()) * 2 + patternCount * 2 + 32 + i * 0x50) + paddingBytes; // Header + orders + instrument parapointers + pattern parapointers + pan positions + previous instruments
        if (tmp & 0xF) {paddingBytes += 16 - (tmp & 0xF); tmp = (tmp & 0xFFF0) + 0x10;}
        tmp >>= 4;
        fwrite(&tmp, 2, 1, out);
    }
    int offset = 0;
    // Write the parapointers to each pattern
    for (int i = 0; i < patternCount; i++) {
        // S3M requires all patterns to be exactly 64 rows, so die if any pattern has <> 64 rows
        if (mod->patterns[i]->rows != 64) {
            fprintf(stderr, "Error: This ROM does not support S3M output. (If S3M was auto-detected, try using the -x switch instead.)\n");
            for (int i = 0; i < patternCount; i++) free((void*)mod->patterns[i]);
            free(mod);
            fclose(out);
            return 3;
        }
        tmp = 0x60 + mod->numOrders + (trimInstruments ? instrumentMap.size() : sampleOffsets.size()) * 0x52 + patternCount * 2 + 32 + offset + paddingBytes; // Header + orders + instrument parapointers + pattern parapointers + pan positions + instruments + previous patterns
        if (tmp & 0xF) {paddingBytes += 16 - (tmp & 0xF); tmp = (tmp & 0xFFF0) + 0x10;}
        tmp >>= 4;
        fwrite(&tmp, 2, 1, out);
        offset += mod->patterns[i]->s3mlength + 2;
    }
    // Write channel pan positions
    for (int i = 0; i < mod->channels; i++) {
        if (mod->channelPan[i] == 0) fputc(0x27, out);
        else fputc((mod->channelPan[i] >> 4) | 0x20, out);
    }
    fputcn(0x08, 32 - mod->channels, out);
    // Write each instrument header
    std::vector<Sample*> samples;
    for (int i = 0; i < (trimInstruments ? instrumentMap.size() : sampleOffsets.size()); i++) {
        // Get instrument number to write
        unsigned short inst = 0;
        if (trimInstruments) {
            for (auto p : instrumentMap) {
                if (p.second == i + 1) {
                    inst = p.first - 1;
                    break;
                }
            }
        } else inst = i;
        // Pad to 16 bytes
        while (ftell(out) & 0xF) fputc(0, out);
        fputc(1, out); // Type (1=Sample)
        fputcn(0, 12, out); // DOS filename
        uint32_t memseg = 0x60 + mod->numOrders + (trimInstruments ? instrumentMap.size() : sampleOffsets.size()) * 0x52 + patternCount * 2 + 32 + offset + paddingBytes; // Header + orders + instrument parapointers + pattern parapointers + pan positions + instruments + patterns + previous samples
        if (memseg & 0xF) {paddingBytes += 16 - (memseg & 0xF); memseg = (memseg & 0xFFFFF0) + 0x10;}
        memseg >>= 4;
        fputc((memseg >> 16) & 0xFF, out); // Sample parapointer high byte
        fputc(memseg & 0xFF, out); // Sample parapointer low two bytes (LE)
        fputc((memseg >> 8) & 0xFF, out);
        Sample * s = readSampleFile(fp, sampleOffsets[inst]);
        fwrite(&s->size, 4, 1, out);
        memseg = s->size - s->loopLength;
        fwrite(&memseg, 4, 1, out); // Loop beginning
        memseg = s->size + 1;
        fwrite(&memseg, 4, 1, out); // Loop end
        fputc(s->volDefault, out);
        fputcn(0, 2, out); // Padding, packing type (0)
        fputc((s->loop ? 1 : 0), out); // Flags
        fwrite(&s->c2Freq, 4, 1, out);
        fputcn(0, 12, out); // Padding/unused
        // Write sample name
        char name[28];
        memset(name, 0, 28);
        snprintf(name, 28, "Sample%d", inst);
        fwrite(name, 1, 28, out);
        fwrite("SCRS", 4, 1, out);
        offset += s->size;
        samples.push_back(s);
    }
    // Write each pattern
    // Krawall pattern data is nearly identical to S3M packed pattern data, so not much conversion is needed
    // We only really need to fix the note/instrument packing, volume column format, and effects
    for (int i = 0; i < patternCount; i++) {
        // Pad to 16 bytes
        while (ftell(out) & 0xF) fputc(0, out);
        // Write the pattern length (it'll be the same length as the Krawall data)
        fwrite(&mod->patterns[i]->s3mlength, 2, 1, out);
        const unsigned char * data = mod->patterns[i]->data;
        int warnings = 0;
        // Loop through each row of the pattern
        for (int row = 0; row < 64 && data < mod->patterns[i]->data + mod->patterns[i]->length; row++) {
            for (;;) {
                // Read the channel/next byte types
                unsigned char follow = *data++;
                fputc(follow, out);
                if (!follow) break; // If it's 0, the row's done
                if (follow & 0x20) { // Note & instrument follows
                    unsigned char note = *data++;
                    unsigned short instrument = *data++;
                    if (version < 0x20040707) { // For versions before 2004-07-07, note is high 7 bits & instrument is low 9 bits
                        instrument |= (note & 1) << 8;
                        note >>= 1;
                    } else if (note & 0x80) { // For versions starting with 2004-07-07, if the note > 128, the instrument field is 2 bytes long
                        instrument |= *data++ << 8;
                        note &= 0x7f;
                    }
                    if (note >= 97 || note == 0) fputc(254, out); // 254 = note off
                    else fputc((((note - 1) / 12) << 4) | ((note - 1) % 12), out); // S3M wants hi=oct, lo=note
                    fputc(trimInstruments ? (instrument == 0 ? 0 : instrumentMap[instrument]) : instrument, out); // Write instrument
                }
                if (follow & 0x40) { // Volume follows
                    // XM/Krawall stores volume from 0x10-0x50, while S3M expects it at 0x00-0x40, so subtract to fix
                    unsigned char volume = *data++;
                    if (volume < 0x10) fputc(0xFF, out); // < 0x10 = nothing
                    else if (volume <= 0x50) fputc(volume - 0x10, out); // 0x10 - 0x50 = volume
                    else if (volume >= 0xC0 && volume < 0xD0) fputc((volume - 0x40) << 2, out); // 0xC0 - 0xCF = panning (MPT only)
                    else {
                        if (!(warnings & 0x01)) {warnings |= 0x01; fprintf(stderr, "Warning: Pattern %d uses special volume column effects not available in S3M. It will not play correctly.\n", i);}
                        fputc(0xFF, out);
                    }
                }
                if (follow & 0x80) { // Effect follows
                    unsigned char effect = *data++;
                    unsigned char effectop = *data++;
                    if (effect == 3) { // Speed/BPM
                        if (effectop >= 0x20) effect = 0x1D;
                        else effect = 0x0A;
                    } else { // Other effects
                        // Convert the Krawall effect into an S3M effect
                        unsigned short s3meffect = effectMap_s3m[effect].first;
                        unsigned char effectmask = effectMap_s3m[effect].second;
                        if (effect == 9) effectop <<= 4; // Volume slide up needs to shift the op up by 4 bits
                        s3meffect = s3meffect | (effectop & effectmask);
                        effect = s3meffect >> 8;
                        effectop = s3meffect & 0xFF;
                    }
                    // Write the final effect
                    fputc(effect, out);
                    fputc(effectop, out);
                }
            }
        }
    }
    // Write sample data
    for (int i = 0; i < samples.size(); i++) {
        while (ftell(out) & 0xF) fputc(0, out);
        Sample * s = samples[i];
        fwrite(s->data, 1, s->size, out);
        free(s);
    }
    // Free & close the patterns, module, & file
    for (int i = 0; i < patternCount; i++) free((void*)mod->patterns[i]);
    free(mod);
    fclose(out);
    printf("Successfully wrote module to %s.\n", filename);
    return 0;
}

#ifndef AS_LIBRARY

// Looks for a string in a file
static bool fstr(FILE* fp, const char * str) {
    rewind(fp);
    const char * ptr = str;
    while (!feof(fp)) {
        if (!*ptr) return true;
        else if (fgetc(fp) == *ptr) ptr++;
        else ptr = str;
    }
    return false;
}

int main(int argc, const char * argv[]) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0) {
        // Help
        fprintf(stderr, "Usage: %s [options...] <rom.gba>\n"
                        "Options:\n"
                        "  -i <address>      Override instrument list address\n"
                        "  -l <file.txt>     Read module names from a file (one name/line, same format as -n)\n"
                        "  -m <address>      Add an extra module address to the list\n"
                        "  -n <addr>=<name>  Assign a name to a module address (max. 20 characters for XM, 28 for S3M)\n"
                        "  -o <directory>    Output directory\n"
                        "  -s <address>      Override sample list address\n"
                        "  -t <threshold>    Search threshold, lower = slower but finds smaller modules,\n"
                        "                      higher = faster but misses smaller modules (defaults to 4)\n"
                        "  -3                Force extraction to output S3M modules (only supported with some modules)\n"
                        "  -a                Do not trim extra instruments; this will make modules much larger in size!\n"
                        "  -c                Disable compatibility fixes, makes patterns more accurate but worsens playback\n"
                        "  -e                Export samples to WAV files\n"
                        "  -v                Enable verbose mode\n"
                        "  -x                Force extraction to output XM modules\n"
                        "  -h                Show this help\n", argv[0]);
        return 1;
    }
    // Command-line argument parsing
    std::string outputDir;
    int searchThreshold = 4;
    bool verbose = false;
    bool trimInstruments = true;
    bool exportSamples = false;
    bool fixCompatibility = true;
    int moduleType = -1;
    std::string romPath;
    uint32_t sampleAddr = 0, instrumentAddr = 0;
    std::vector<uint32_t> additionalModules;
    std::map<uint32_t, std::string> nameMap;
    int nextArg = 0;
    // Loop through all arguments
    for (int i = 1; i < argc; i++) {
        if (nextArg) {
            switch (nextArg) {
                case 1: instrumentAddr = atoi(argv[i]); break;
                case 2: additionalModules.push_back(atoi(argv[i])); break;
                case 3: outputDir = std::string(argv[i]) + "/"; break;
                case 4: sampleAddr = atoi(argv[i]); break;
                case 5: searchThreshold = atoi(argv[i]); break;
                case 6: {
                    std::string arg(argv[i]);
                    size_t pos = arg.find('=');
                    if (pos == std::string::npos) {
                        fprintf(stderr, "Error: Invalid argument to -n\n");
                        return 7;
                    }
                    std::string name = arg.substr(pos + 1);
                    if (name.size() > 20) name.erase(20);
                    nameMap[std::stoul(arg.substr(0, pos), nullptr, 16) & 0x1ffffff] = name;
                    break;
                }
                case 7: {
                    FILE* fp = fopen(argv[i], "r");
                    if (fp == NULL) {
                        fprintf(stderr, "Error: Invalid argument to -l\n");
                        return 8;
                    }
                    std::string tmpaddr, tmpname;
                    while (!feof(fp)) {
                        bool a = false;
                        tmpaddr.clear();
                        tmpname.clear();
                        for (char c = fgetc(fp); c != '\n' && c != EOF; c = fgetc(fp)) {
                            if (!a && c == '=') a = true;
                            else if (c >= 0x20) {
                                if (a) tmpname += c;
                                else tmpaddr += c;
                            }
                        }
                        if (a && !tmpaddr.empty() && !tmpname.empty()) {
                            if (tmpname.size() > 20) tmpname.erase(20);
                            nameMap[std::stoul(tmpaddr, nullptr, 16)] = tmpname;
                        }
                    }
                    fclose(fp);
                    break;
                }
            }
            nextArg = 0;
        } else if (argv[i][0] == '-') {
            for (int j = 1; j < strlen(argv[i]); j++) {
                switch (argv[i][j]) {
                    case '3': moduleType = 1; break;
                    case 'a': trimInstruments = false; break;
                    case 'c': fixCompatibility = false; break;
                    case 'e': exportSamples = true; break;
                    case 'i': nextArg = 1; break;
                    case 'k': version = 0x20030901; break;
                    case 'l': nextArg = 7; break;
                    case 'm': nextArg = 2; break;
                    case 'n': nextArg = 6; break;
                    case 'o': nextArg = 3; break;
                    case 's': nextArg = 4; break;
                    case 't': nextArg = 5; break;
                    case 'v': verbose = true; break;
                    case 'x': moduleType = 0; break;
                }
            }
        } else if (romPath.empty()) romPath = argv[i];
    }
    // Die if no ROM file was specified
    if (romPath.empty()) {
        fprintf(stderr, "Error: No ROM file specified.\n");
        return 4;
    }
    // Open the ROM file
    FILE* fp = fopen(romPath.c_str(), "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open file %s for reading.", romPath.c_str());
        return 2;
    }
    // Look for a Krawall signature & version in the file and warn if one isn't found
    if (!fstr(fp, "$Id: Krawall")) fprintf(stderr, "Warning: Could not find Krawall signature. Are you sure this game uses the Krawall engine?\n");
    else if (fstr(fp, "$Date: ")) {
        // $Date: 2000/01/01
        char tmp[11];
        fread(tmp, 10, 1, fp);
        tmp[10] = 0;
        version = ((tmp[0] - '0') << 28) | ((tmp[1] - '0') << 24) | ((tmp[2] - '0') << 20) | ((tmp[3] - '0') << 16) | ((tmp[5] - '0') << 12) | ((tmp[6] - '0') << 8) | ((tmp[8] - '0') << 4) | (tmp[9] - '0');
        printf("Krawall version: %08x\n", version);
    } else {
        rewind(fp);
        if (fstr(fp, "$Id: version.h 8 ")) {
            // $Id: version.h 8 2001-01-01
            char tmp[11];
            fread(tmp, 10, 1, fp);
            tmp[10] = 0;
            version = ((tmp[0] - '0') << 28) | ((tmp[1] - '0') << 24) | ((tmp[2] - '0') << 20) | ((tmp[3] - '0') << 16) | ((tmp[5] - '0') << 12) | ((tmp[6] - '0') << 8) | ((tmp[8] - '0') << 4) | (tmp[9] - '0');
            printf("Krawall version: %08x\n", version);
        }
    }
    rewind(fp);
    // Search for the offsets
    OffsetSearchResult offsets;
    offsets = unkrawerter_searchForOffsets(fp, searchThreshold, verbose);
    // Add in overrides if provided
    if (sampleAddr) {
        offsets.sampleAddr = sampleAddr & 0x1ffffff;
        uint32_t tmp = 0;
        fseek(fp, offsets.sampleAddr, SEEK_SET);
        fread(&tmp, 4, 1, fp);
        for (offsets.sampleCount = 0; (tmp & 0xf6000000) == 0 && (tmp & 0x8000000) == 0x8000000; offsets.sampleCount++) fread(&tmp, 4, 1, fp);
        rewind(fp);
    }
    if (instrumentAddr) {
        offsets.instrumentAddr = instrumentAddr & 0x1ffffff;
        uint32_t tmp = 0;
        fseek(fp, offsets.instrumentAddr, SEEK_SET);
        fread(&tmp, 4, 1, fp);
        for (offsets.instrumentCount = 0; (tmp & 0xf6000000) == 0 && (tmp & 0x8000000) == 0x8000000; offsets.instrumentCount++) fread(&tmp, 4, 1, fp);
        rewind(fp);
    }
    for (uint32_t a : additionalModules) offsets.modules.push_back(a);
    offsets.success = offsets.sampleAddr && !offsets.modules.empty();
    // If we don't have all of the required offsets, we can't continue
    if (!offsets.success) {
        fprintf(stderr, "Could not find all of the offsets required.\n * Does the ROM use the Krawall engine?\n * Try adjusting the search threshold.\n * You may need to find offsets yourself.\n");
        return 3;
    }
    // Read each of the offsets from the lists in the file into vectors
    std::vector<uint32_t> sampleOffsets, instrumentOffsets;
    uint32_t tmp = 0;
    fseek(fp, offsets.sampleAddr, SEEK_SET);
    for (int i = 0; i < offsets.sampleCount; i++) {
        fread(&tmp, 4, 1, fp);
        sampleOffsets.push_back(tmp & 0x1ffffff);
    }
    if (offsets.instrumentAddr) {
        fseek(fp, offsets.instrumentAddr, SEEK_SET);
        for (int i = 0; i < offsets.instrumentCount; i++) {
            fread(&tmp, 4, 1, fp);
            instrumentOffsets.push_back(tmp & 0x1ffffff);
        }
    }
    // Export all WAV samples (if desired)
    if (exportSamples) {
        for (int i = 0; i < sampleOffsets.size(); i++) {
            std::string name = outputDir + "Sample" + std::to_string(i) + ".wav";
            unkrawerter_readSampleToWAV(fp, sampleOffsets[i], name.c_str());
            printf("Wrote sample %d to %s\n", i, name.c_str());
        }
    }
    // Write out all of the new modules
    for (int i = 0; i < offsets.modules.size(); i++) {
        // Detect whether to use S3M or XM module format
        fseek(fp, offsets.modules[i] + 358, SEEK_SET);
        bool useS3M = (!fgetc(fp) && moduleType != 0) || moduleType == 1; // Check the instrumentBased flag
        if (useS3M && moduleType != 1) {
            // Also check that the first module (at least) has exactly 64 rows
            uint32_t tmp = 0;
            uint16_t tmp16 = 0;
            fseek(fp, 5, SEEK_CUR);
            fread(&tmp, 4, 1, fp);
            fseek(fp, (tmp & 0x1ffffff) + 32, SEEK_SET);
            if (version < 0x20040707) tmp16 = fgetc(fp);
            else fread(&tmp16, 2, 1, fp);
            useS3M = tmp16 == 64;
        }
        std::string name = outputDir + (nameMap.find(offsets.modules[i]) != nameMap.end() ? nameMap[offsets.modules[i]] : "Module" + std::to_string(i)) + (useS3M ? ".s3m" : ".xm");
        int r;
        if (useS3M) r = unkrawerter_writeModuleToS3M(fp, offsets.modules[i], sampleOffsets, name.c_str(), trimInstruments, (nameMap.find(offsets.modules[i]) != nameMap.end() ? nameMap[offsets.modules[i]].c_str() : NULL));
        else r = unkrawerter_writeModuleToXM(fp, offsets.modules[i], sampleOffsets, instrumentOffsets, name.c_str(), trimInstruments, (nameMap.find(offsets.modules[i]) != nameMap.end() ? nameMap[offsets.modules[i]].c_str() : NULL), fixCompatibility);
        if (r) {fclose(fp); return r;}
    }
    fclose(fp);
    return 0;
}

#endif // AS_LIBRARY
