# UnkrawerterGBA
A tool to rip music from Game Boy Advance games that use the Krawall sound engine. Exports the audio as XM or S3M module files.

## Compiling
The latest version can be downloaded precompiled on the Releases tab, or you can compile it yourself:
* GCC/Clang: `g++ -std=c++11 -o UnkrawerterGBA unkrawerter.cpp`  
* Microsoft Visual C++: `cl /EHsc /FeUnkrawerterGBA.exe unkrawerter.cpp`

To use UnkrawerterGBA as a library, make sure to add a macro named `AS_LIBRARY` when compiling (for GCC, add `-DAS_LIBRARY` to the command line).

## Usage
In its most basic form, you can run UnkrawerterGBA with just the ROM path, and it will output the module files in the current directory. You can also add the following options to the command line:
```
  -f <file.krm>     Ripped module to convert; may be used multiple times\n"
                      If this option is specified, the <rom.gba> argument must point to the bank instead
  -i <address>      Override instrument list address
  -l <file.txt>     Read module names from a file (one name/line, same format as -n)
  -m <address>      Add an extra module address to the list
  -n <addr>=<name>  Assign a name to a module address (max. 20 characters for XM, 28 for S3M)
  -o <directory>    Output directory
  -s <address>      Override sample list address
  -t <threshold>    Search threshold, lower = slower but finds smaller modules,
                      higher = faster but misses smaller modules (defaults to 4)
  -3                Force extraction to output S3M modules (only supported with some modules)
  -a                Do not trim extra instruments; this will make modules much larger in size!
  -c                Disable compatibility fixes, makes patterns more accurate but worsens playback
  -e                Export samples to WAV files
  -k                Force Krawall version to 20030901 (disables auto-detection)
  -K                Force Krawall version to 20050421 (disables auto-detection)
  -r                Rip data into Krawall bank/modules without conversion
  -v                Enable verbose mode
  -x                Force extraction to output XM modules
  -h                Show this help
```

### Threshold argument
UnkrawerterGBA searches for Krawall data by looking through the ROM for lists of pointers to structures with the Krawall data. These lists can either be the master instrument list, the master sample list, or a module's list of patterns. By default, UnkrawerterGBA ignores any lists with less than four addresses. This is to avoid detecting single variables that are unrelated to Krawall, speeding up detection time. But some songs may have less than four patterns, and so they won't be detected with the default threshold. You can adjust this number with the `-t` argument to detect modules with fewer patterns, but it may take longer for it to filter out all of the addresses that are not related to Krawall.

### Verbose mode
Enable verbose mode (`-v`) to show all of the detected addresses and their types. This can be useful if UnkrawerterGBA isn't detecting one of the required lists properly.

### XM vs. S3M
UnkrawerterGBA 3.0 supports ripping music to either the XM module format or the S3M module format. Krawall natively supports both of these formats, but UnkrawerterGBA has to pick which format it needs to use. XM supports instruments and more than 64 rows per pattern, but S3M has a different effect syntax that is mostly incompatible with XM. Some compatibility fixes are available when exporting to XM, but it is better to export modules originally created as S3Ms to S3M files.

UnkrawerterGBA makes a good guess at whether a module is best converted to XM or S3M. However, if it gets this wrong, or you want to force a different format, you can use the `-3` or `-x` arguments

### Direct rip
UnkrawerterGBA 4.0 adds a way to rip the modules and instruments directly without any conversion. This is useful for getting the highest quality rip without losing any information during conversion, or to keep file sizes low. To create a direct rip, use the `-r` flag. This will create a `.krb` file with the instruments, and one `.krw` file for each module in the ROM.

To convert a direct-rip module into an XM or S3M file, use the `-f` flag for each `.krw` module to rip. Then specify the `.krb` bank file without any flags before it. Finally, run the program, and it will output the new XM or S3M files to the output directory.

## Library API
UnkrawerterGBA also supports usage as a library for embedding in another program. These functions can be used to rip Krawall music from ROMs in another program.

