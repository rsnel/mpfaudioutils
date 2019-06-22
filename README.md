Transcode MPF-I(B) programs to and from audio
=============================================

Introduction
------------

The [Micro-Professor MPF-I(B)](https://en.wikipedia.org/wiki/Micro-Professor_MPF-I)
is a single board computer from the early eighties manufactured by Multitech
(which became Acer in 1987). The computer is based on the Zilog Z80 CPU. For
brevity we will call this machine by the short name MPF.

The computer can communicate with the outside world using:
* a keyboard
* six 7+1 segment displays
* a header that connects to the CPU bus
* a header that connect to the optional Z80-CTC and/or Z80-PIO
* audio line in, speaker and line out

The MPF includes software to store and retrieve data through the audio interface
and is originally designed to use a cassette recorder. However, a normal PC with
line out and mic in can also be used.

This suite of programs enable you to translate between the audio file and a hex
representation of the data sent to and from the MPF. It mainly consists of
`raw2mpf`, to convert an audio stream to hex text and `mpf2raw` to do the reverse.

The programs attempt to follow the Unix philosophy (quote from Doug McIlroy
(2003) in "The Art of Unix programming: Basics of the Unix Philosophy"): "Write
programs that do one thing and do it well. Write programs that work together.
Write programs to handle text streams, because that is a universal interface."


Handling audio files
--------------------

We use raw audio files: unsigned 8 bit with a sample rate of 8kHz.  Other formats
must be converted by using, for example, sox(1). See below for some examples.

This text assumes you have a basic understanding of the `TAPE WR` and `TAPE RD`
functionality of the MPF. These two functions are documented in the
excellent manual of the MPF, which can be found on the internet. See the section
Resources at the end of this README.

The BASIC commands `LOAD` and `SAVE` work in the same way. However, the BASIC
filename (which is a decimal number from 0 to 255) is the second byte of the
read filename. The first byte of the real filename is `0xba` for BASIC programs.
BASIC programs are expected to be loaded at `0x18e7`.

To record a program from your MPF, connect the 'mic' output of the MPF to the
'mic' input of the PC. Set the MIC gain higher if the signal does not max out
the dynamic range. Start recording just before pressing 'GO' on the MPF.
Terminate with `^C` after the MPF is done sending the audio.

    $ arecord -t raw -f U8 -r 8000 data.raw

To play a program to your MPF, connect the 'ear' input of the MPF to the
left channel of the line-out of your PC. Set the output volume to max. Start the
`TAPE RD` function of the MPF and enter the following command:

    $ aplay -t raw -f U8 -r 8000 data.raw


Compiling and installing
------------------------

The commands

    $ make
    # make install

should suffice. The programs are installed in /usr/local/bin


Using `raw2mpf` and `mpf2raw`
-----------------------------

A list of examples reveal all the use cases of `raw2mpf` and `mpf2raw`.

To extract programs from an audio stream you can do:

    $ raw2mpf < data.raw > programs.mpf

To do the reverse you do:

    $ mpf2raw < programs.mpf > data.raw

To cleanup a recording:

    $ raw2mpf < dirty.raw | mpf2raw > clean.raw

To record from the MPF live, terminate with `^C`:

    $ arecord -t raw -f U8 -r 8000 | raw2mpf > programs.mpf

And do the reverse:

    $ mpf2raw < programs.mpf | aplay -t raw -f U8 -r 8000

For convenience mpf2alsa and alsa2mpf are also provided. Record:

    $ alsa2mpf > programs.mpf   # terminate with ^C

Play to MPF:

    $ mpf2alsa < programs.mpf


.mpf file format
----------------

The format of .mpf files is very simple. Each line represents a program, it looks like:

    xxxx/yyyy:zzzzzzzz....\n

    xxxx is the hexadecimal 'filename'
    yyyy is the hexadecimal loading address
    zzzzzz is an even number of hexadecimal digits that represent the data

Note that there is no \r at the end of the line. We use the UNIX convention for
text files. To convert, use `dos2unix(1)`.

For example:

    1234/1800:0076

is a program with filename `0x1234`

    0x1800 00 nop
    0x1801 76 halt


Audio format
------------

The MPF comes with a detailed manual that explains the format in which the
data is written to (and read from) tape.

Here is a short version of the spec. Every program starts with a `LEAD_SYNC`,
followed by seven bytes of `HEADER` info, then a `MID_SYNC`, followed by all the
actual `DATA` bytes and, in closing, a `TAIL_SYNC`. In detail:

    LEAD_SYNC: 4 seconds of 1kHz

    HEADER:
    2 bytes filename (little endian)
    2 bytes first_address (little endian)
    2 bytes last_address (inclusive) (little endian)
    1 byte checksum (sum of all bytes in data section)

    MID_SYNC:
    2 seconds of 2kHz

    DATA:
    last_address - first_address + 1 bytes of data

    TAIL_SYNC:
    2 seconds of 2kHz

All the information is encoded using two tones:

    1kHz: one period of tone @8kHz U8, 1ms; `O: FF FF FF FF 00 00 00 00`
    2kHz: two periods of tone @8kHz U8, 1ms; `X: FF FF 00 00 FF FF 00 00`

Bits are encoded as below:

    0: XXO
    1: XOO

And bytes are encoded as:

    start bit (0)
    bit 0 t/m bit 7
    end bit (1)

While the on tape format has some redundancy, it is not used by `raw2mpf`; it
bails on the current program immediately if something is out of order.


Examples
--------

### `coffee.mpf`

This program prints `COFFEE` on the display, and waits for the user to press `GO`,
if that happens the monitor is entered through `RST 30h`.

### sbprojects.net

The website [https://www.sbprojects.net/projects/mpf1/] contains interesting
programs for the MPF (with sourcecode and explanations). Each program is
available as `.asm`, `.hex`, `.lst` and `.mp3` file. I didn't have much luck
with playing the `.mp3`, but I have written a small script to translate the `.hex` files
to `.mpf` files.

For example:

    ./sbhex2mpf < dice6.hex > dice6.mpf

`dice6.mpf` can then be loaded in the usual way.

Another option is using `sox(1)` to decode the `.mp3` to `.raw` and to clean the
recording up by running it through `raw2mpf | mpf2raw`.

### Sox

The program `sox(1)` can be used to convert audiofiles to the correct format for
`raw2mpf`. Suppose you have an `.mp3` of a program called `test.mp3`. It can be
converted to our `.raw` format using:

    $ sox test.mp3 -r 8000 -e unsigned-integer -c 1 -b 8 test.raw

Be sure to have `libsox-fmt-mp3` installed if `sox` complains about `no handler
for file extension .mp3`.

Captured files can be converted to `.wav` for inspection in, for example,
`audacity(1)`.

    $ sox -r 8000 -e unsigned-integer -c 1 -b 8 test.raw test.wav

### Reading the audio data by eye

In a wave editor, the data can be read with relative ease. Two slow waves are a
`0` and four slow waves are a `1`. Every byte consists of a start bit, `0`,
then its own bits from 0 to 7, and a stop bit `1`. So the bits of each byte are
stored in little endian order.

For example, a 1 bit looks like this

     _   _   _   _   ___     ___     ___     ___
    / \_/ \_/ \_/ \_/   \___/   \___/   \___/   \___/

and a 0 bit looks like

     _   _   _   _   _   _   _   _   ___     ___     
    / \_/ \_/ \_/ \_/ \_/ \_/ \_/ \_/   \___/   \___/

to decode the audio, you only have to count the slow waves
and view the fast wave as a separator.

Resources
---------

* [MPF-I manual](http://www.1000bit.it/support/manuali/multitech/MPF-I-User's-manual.pdf)
* [Projects for the MPF-I](https://www.sbprojects.net/projects/mpf1/)
* [Manuals and ROM listings of the MPF-I](http://electrickery.xs4all.nl/comp/mpf1/doc/)
* [old-computers.com page](http://www.old-computers.com/museum/computer.asp?c=479)
* [vintagecomputer.net site about the MPF-I](http://www.vintagecomputer.net/fjkraan/comp/mpf1/)
* [Rudi Niemeijer's page about the MPF-I (in Dutch)](http://www.rudiniemeijer.nl/micro-professor-mpf-1/)
