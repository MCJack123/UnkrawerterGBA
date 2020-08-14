/*
 * UnkrawerterGBA
 * Version 1.1
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

// Maps type numbers detected in searchForOffsets to strings for display (only used in verbose mode)
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

// Structure to hold results of offset search
struct OffsetSearchResult {
    bool success = false;
    uint32_t instrumentAddr = 0;
    uint32_t instrumentCount = 0;
    uint32_t sampleAddr = 0;
    uint32_t sampleCount = 0;
    std::vector<uint32_t> modules;
};

// Searches a ROM file pointer for offsets to modules, an instrument list, and a sample list.
// This looks for sets of 4-byte aligned addresses in the form 0x08xxxxxx or 0x09xxxxxx
// Once the sets are found, their types are determined by dereferencing the addresses and checking
// whether the data stored therein is consistent with the structure type.
// Sets that don't match exactly one type are discarded.
// Returns a structure with the addresses to the instrument & sample lists, as well as all modules.
OffsetSearchResult searchForOffsets(FILE* fp, int threshold = 4, bool verbose = false) {
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
Pattern * readPatternFile(FILE* fp, uint32_t offset) {
    fseek(fp, offset + 32, SEEK_SET);
    std::vector<uint8_t> fileContents;
    unsigned short rows = 0;
    fread(&rows, 2, 1, fp);
    // We don't need to do full decoding; decode just enough to understand the size of the pattern
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

// Read a module from a file pointer to a Module structure pointer
// This reads all its patterns as well
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

// Read an instrument from a file pointer to an Instrument structure
Instrument readInstrumentFile(FILE* fp, uint32_t offset) {
    fseek(fp, offset, SEEK_SET);
    Instrument retval;
    fread(&retval, sizeof(retval), 1, fp);
    return retval;
}

// Read a sample from a file pointer to a Sample structure pointer
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
const std::pair<unsigned short, unsigned char> effectMap[] = {
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
    {0x0E90, 0x0F},       // EFF_RETRIG
    {0x0700, 0xFF},       // EFF_TREMOLO					30
    {0xFFFF, 0xFF},       // EFF_FVIBRATO                   (S3M!)
    {0x1000, 0xFF},       // EFF_GLOBAL_VOL
    {0x1100, 0xFF},       // EFF_GLOBAL_VOLSLIDE
    {0x0800, 0xFF},       // EFF_PAN
    {0x2300, 0xFF},       // EFF_PANBRELLO					35 (MPT!)
    {0xFFFF, 0xFF},       // EFF_MARK
    {0x0E30, 0x0F},       // EFF_GLISSANDO
    {0x0E40, 0x0F},       // EFF_WAVE_VIBR
    {0x0E70, 0x0F},       // EFF_WAVE_TREMOLO
    {0x2150, 0x0F},       // EFF_WAVE_PANBRELLO				40 (MPT!)
    {0x2160, 0x0F},       // EFF_PATTERN_DELAYF			    (!)
    {0x0800, 0xFF},       // EFF_OLD_PAN					(!) converted to EFF_PAN
    {0x0E60, 0x0F},       // EFF_PATTERN_LOOP
    {0x0EC0, 0x0F},       // EFF_NOTE_CUT
    {0x0ED0, 0x0F},       // EFF_NOTE_DELAY					45
    {0x0EE0, 0x0F},       // EFF_PATTERN_DELAY			    (*)
    {0x1300, 0xFF},       // EFF_ENV_SETPOS
    {0x0900, 0xFF},       // EFF_OFFSET_HIGH
    {0x0600, 0xFF},       // EFF_VOLSLIDE_VIBRATO_XM
    {0x0500, 0xFF}        // EFF_VOLSLIDE_PORTA_XM			50
};

// Writes a module from a file pointer to a new XM file.
// XM file format from http://web.archive.org/web/20060809013752/http://pipin.tmd.ns.ac.yu/extra/fileformat/modules/xm/xm.txt
int writeModuleToXM(FILE* fp, uint32_t moduleOffset, const std::vector<uint32_t> &sampleOffsets, const std::vector<uint32_t> &instrumentOffsets, const char * filename) {
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
    // Write the XM header info
    fwrite("Extended Module: Krawall conversion  \032UnkrawerterGBA      \x04\x01\x14\x01\0\0", 1, 64, out);
    fputc(mod->numOrders, out);
    fputcn(0, 3, out); // 4-byte padding
    fputc(mod->channels, out);
    fputc(0, out); // 2-byte padding
    unsigned short pnum = patternCount;
    fwrite(&pnum, 2, 1, out);
    uint32_t instrumentSizePos = ftell(out); // we'll get back to this later
    fputcn(0, 2, out);
    fputc((mod->flagLinearSlides ? 1 : 0), out);
    fputc(0, out); // 2-byte padding
    fputc(mod->initSpeed, out);
    fputc(0, out); // 2-byte padding
    fputc(mod->initBPM, out);
    fputc(0, out); // 2-byte padding
    fwrite(mod->order, 1, 256, out);
    std::vector<unsigned short> instrumentList; // used to hold the instruments used so we can remove unnecessary instruments
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
                    if (note & 0x80) { // If the note > 128, the instrument field is 2 bytes long
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
                    unsigned short xmeffect = effectMap[effect].first;
                    unsigned char effectmask = effectMap[effect].second;
                    if (xmeffect == 0xFFFF) { // Ignored
                        xmflag &= ~0x18;
                        effect = 0;
                        effectop = 0;
                    } else if (effect == 6) { // S3M volume slide
                        if ((effectop & 0xF0) == 0xF0) { // fine decrease
                            effect = 0x0E;
                            effectop = 0xB0 | (effectop & 0x0F);
                        } else if ((effectop & 0x0F) == 0x0F) { // fine increase
                            effect = 0x0E;
                            effectop = 0xA0 | (effectop >> 4);
                        } else { // normal volume slide
                            effect = 0x0A;
                        }
                    } else if (effect == 11) { // S3M porta down
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
                        if ((effectop & 0xF0) == 0xF0) { // fine
                            effect = 0x0E;
                            effectop = 0x10 | (effectop & 0x0F);
                        } else if ((effectop & 0xF0) == 0xE0) { // extra fine
                            effect = 0x21;
                            effectop = 0x10 | (effectop & 0x0F);
                        } else { // normal
                            effect = 0x01;
                        }
                    } else if (effect == 25 || effect == 26 || effect == 31) { // Unsupported S3M effects
                        if (!(warnings & 0x02)) {warnings |= 0x02; printf("Warning: Pattern %d uses an effect specific to S3M. It will not play correctly.\n", i);}
                        xmflag &= ~0x18;
                        effect = 0;
                        effectop = 0;
                    } else { // Other effects
                        // Warn if MPT-only
                        if (effect == 35 || effect == 40 && !(warnings & 0x01)) {warnings |= 0x01; printf("Warning: Pattern %d uses an effect specific to OpenMPT. It will not play correctly in other trackers.\n", i);}
                        xmeffect = xmeffect | (effectop & effectmask);
                        effect = xmeffect >> 8;
                        effectop = xmeffect & 0xFF;
                    }
                }
                // If the channel is OOB then don't store it (prevents segfaults, but that shouldn't happen if the file's good)
                if (channel >= mod->channels) continue;
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
                    if (thisrow[j].xmflag & 0x08) fputc(thisrow[j].effect, out);
                    if (thisrow[j].xmflag & 0x10) fputc(thisrow[j].effectop, out);
                } else fputc(0x80, out); // Empty note (do nothing this row)
            }
        }
        free(thisrow);
        // Write the size of the packed pattern data
        uint32_t endPos = ftell(out);
        fseek(out, sizePos, SEEK_SET);
        unsigned short size = endPos - sizePos - 2;
        fwrite(&size, 2, 1, out);
        fseek(out, endPos, SEEK_SET);
    }
    // Write the total number of instruments used in the module
    uint32_t endPos = ftell(out);
    fseek(out, instrumentSizePos, SEEK_SET);
    pnum = instrumentList.size();
    fwrite(&pnum, 2, 1, out);
    fseek(out, endPos, SEEK_SET);
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
            if (s->hq) {
                uint32_t ssize = s->size / 2;
                fwrite(&ssize, 4, 1, out);
            } else fwrite(&s->size, 4, 1, out);
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
            fputc((s->loop ? 1 : 0) | (s->hq ? 4 : 0), out);
            fputc(s->panDefault + 0x80, out);
            fputc(s->relativeNote, out);
            fputc(0, out);
            memset(name, ' ', 22);
            snprintf(name, 22, "Sample%d", samples[j]);
            fwrite(name, 1, 22, out);
            sarr.push_back(s); // Push the read sample back so we don't have to allocate & read it again
        }
        // Write the actual sample data
        for (int j = 0; j < sarr.size(); j++) {
            Sample * s = sarr[j];
            // Everything's written as deltas instead of absolute values
            if (s->hq) { // 16-bit?
                // Don't have an example of this in the wild (yet?), so support isn't guaranteed
                signed short old = 0;
                for (uint32_t k = 0; k < s->size; k+=2) {
                    fputc(((signed short*)s->data)[k] - old, out);
                    old = ((signed short*)s->data)[k];
                }
            } else { // 8-bit
                // We also convert from signed to unsigned here since it has to be unsigned
                unsigned char old = 0;
                for (uint32_t k = 0; k < s->size; k++) {
                    fputc(((int)s->data[k] + 0x80) - old, out);
                    old = (int)s->data[k] + 0x80;
                }
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

// Looks for a string in a file
bool fstr(FILE* fp, const char * str) {
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
        fprintf(stderr, "Usage: %s <rom.gba> [output dir] [search threshold] [verbose]\n", argv[0]);
        return 1;
    }
    // Open the ROM file
    FILE* fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open file %s for reading.", argv[1]);
        return 2;
    }
    // Look for a Krawall signature in the file and warn if one isn't found
    if (!fstr(fp, "Krawall")) printf("Warning: Could not find Krawall signature. Are you sure this game uses the Krawall engine?\n");
    rewind(fp);
    // Search for the offsets
    OffsetSearchResult offsets;
    if (argc > 3) offsets = searchForOffsets(fp, atoi(argv[3]), argc > 4);
    else offsets = searchForOffsets(fp);
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
    fseek(fp, offsets.instrumentAddr, SEEK_SET);
    for (int i = 0; i < offsets.instrumentCount; i++) {
        fread(&tmp, 4, 1, fp);
        instrumentOffsets.push_back(tmp & 0x1ffffff);
    }
    // Write out all of the new modules
    for (int i = 0; i < offsets.modules.size(); i++) {
        std::string name = (argc > 2 ? std::string(argv[2]) + "/" : std::string()) + "Module" + std::to_string(i) + ".xm";
        int r = writeModuleToXM(fp, offsets.modules[i], sampleOffsets, instrumentOffsets, name.c_str());
        if (r) {fclose(fp); return r;}
    }
    fclose(fp);
    return 0;
}