### `struct OffsetSearchResult`
Structure to hold results from unkrawerter_searchForOffsets.
* `success`: Whether all required offsets were found
* `instrumentAddr`: Address of instrument list
* `instrumentCount`: Number of instruments in list
* `sampleAddr`: Address of sample list
* `sampleCount`: Number of samples in list
* `modules`: List of module addresses

### `void unkrawerter_setVersion(uint32_t version)`
Sets the Krawall version to convert from. This MUST be used for ROMs using versions older than 2004-07-07.
* `version`: The Krawall version to set. Should be in the format 0xYYYYMMDD.

### `OffsetSearchResult unkrawerter_searchForOffsets(FILE* fp, int threshold = 4, bool verbose = false)`
Searches a ROM file for offsets and returns the results in a structure.
* `fp`: The file to read from.
* `threshold`: The search threshold (as described above). Defaults to 4.
* `verbose`: Whether to print all addresses found. Defaults to false.
* Returns: An `OffsetSearchResult` structure with the results.

### `void unkrawerter_readSampleToWAV(FILE* fp, uint32_t offset, const char * filename)`
Reads a sample at an offset from a ROM file to a WAV file.
* `fp`: The file to read from.
* `offset`: The offset of the sample to read.
* `filename`: The path to the WAV file to write to.

### `int unkrawerter_writeModuleToXM(FILE* fp, uint32_t moduleOffset, const std::vector<uint32_t> &sampleOffsets, const std::vector<uint32_t> &instrumentOffsets, const char * filename, bool trimInstruments = true, const char * name = NULL, bool fixCompatibility = true, FILE* instfp = NULL)`
Writes a single XM module at an offset from a ROM file, using the specified samples and instruments.
* `fp`: The file to read from.
* `moduleOffset`: The address of the module to read.
* `sampleOffsets`: A list of sample addresses.
* `instrumentOffsets`: A list of instrument addresses.
* `filename`: The path to the XM file to write to.
* `trimInstruments`: Whether to remove instruments that are not used by the module. Defaults to true.
* `name`: The name of the module; if unset then the module is named "Krawall conversion". Defaults to `NULL`. (The name must be <= 20 characters long.)
* `fixCompatibility`: Whether to attempt to fix some effects that behave differently in Krawall/S3M. This will modify the extracted patterns and reduces extraction accuracy, but improves playback accuracy. Defaults to true.
* `instfp`: A file handle to read instruments from. Defaults to `NULL` (use the ROM).
* Returns: 0 on success, non-zero on error.

### `int unkrawerter_writeModuleToS3M(FILE* fp, uint32_t moduleOffset, const std::vector<uint32_t> &sampleOffsets, const char * filename, bool trimInstruments = true, const char * name = NULL, FILE* instfp = NULL)`
Writes a single S3M module at an offset from a ROM file, using the specified samples. This will not work for instrument-based modules, or for patterns that have <> 64 rows.
* `fp`: The file to read from.
* `moduleOffset`: The address of the module to read.
* `sampleOffsets`: A list of sample addresses.
* `filename`: The path to the S3M file to write to.
* `trimInstruments`: Whether to remove instruments that are not used by the module. Defaults to true.
* `name`: The name of the module; if unset then the module is named "Krawall conversion". Defaults to `NULL`. (The name must be <= 28 characters long.)
* `instfp`: A file handle to read instruments from. Defaults to `NULL` (use the ROM).
* Returns: 0 on success, non-zero on error.

### `bool unkrawerter_writeBankFile(FILE* fp, const std::vector<uint32_t> &sampleOffsets, const std::vector<uint32_t> &instrumentOffsets, const char * filename)`
Writes a Krawall Bank file to a path using the specified instrument and sample offsets.
* `fp`: The ROM to read from.
* `sampleOffsets`: The sample offsets in the ROM to use.
* `instrumentOffsets`: The instrument offsets in the ROM to use. Pass an empty list to ignore.
* `filename`: The name of the file to write.
* Returns: `true` on success, `false` on error.

### `bool unkrawerter_writeModuleFile(FILE* fp, uint32_t moduleOffset, const char * filename)`
Writes a Krawall Module file to a path using the specified module offset.
* `fp`: The ROM to read from.
* `sampleOffsets`: The module offset in the ROM to use.
* `filename`: The name of the file to write.
* Returns: `true` on success, `false` on error.

