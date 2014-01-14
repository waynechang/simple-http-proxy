simple-http-proxy
=================

A simple concurrent HTTP proxy written in C.
Not yet fit for production.

## Usage

    $ make

    # sequential (single-threaded)
    $ ./proxy 8080 0

    # threaded
    $ ./proxy 8080 1

    # multi-process
    $ ./proxy 8080 2

## Todo
- Track down some bugs where CSS doesn't show up correctly on some non-HTTPS
sites (e.g. non-HTTPS Wikipedia)
- Clean up/reorganize the code and remove extraneous messages

## Notes
HTTPS works as expected. There are some HTTP features that still need to be
implemented.
