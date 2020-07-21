#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <tuple>
#include <algorithm>

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

int main(int argc, const char * argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom.gba> [threshold=4] [verbose]\n", argv[0]);
        return 1;
    }
    FILE * fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open file %s for reading.", argv[1]);
        return 2;
    }
    int threshold = 4;
    if (argc > 2) threshold = atoi(argv[2]);
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
        return 7;
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
    if (argc > 3) std::for_each(foundAddressLists.begin(), foundAddressLists.end(), [](std::tuple<uint32_t, uint32_t, int> p){printf("Found %d matches at %08X with type %s\n", std::get<1>(p), std::get<0>(p), typemap[std::get<2>(p)]);});

    // Filter results down to one instrument & sample list, and all modules
    uint32_t instrumentAddr = 0, sampleAddr = 0, maxInstrCount = 0, maxSampleCount = 0;
    std::vector<uint32_t> moduleAddrs;
    for (auto p : foundAddressLists) {
        if (std::get<2>(p) == 1) moduleAddrs.push_back(std::get<0>(p));
        else if (std::get<2>(p) == 2 && std::get<1>(p) > maxSampleCount) {maxSampleCount = std::get<1>(p); sampleAddr = std::get<0>(p);}
        else if (std::get<2>(p) == 4 && std::get<1>(p) > maxInstrCount) {maxInstrCount = std::get<1>(p); instrumentAddr = std::get<0>(p);}
    }

    if (instrumentAddr) printf("> Found instrument list at address %08X\n", instrumentAddr);
    if (sampleAddr) printf("> Found sample list at address %08X\n", sampleAddr);
    for (uint32_t a : moduleAddrs) printf("> Found module at address %08X\n", a - 364);

    fclose(fp);
    return 0;
}