#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    bool set;
    unsigned char note, volume, effect, effectop;
    unsigned short instrument;
} Note;

bool readPattern(FILE* fp, unsigned long offset, int modNum, int num, int channelCount) {
    fseek(fp, offset, SEEK_SET);
    char title[32];
    snprintf(title, 32, "Module%02dPattern%02d.bin", modNum, num);
    FILE* out = fopen(title, "wb");
    snprintf(title, 32, "Module%02dPattern%02d.csv", modNum, num);
    FILE* csv = fopen(title, "w");
    unsigned short index[16];
    fread(index, 2, 16, fp);
    fwrite(index, 2, 16, out);
    unsigned short rows = 0;
    fread(&rows, 2, 1, fp);
    fwrite(&rows, 2, 1, out);
    for (int i = 0; i < channelCount; i++) fprintf(csv, "Channel%d%c", i, i == channelCount - 1 ? '\n' : ',');
    Note * thisrow = (Note*)calloc(channelCount, sizeof(Note));
    for (int row = 0; row < rows; row++) {
        memset(thisrow, 0, sizeof(Note) * channelCount);
        for (;;) {
            unsigned char follow = fgetc(fp);
            fputc(follow, out);
            if (!follow) break;
            int channel = follow & 0x1f;
            unsigned char note = 0, volume = 0, effect = 0, effectop = 0;
            unsigned short instrument = 0;
            if (follow & 0x20) {
                note = fgetc(fp);
                fputc(note, out);
                instrument = fgetc(fp);
                fputc((unsigned char)instrument, out);
                if (note & 0x80) {
                    instrument |= fgetc(fp) << 8;
                    fputc(instrument >> 8, out);
                    note &= 0x7f;
                }
            }
            if (follow & 0x40) {
                volume = fgetc(fp);
                fputc(volume, out);
            }
            if (follow & 0x80) {
                effect = fgetc(fp);
                fputc(effect, out);
                effectop = fgetc(fp);
                fputc(effectop, out);
            }
            thisrow[channel].set = true;
            thisrow[channel].note = note;
            thisrow[channel].instrument = instrument;
            thisrow[channel].volume = volume;
            thisrow[channel].effect = effect;
            thisrow[channel].effectop = effectop;
        }
        for (int i = 0; i < channelCount; i++) {
            if (i != 0) fputc(',', csv);
            if (thisrow[i].set) {
                if (thisrow[i].note) fprintf(csv, "+%d", thisrow[i].note);
                if (thisrow[i].instrument) fprintf(csv, "#%d", thisrow[i].instrument);
                if (thisrow[i].volume) fprintf(csv, "@%d", thisrow[i].volume);
                if (thisrow[i].effect) fprintf(csv, "&%d", thisrow[i].effect);
                if (thisrow[i].effectop) fprintf(csv, "$%d", thisrow[i].effectop);
            } else fputc('-', csv);
        }
        fputc('\n', csv);
    }
    free(thisrow);
    fclose(out);
    fclose(csv);
    return true;
}

int readModule(FILE* fp, unsigned long offset, int num) {
    fseek(fp, offset, SEEK_SET);
    unsigned char channelCount = fgetc(fp);
    unsigned char numOrders = fgetc(fp);
    fseek(fp, offset, SEEK_SET);
    char title[32];
    snprintf(title, 32, "Module%02d.bin", num);
    FILE* out = fopen(title, "wb");
    unsigned char tmp[364];
    fread(tmp, 1, 364, fp);
    fwrite(tmp, 1, 364, out);
    for (int i = 0; i < numOrders; i++) {
        unsigned long lastOffset = ftell(fp);
        unsigned long patternOffset = 0;
        fread(&patternOffset, 4, 1, fp);
        fwrite(&patternOffset, 4, 1, out);
        patternOffset &= 0x7FFFFFF;
        if (!readPattern(fp, patternOffset, num, i, channelCount)) return 0;
        fseek(fp, lastOffset + 4, SEEK_SET);
    }
    fclose(out);
    return numOrders;
}

void readSingleInstrument(FILE* fp, int num) {
    char title[32];
    snprintf(title, 32, "Instrument%02d.bin", num);
    FILE* out = fopen(title, "wb");
    unsigned char tmp[302];
    fread(tmp, 1, 302, fp);
    fwrite(tmp, 1, 302, out);
    fclose(out);
    // parse it into separate file?
}

