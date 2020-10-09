/*
 * UnkrawerterGBA
 * Version 3.0
 * 
 * This program automatically extracts music files from Gameboy Advance games
 * that use the Krawall sound engine. Audio files are extracted in the XM or S3M
 * module format, which can be opened by programs such as OpenMPT.
 * 
 * This file is intended for use as a library. Make sure to define AS_LIBRARY
 * when compiling UnkrawerterGBA to avoid defining main.
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

#ifndef UNKRAWERTERGBA_HPP
#define UNKRAWERTERGBA_HPP
#include <vector>
#include <cstdint>
#include <cstdio>

// Structure to hold results from unkrawerter_searchForOffsets.
struct OffsetSearchResult {
    bool success = false;          // Whether all required offsets were found
    uint32_t instrumentAddr = 0;   // Address of instrument list
    uint32_t instrumentCount = 0;  // Number of instruments in list
    uint32_t sampleAddr = 0;       // Address of sample list
    uint32_t sampleCount = 0;      // Number of samples in list
    std::vector<uint32_t> modules; // List of module addresses
};

// Sets the Krawall version to convert from. This MUST be used for ROMs using versions older than 2004-07-07.
extern void unkrawerter_setVersion(uint32_t version);

// Searches a ROM file for offsets and returns the results in a structure.
extern OffsetSearchResult unkrawerter_searchForOffsets(FILE* fp, int threshold = 4, bool verbose = false);

// Reads a sample at an offset from a ROM file to a WAV file.
extern void unkrawerter_readSampleToWAV(FILE* fp, uint32_t offset, const char * filename);

// Writes a single XM module at an offset from a ROM file, using the specified samples and instruments.
// trimInstruments specifies whether to remove instruments that are not used by the module.
// name specifies the name of the module; if unset then the module is named "Krawall conversion".
// fixCompatibility specifies whether to make some changes to the pattern data in order to emulate some Krawall/S3M quirks. Set to true for accurate playback; set to false for accurate pattern data.
// Returns 0 on success, non-zero on error.
extern int unkrawerter_writeModuleToXM(
    FILE* fp,
    uint32_t moduleOffset,
    const std::vector<uint32_t> &sampleOffsets,
    const std::vector<uint32_t> &instrumentOffsets,
    const char * filename,
    bool trimInstruments = true,
    const char * name = NULL,
    bool fixCompatibility = true
);

// Writes a module from a file pointer to a new S3M file.
// trimInstruments specifies whether to remove instruments that are not used by the module.
// name specifies the name of the module; if unset then the module is named "Krawall conversion".
// Returns 0 on success, non-zero on error.
extern int unkrawerter_writeModuleToS3M(
    FILE* fp,
    uint32_t moduleOffset,
    const std::vector<uint32_t> &sampleOffsets,
    const char * filename,
    bool trimInstruments = true,
    const char * name = NULL
);

#endif