### Finding Krawall data structures in ROMs manually
If you desire to find the offsets on your own (such as if the automatic finder isn't working properly), you can search through the ROM for the offsets manually. This process will require the use of a hex editor, as well as some basic knowledge on reading hexadecimal from files. In most cases this is unnecessary, since the automatic detector is pretty good at finding the offsets itself.

(Note: I've written an in-depth write-up of the format Krawall uses to store data on [The Cutting Room Floor](https://tcrf.net/Format:Krawall). Look there for more info on how the music is stored in the ROM.)

#### Modules
Modules are probably the easiest structure to locate in a ROM. Search for a block of at least 256 bytes of 0's. Just before this block, check for a chunk of bytes that are ascending from 0. Before that, look for a byte that will likely be 8, as well as another byte that should tell you the number of bytes in that ascending chunk. At the end of the block of 0's, the first byte will likely be 0x80 (but don't rely on it). The next byte should be below 10, and the byte after that should be a reasonable BPM (probably 60 <= n <= 180). The next 6 bytes should be either 1 or 0, with the last byte definitely being 0. Lastly, there will be a list of 4-byte addresses in the decoded form `0x0[89]xxxxxx`, or `xx xx xx 0[89]` in the hex editor.

If the block you found matches this description: congratulations, you found a module structure! The offset you need is the address of the first byte that was probably 8, before the increasing bytes and 0 chunk.

#### Sample lists & instrument lists
Krawall stores the list of samples & instruments in two blocks of data as pointer arrays. You will need to look for chunks of data that are in the form `xx xx xx 0[89] xx xx xx 0[89] xx xx xx 0[89] xx xx xx 0[89]` in each row. This command should help you find a few candidates:
```sh
xxd rom.gba | grep -E '[0-9a-f][0-9a-f][0-9a-f][0-9a-f] [1-9a-f][0-9a-f]0[89] .... ..0[89] .... ..0[89] .... ..0[89]'
```
Rule out any lines that are not in chunks of at least 4 lines, or are filled with 08080808 or another regular pattern. You should end up with two blocks of consecutive addresses: one is the list of instruments, and the other is the list of samples. Keep track of the start addresses for each block; these will be your offsets. To find which one each is, take the first address in each block, reverse the bytes (since it's in little-endian), and take off the high 08 bit (for ROMs <= 16MB, take off the high byte; for 32MB ROMs, subtract 8 from the high byte). Then jump to that offset in the file through the hex editor. You will be brought to whatever structure is located there.

An instrument structure will likely have a long string of the same byte (the first instrument will probably have lots of 0's or 1's). A sample structure will likely not have more than 16 bytes less than 0, and lots of bytes in the range 0x70-0x90 in the beginning. Use your findings to determine which block corresponds with what structure type.

#### Finding sample lists through an audio editor
A simpler way to find sample lists is to use an audio editor to load the ROM as a raw audio file. I will be detailing how to do this through Audacity; if using another editor, you will have to adapt these instructions to work with that editor.

First, go to File => Import => Raw Data... and browse to the ROM file you are ripping. Set the audio format to 8-bit unsigned PCM, mono, with any sample rate (though 16384 is preferred). Then search through the file for the first region that resembles an audio track (it shouldn't be pure noise). Place the cursor at the beginning, and zoom in until you can see the individual sample dots. Then move the cursor to the very first sample of the audio.

Next, switch your unit by selecting the arrow at the bottom next to the selection time, and change it to samples. Take the number that is now displayed there, and convert it to hexadecimal either with the calculator or an online tool. Subtract 18 decimal (0x12 hex) from that number, add 0x08000000 to it, and reverse the bytes. For example, if the sample number at the bottom is 2449246, it would become 0x255f5e, then 0x255f4c, then 0x08255f4c, then 4c 5f 25 08.

Finally, go back into the hex editor and search for this sequence of bytes. If done correctly, this will take you straight to the sample list. Record the address that the value is located at, as this is your sample list offset.
