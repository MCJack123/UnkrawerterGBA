# UnkrawerterGBA
A tool to rip music from Gameboy Advance games that use the Krawall sound engine.

## Compiling
The latest version can be downloaded precompiled on the Releases tab, or you can compile it yourself:
* GCC/Clang: `g++ -std=c++11 -o UnkrawerterGBA unkrawerter.cpp`  
* Microsoft Visual C++: `cl /EHsc /FeUnkrawerterGBA.exe unkrawerter.cpp`

## Usage
Jut run the program from the command line with the path to the ROM as the first argument. You can also specify a second argument for the output directory (defaults to the current one), a third argument for the address count threshold (defaults to 4), and a fourth argument for verbose mode. It will output one XM module file per song in the form `Module<n>.xm` in the output directory.

### Threshold argument
UnkrawallGBA searches for audio files by looking through the ROM for lists of pointers to structures with the audio data. These lists can either be the master instrument list, the master sample list, or a module's list of patterns. By default, UnkrawallGBA ignores any lists with less than four addresses. This is to avoid detecting single variables that are unrelated to Krawall, speeding up detection time. But some songs may have less than four patterns, and so they won't be detected with the default threshold. You can adjust this number to detect modules with fewer patterns, but it may take longer for it to filter out all of the addresses that are not related to Krawall.

### Verbose mode
Enable verbose mode to show all of the detected addresses and their types. This can be useful if UnkrawallGBA isn't detecting one of the required lists properly.

## Legacy tools
These were the original three tools used before they were combined into one program. They remain here for legacy purposes and debugging.

### `find_krawall_offsets`
This tool automatically detects the necessary offsets for UnkrawerterGBA in a ROM file. This should work pretty well, but I can't guarantee the effectiveness of it yet. (It's been able to successfully find all of the offsets in my ROM.)

#### Compiling
`$ gcc -o find_krawall_offsets find_krawall_offsets.cpp`

#### Usage
`find_krawall_offsets` expects at least one argument with the path to the ROM file to search through. You can also give it an optional second argument that specifies the minimum number of consecutive addresses in a list that are required to detect it. This is set to 4 by default, but you'll need to set it lower for any modules with less than 4 patterns (my ROM had one of those). You can also give it a third argument which can be anything, and whose presence will tell the program to show all detected offsets with their type. This can be useful if you're having trouble finding all of the offsets and want to see what addresses were detected with what type.

### `extract_krawall_data`
This tool extracts data of various types from the specified offsets in a ROM into separate files. This prepares the data to be converted into a suitable format.

#### Compilation
`$ gcc -o extract_krawall_data extract_krawall_data.c`

#### Usage
`extract_krawall_data` expects the first argument to be the path to the ROM file to read from. After that, each argument is an offset in hexadecimal with a letter prefix specifying what kind of data to extract. Here is the current list of prefixes:
* `m`: Reads a module structure and its associated patterns.
* `l`: Reads samples from a list of sample addresses.
* `i`: Reads instruments from a list of instrument addresses.
* `s`: Reads multiple samples starting at the specified address. This is not guaranteed to work and may be removed in the future.

The resulting files will be output in the current directory in the form `<type><number>.bin` (except for patterns, which are in the form `Module<modnum>Pattern<num>.bin`). In addition, patterns will have an accompanying CSV file output with the decoded contents of the pattern; and samples will have an accompanying WAV file with the playable audio data for that sample.

For example, running `extract_krawall_data game.gba m12ab56 l15e69c` will extract the module located in `game.gba` at offset 0x12ab56, the patterns that are used by the module, and the samples in the list located at offset 0x15e69c.

### `create_xm_from_krawall`
This tool converts the extracted data from `extract_krawall_data` into an XM module file that can by played by a tracker program that supports XM modules, such as OpenMPT.

#### Compiling
`$ g++ -o create_xm_from_krawall create_xm_from_krawall.cpp`

#### Usage
`create_xm_from_krawall` expects two initial arguments: the path to the previously extracted module binary, and the output XM file. After that, it expects paths to either pattern files, sample files, or instrument files. The `-p`, `-s`, and `-i` flags will tell it that the following files are patterns, samples, or instruments, respectively, until the next flag is specified or there are no more arguments. You must specify one of these flags after the output argument. There must be at least one of each type, and if you don't specify all of the files needed the program will crash.