int readInstrumentList(FILE* fp, unsigned long offset, int num) {
    fseek(fp, offset, SEEK_SET);
    int i = 0;
    unsigned long addr = 0;
    for (fread(&addr, 4, 1, fp); addr & 0x8000000; i++, fread(&addr, 4, 1, fp)) {
        unsigned long pos = ftell(fp);
        fseek(fp, addr & 0x7ffffff, SEEK_SET);
        readSingleInstrument(fp, num + i);
        fseek(fp, pos, SEEK_SET);
    }
    return i;
}

void readSingleSample(FILE* fp, int num) {
    char title[32];
    unsigned long loopLength = 0, end = 0;
    fread(&loopLength, 4, 1, fp);
    fread(&end, 4, 1, fp);
    end &= 0x7ffffff;
    snprintf(title, 32, "Sample%02d.bin", num);
    FILE* out = fopen(title, "wb");
    fwrite(&loopLength, 4, 1, out);
    unsigned long currentSize = end - ftell(fp) - 10;
    fwrite(&currentSize, 4, 1, out);
    snprintf(title, 32, "Sample%02d.wav", num);
    FILE* wav = fopen(title, "wb");
    fwrite("RIFF", 4, 1, wav);
    unsigned long sampleRate = 0;
    fread(&sampleRate, 4, 1, fp);
    fwrite(&sampleRate, 4, 1, out);
    for (int i = 0; i < 6; i++) fputc(fgetc(fp), out);
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
    fwrite(data, 1, size, out);
    fwrite(data, 1, size, wav);
    fclose(out);
    fclose(wav);
}

int readSamples(FILE* fp, unsigned long offset, int num) {
    fseek(fp, offset, SEEK_SET);
    int i;
    for (i = 0;; i++, num++) {
        unsigned long currentOffset = ftell(fp);
        //printf("%08lX\n", currentOffset);
        unsigned long loopLength = 0, end = 0;
        fread(&loopLength, 4, 1, fp);
        fread(&end, 4, 1, fp);
        if (!(end & 0x08000000) || end < currentOffset || end - currentOffset < loopLength) break;
        fseek(fp, currentOffset, SEEK_SET);
        readSingleSample(fp, num);
        fseek(fp, end + 0x48 - (end % 4), SEEK_SET);
    }
    return i;
}

int readSampleList(FILE* fp, unsigned long offset, int num) {
    fseek(fp, offset, SEEK_SET);
    int i = 0;
    unsigned long addr = 0;
    for (fread(&addr, 4, 1, fp); addr & 0x8000000; i++, fread(&addr, 4, 1, fp)) {
        unsigned long pos = ftell(fp);
        fseek(fp, addr & 0x7ffffff, SEEK_SET);
        readSingleSample(fp, num + i);
        fseek(fp, pos, SEEK_SET);
    }
    return i;
}

int main(int argc, const char * argv[]) {
    if (argc < 3 || strcmp(argv[1], "-h") == 0) {
        fprintf(stderr, "Usage: %s <ROM.gba> <type:address...>\n", argv[0]);
        return 1;
    }
    int currentModule = 0, currentInstrument = 0, currentSample = 0;
    FILE* fp = fopen(argv[1], "rb");
    if (fp == NULL) {
        fprintf(stderr, "Could not open file %s for reading.\n", argv[1]);
        return 2;
    }
    for (int i = 2; i < argc; i++) {
        unsigned long offset = strtoul(argv[i] + 1, NULL, 16);
        offset &= 0x7ffffff;
        int read;
        switch (argv[i][0]) {
            case 'm':
                read = readModule(fp, offset, currentModule++);
                if (!read) {
                    fprintf(stderr, "Failed to read module at offset %08lX\n", offset);
                    fclose(fp);
                    return 3;
                }
                printf("Read module with %d patterns at offset %08lX\n", read, offset);
                break;
            case 's':
                read = readSamples(fp, offset, currentSample);
                currentSample += read;
                printf("Read %d samples at offset %08lX\n", read, offset);
                break;
            case 't': case 'l':
                read = readSampleList(fp, offset, currentSample);
                currentSample += read;
                printf("Read %d samples from the list at offset %08lX\n", read, offset);
                break;
            case 'i':
                read = readInstrumentList(fp, offset, currentInstrument);
                currentInstrument += read;
                printf("Read %d instruments from the list at offset %08lX\n", read, offset);
                break;
            default:
                printf("Unknown offset type %c\n", argv[i][0]);
        }
    }
    fclose(fp);
    return 0;
}