For example, running `create_xm_from_krawall Module00.bin Module00.xm -p Module00Pattern*.bin -s Sample*.bin -i Instrument*.bin` will create a new XM module `Module00.xm` from the module binary `Module00.bin`, using all of the previously extracted patterns, samples, and instruments.

### Finding Krawall data structures in ROMs manually
If you desire to find the offsets on your own (such as if the automatic finder isn't working properly), you can search through the ROM for the offsets manually. This process will require the use of a hex editor, as well as some basic knowledge on reading hexadecimal from files.

#### Modules
Modules are probably the easiest structure to locate in a ROM. Search for a block of at least 256 bytes of 0's. Just before this block, check for a chunk of bytes that are ascending from 0. Before that, look for a byte that will likely be 8, as well as another byte that should tell you the number of bytes in that ascending chunk. At the end of the block of 0's, the first byte will likely be 0x80 (but don't rely on it). The next byte should be below 10, and the byte after that should be a reasonable BPM (probably 60 <= n <= 180). The next 6 bytes should be either 1 or 0, with the last byte definitely being 0. Lastly, there will be a list of 4-byte addresses in the decoded form `0x08xxxxxx`, or `xx xx xx 08` in the hex editor.

If the block you found matches this description: congratulations, you found a module structure! The offset you need is the address of the first byte that was probably 8, before the increasing bytes and 0 chunk.

#### Sample lists & instrument lists
Krawall stores the list of samples & instruments in two blocks of data as pointer arrays. You will need to look for chunks of data that are in the form `xx xx xx 08 xx xx xx 08 xx xx xx 08 xx xx xx 08` in each row. This command should help you find a few candidates:
```sh
xxd ../2403\ -\ 3\ in\ 1\ -\ Life,\ Yahtzee,\ Payday\ \(U\)\(Trashman\).gba | grep -E '[0-9a-f][0-9a-f][0-9a-f][0-9a-f] [1-9a-f][0-9a-f]08 .... ..08 .... ..08 .... ..08'
```
Rule out any lines that are not in chunks of at least 4 lines, or are filled with 08080808 or another regular pattern. You should end up with two blocks of consecutive addresses: one is the list of instruments, and the other is the list of samples. Keep track of the start addresses for each block; these will be your offsets. To find which one each is, take the first address in each block, reverse the bytes (since it's in little-endian), and take off the high 08 bit (for ROMs <= 16MB, take off the high byte; for 32MB ROMs, subtract 8 from the high byte). Then jump to that offset in the file through the hex editor. You will be brought to whatever structure is located there.

An instrument structure will likely have a long string of the same byte (the first instrument will probably have lots of 0's or 1's). A sample structure will likely not have more than 16 bytes less than 0, and lots of bytes in the range 0x70-0x90 in the beginning. Use your findings to determine which block corresponds with what structure type.

#### Finding sample lists through an audio editor
A simpler way to find sample lists is to use an audio editor to load the ROM as a raw audio file. I will be detailing how to do this through Audacity; if using another editor, you will have to adapt these instructions to work with that editor.

First, go to File => Import => Raw Data... and browse to the ROM file you are ripping. Set the audio format to 8-bit unsigned PCM, mono, with any sample rate (though 16384 is preferred). Then search through the file for the first region that resembles an audio track (it shouldn't be pure noise). Place the cursor at the beginning, and zoom in until you can see the individual sample dots. Then move the cursor to the very first sample of the audio.

Next, switch your unit by selecting the arrow at the bottom next to the selection time, and change it to samples. Take the number that is now displayed there, and convert it to hexadecimal either with the calculator or an online tool. Subtract 18 decimal (0x12 hex) from that number, add 0x08000000 to it, and reverse the bytes. For example, if the sample number at the bottom is 2449246, it would become 0x255f5e, then 0x255f4c, then 0x08255f4c, then 4c 5f 25 08.

Finally, go back into the hex editor and search for this sequence of bytes. If done correctly, this will take you straight to the sample list. Record the address that the value is located at, as this is your sample list offset.
